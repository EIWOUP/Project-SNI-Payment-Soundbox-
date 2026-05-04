/* =============================================================================
 * File: main.c
 * Project: UPI Payment Announcer — STM32F103C8T6 (Blue Pill)
 *
 * Hardware:
 *   USART1 (PA9/PA10)  — SIM800L GSM module @ 9600
 *   USART2 (PA2/PA3)   — DFPlayer Mini      @ 9600
 *   USART3 (PB10/PB11) — Debug console      @ 9600
 *   PC13               — User LED (HIGH = ON)
 *   PA4                — DFPlayer BUSY (LOW = playing) — polled, NOT EXTI
 *   PB3                — Vol+  button  (EXTI3,   active LOW, falling edge)
 *   PB4                — Vol-  button  (EXTI4,   active LOW, falling edge)
 *   PB5                — Prev  button  (EXTI9_5, active LOW, falling edge)
 *
 * =============================================================================
 * VOLUME DURING PLAYBACK — HOW IT WORKS
 * =============================================================================
 * dfplayer_announce_payment() is a BLOCKING call — the main loop does not run
 * while audio is playing. This means the normal "read flag → call volume func"
 * pattern in the main loop CANNOT serve volume changes during playback.
 *
 * Solution: The EXTI ISR for VOL_UP / VOL_DOWN sets two flags:
 *   vol_up_active / vol_down_active  — marks button as held for repeat logic
 *   vol_up_pending / vol_down_pending — marks that the FIRST tap step is due
 *
 * process_volume_hold() consumes both flags and calls dfplayer_volume_up/down()
 * from NON-ISR context. It is called:
 *   • every iteration of the main loop (idle path)
 *   • every 10 ms inside dfplayer_wait_until_finished() via dfplayer_poll_hook()
 *
 * WHY NOT CALL dfplayer_volume_up/down() DIRECTLY FROM THE ISR:
 *   dfplayer_volume_up/down() calls HAL_UART_Transmit(&huart2) + HAL_Delay(50).
 *   If the main loop is mid-transmit on huart2 (sending a play command during
 *   announcement), calling HAL_UART_Transmit on the same handle from inside a
 *   priority-1 EXTI ISR hits the HAL re-entrancy guard: huart2->gState is
 *   already HAL_UART_STATE_BUSY_TX, so HAL_UART_Transmit returns HAL_BUSY
 *   without sending anything AND leaves the state machine stuck BUSY forever.
 *   All subsequent DFPlayer commands are silently dropped — audio stops, the
 *   module goes silent, and the controller appears completely hung.
 *   Moving all UART TX to non-ISR context via the flag pattern eliminates this.
 *
 * HOLD-TO-REPEAT VOLUME:
 *   On a falling-edge EXTI, we cannot detect "button is still held".
 *   Strategy: A software repeat timer. The ISR records the tick of the LAST
 *   press. A lightweight check runs inside dfplayer_wait_until_finished()'s
 *   polling loop AND in the main loop. If the button is still physically LOW
 *   and the repeat interval has elapsed, another volume step is applied.
 *   GPIO_PIN_RESET = button pressed (active-LOW with pull-up).
 *
 * =============================================================================
 * PLAY_PREV DEBOUNCE
 * =============================================================================
 * A 500 ms debounce window prevents double-fires from contact bounce or
 * accidental multi-press. The flag is only set if 500 ms has elapsed since
 * the last accepted press.
 *
 * =============================================================================
 * FIRST-PAYMENT "0 Rs." BUG — ROOT CAUSE AND FIX
 * =============================================================================
 * OLD BEHAVIOUR:
 *   previous_payment_amount was initialised to 0.
 *   It was only updated AFTER the announcement block completed.
 *   If the user pressed PREV after the FIRST payment (before any second
 *   payment arrived), previous_payment_amount was still 0 — so the
 *   announcer said "You Have Received A Payment Of Zero Rupees."
 *
 * FIX:
 *   A boolean flag `has_previous_payment` is initialised to 0.
 *   It is set to 1 only when a SECOND payment arrives (i.e., when
 *   current_payment_amount already holds a valid first payment before
 *   it is pushed to previous_payment_amount).
 *   process_prev_button() checks this flag and silently ignores the press
 *   if no valid previous payment exists yet.
 *
 * =============================================================================
 * FLAG PATTERN (unchanged from previous version)
 * =============================================================================
 * HAL_GPIO_EXTI_Callback() for VOL_UP / VOL_DOWN calls dfplayer_set_volume()
 * directly (see VOLUME section above). For PLAY_PREV it still sets a flag that
 * is consumed in the main loop, because PREV triggers announce_payment() which
 * would block the ISR for seconds if called directly.
 * =============================================================================*/

#include "main.h"       /* CubeMX-generated pin defines and HAL types */
#include "usart.h"      /* huart1 / huart2 / huart3 handles */
#include "gpio.h"       /* MX_GPIO_Init() */
#include "dfplayer.h"   /* DFPlayer Mini driver */
#include "gsm.h"        /* SIM800L GSM driver */
#include "debug.h"      /* printf() → USART3 redirect */

#include <stdio.h>      /* printf() */
#include <string.h>     /* strstr(), strcmp() */
#include <stdlib.h>     /* strtoul() */
#include <stdint.h>     /* uint8_t, uint32_t */

/* -----------------------------------------------------------------------
 * Prototypes
 * ----------------------------------------------------------------------- */
void SystemClock_Config(void); /* Defined at bottom of this file */

/* -----------------------------------------------------------------------
 * Helpers
 * ----------------------------------------------------------------------- */

/* ELAPSED(start, ms) — true when HAL_GetTick() has advanced >= ms since start */
#define ELAPSED(start, ms)   ((HAL_GetTick() - (start)) >= (uint32_t)(ms))

/* -----------------------------------------------------------------------
 * LED helpers
 * ----------------------------------------------------------------------- */

/* led_on — drive PC13 HIGH (LED on) */
static inline void led_on(void) {
    HAL_GPIO_WritePin(USER_LED_GPIO_Port, USER_LED_Pin, GPIO_PIN_SET); /* PC13 HIGH = LED on */
}

/* led_off — drive PC13 LOW (LED off) */
static inline void led_off(void) {
    HAL_GPIO_WritePin(USER_LED_GPIO_Port, USER_LED_Pin, GPIO_PIN_RESET); /* PC13 LOW = LED off */
}

/* led_blink — blink LED n times with 200 ms on/off period */
static void led_blink(uint8_t n) {
    for (uint8_t i = 0; i < n; i++) { /* Repeat n times */
        led_on();          /* Turn LED on */
        HAL_Delay(200);    /* Wait 200 ms */
        led_off();         /* Turn LED off */
        HAL_Delay(200);    /* Wait 200 ms */
    }
}

/* -----------------------------------------------------------------------
 * USART1 RX — interrupt-driven single-byte reception for GSM
 * ----------------------------------------------------------------------- */

static uint8_t gsm_rx_byte; /* Single-byte DMA/IT buffer for USART1 RX */

/* gsm_rx_start — arm USART1 IT to receive the next byte into gsm_rx_byte */
static void gsm_rx_start(void) {
    HAL_UART_Receive_IT(&huart1, &gsm_rx_byte, 1); /* Request 1-byte IT receive on USART1 */
}

/* HAL_UART_RxCpltCallback — called by HAL when 1 byte has arrived on USART1.
 * Feeds the byte to the GSM parser and immediately re-arms the IT receive. */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) {         /* Only handle USART1 (SIM800L) */
        gsm_rx_byte_handler(gsm_rx_byte);    /* Feed byte to GSM line parser */
        HAL_UART_Receive_IT(&huart1, &gsm_rx_byte, 1); /* Re-arm for next byte */
    }
}

/* -----------------------------------------------------------------------
 * Volume button state — ISR + main-loop shared
 * ----------------------------------------------------------------------- */

/* VOL_UP repeat state — written in ISR, read in process_volume_hold() */
static volatile uint8_t  vol_up_active      = 0; /* 1 while PB3 is held LOW */
static volatile uint32_t vol_up_last_tick   = 0; /* Tick of last accepted vol-up step */
static volatile uint8_t  vol_up_pending     = 0; /* FIX: set by ISR, cleared by process_volume_hold() */

/* VOL_DOWN repeat state — written in ISR, read in process_volume_hold() */
static volatile uint8_t  vol_down_active    = 0; /* 1 while PB4 is held LOW */
static volatile uint32_t vol_down_last_tick = 0; /* Tick of last accepted vol-down step */
static volatile uint8_t  vol_down_pending   = 0; /* FIX: set by ISR, cleared by process_volume_hold() */

/* How many ms must elapse between successive hold-repeat steps.
 * First step fires immediately in the ISR on the falling edge.
 * Subsequent steps fire every VOL_HOLD_REPEAT_MS while the pin stays LOW. */
#define VOL_HOLD_REPEAT_MS  200U /* Repeat volume step every 200 ms while held */

/* -----------------------------------------------------------------------
 * PLAY_PREV button state — ISR + main-loop shared
 * ----------------------------------------------------------------------- */

volatile uint8_t prev_pressed = 0;            /* Flag set in ISR, cleared in main loop */
static uint32_t  prev_last_accepted_tick = 0; /* Tick of last accepted PREV press */
#define PREV_DEBOUNCE_MS  500U                /* Minimum ms between two accepted PREV presses */

/* -----------------------------------------------------------------------
 * dfplayer_ready guard — discard button commands until DFPlayer is inited
 * ----------------------------------------------------------------------- */

static uint8_t dfplayer_ready = 0; /* 0 during boot/init, 1 after dfplayer_init() */

/* -----------------------------------------------------------------------
 * Payment tracking
 * ----------------------------------------------------------------------- */

static uint32_t current_payment_amount  = 0; /* Amount (cents) of the LATEST payment */
static uint32_t previous_payment_amount = 0; /* Amount (cents) of the payment BEFORE current */
static uint8_t  has_previous_payment    = 0; /* 1 only after a second payment has arrived */

/* -----------------------------------------------------------------------
 * HAL_GPIO_EXTI_Callback — ISR context
 *
 * VOL_UP / VOL_DOWN: Volume is applied DIRECTLY here so it works even
 *   while dfplayer_announce_payment() is blocking the main loop.
 *   HAL_Delay(50) inside dfplayer_set_volume() is safe because SysTick
 *   is at priority 0 and these EXTIs are at priority 1.
 *
 * PLAY_PREV: Sets a flag only — announce_payment() would block for
 *   several seconds if called here, which would starve USART1 ISR.
 *
 * SIM800L_RING: Sets a flag inside gsm.c.
 * ----------------------------------------------------------------------- */

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {

    /* ---- VOL UP (PB3 falling edge) ------------------------------------ */
    if (GPIO_Pin == VOL_UP_Pin) {

        if (!dfplayer_ready) return; /* Ignore all button presses before DFPlayer is ready */

        vol_up_active    = 1;        /* Mark button as held — enables hold-repeat */
        vol_down_active  = 0;        /* Cancel any opposing direction hold */

        vol_up_last_tick = HAL_GetTick(); /* Record tick for hold-repeat interval */

        /* FIX: Do NOT call dfplayer_volume_up() here.
         * Calling HAL_UART_Transmit(huart2) from inside an ISR while the
         * main loop may also be mid-transmit on huart2 causes HAL_BUSY
         * re-entrancy corruption — the UART state machine gets stuck BUSY
         * forever and no further DFPlayer commands are ever sent (audio stops,
         * module goes silent, controller appears hung).
         * Instead, set a flag. process_volume_hold() is called both from the
         * main loop AND from dfplayer_poll_hook() inside the 10 ms wait loop,
         * so volume changes are applied within ≤10 ms — imperceptible delay. */
        vol_up_pending = 1; /* Signal process_volume_hold() to fire one step */
    }

    /* ---- VOL DOWN (PB4 falling edge) ---------------------------------- */
    else if (GPIO_Pin == VOL_DOWN_Pin) {

        if (!dfplayer_ready) return; /* Ignore all button presses before DFPlayer is ready */

        vol_down_active    = 1;      /* Mark button as held — enables hold-repeat */
        vol_up_active      = 0;      /* Cancel any opposing direction hold */

        vol_down_last_tick = HAL_GetTick(); /* Record tick for hold-repeat interval */

        /* FIX: Same reason as VOL_UP above — flag only, no UART TX from ISR */
        vol_down_pending = 1; /* Signal process_volume_hold() to fire one step */
    }

    /* ---- PLAY_PREV (PB5 falling edge) --------------------------------- */
    else if (GPIO_Pin == PLAY_PREV_Pin) {

        if (!dfplayer_ready) return; /* Ignore all button presses before DFPlayer is ready */

        uint32_t now = HAL_GetTick(); /* Get current tick for debounce check */

        /* Only accept the press if 500 ms has elapsed since the last accepted press */
        if ((now - prev_last_accepted_tick) >= PREV_DEBOUNCE_MS) {
            prev_last_accepted_tick = now; /* Update the debounce timestamp */
            prev_pressed = 1;             /* Set flag — main loop will handle playback */
        }
        /* If inside debounce window: silently discard — contact bounce or accidental re-press */
    }

    /* ---- SIM800L RING (PA11 falling edge) ----------------------------- */
    else if (GPIO_Pin == SIM800L_RING_Pin) {
        gsm_ring_isr_notify(); /* ISR-safe: sets a volatile flag inside gsm.c */
    }
}

/* -----------------------------------------------------------------------
 * process_volume_hold — call frequently (main loop, wait loops)
 *
 * Reads the physical GPIO pin to detect whether the button is still held.
 * If it is, and the repeat interval has elapsed, fires another volume step.
 * If the pin has been released, clears the active flag.
 * ----------------------------------------------------------------------- */
static void process_volume_hold(void) {

    /* ---- VOL UP hold check ---- */
    if (vol_up_active) {
        /* Read physical pin: GPIO_PIN_RESET = LOW = button pressed (active-LOW) */
        if (HAL_GPIO_ReadPin(VOL_UP_GPIO_Port, VOL_UP_Pin) == GPIO_PIN_RESET) {

            /* FIX: consume the pending flag set by the ISR (first tap step) */
            if (vol_up_pending) {
                vol_up_pending = 0;          /* Clear pending flag */
                vol_up_last_tick = HAL_GetTick(); /* Reset repeat timer from now */
                dfplayer_volume_up();         /* Apply the tap step here, safely outside ISR */
            }
            /* Button is still held; check if repeat interval has elapsed */
            else if (ELAPSED(vol_up_last_tick, VOL_HOLD_REPEAT_MS)) {
                vol_up_last_tick = HAL_GetTick(); /* Reset repeat timer */
                dfplayer_volume_up();             /* Apply another volume step */
            }
        } else {
            /* Pin went HIGH — button released, clear all flags */
            vol_up_active  = 0;
            vol_up_pending = 0;
        }
    }

    /* ---- VOL DOWN hold check ---- */
    if (vol_down_active) {
        /* Read physical pin: GPIO_PIN_RESET = LOW = button pressed (active-LOW) */
        if (HAL_GPIO_ReadPin(VOL_DOWN_GPIO_Port, VOL_DOWN_Pin) == GPIO_PIN_RESET) {

            /* FIX: consume the pending flag set by the ISR (first tap step) */
            if (vol_down_pending) {
                vol_down_pending = 0;          /* Clear pending flag */
                vol_down_last_tick = HAL_GetTick(); /* Reset repeat timer from now */
                dfplayer_volume_down();         /* Apply the tap step here, safely outside ISR */
            }
            /* Button is still held; check if repeat interval has elapsed */
            else if (ELAPSED(vol_down_last_tick, VOL_HOLD_REPEAT_MS)) {
                vol_down_last_tick = HAL_GetTick(); /* Reset repeat timer */
                dfplayer_volume_down();             /* Apply another volume step */
            }
        } else {
            /* Pin went HIGH — button released, clear all flags */
            vol_down_active  = 0;
            vol_down_pending = 0;
        }
    }
}

/* -----------------------------------------------------------------------
 * dfplayer_poll_hook — called every 10 ms inside dfplayer_wait_until_finished()
 *
 * This is the bridge that makes volume buttons work DURING audio playback.
 * dfplayer_wait_until_finished() blocks the main loop, but calls this hook
 * every 10 ms so we can service volume hold-repeat without an ISR UART call.
 * ----------------------------------------------------------------------- */
void dfplayer_poll_hook(void) {
    process_volume_hold(); /* Service volume up/down tap and hold-repeat */
}

/* -----------------------------------------------------------------------
 * Amount parser — USD
 * Searches the SMS body for a USD amount in any of these formats:
 *   "$2.95"   "$95"   "USD 10789.29"   "USD10789"   "$ 2.95"
 *   The "$" or "USD" token can appear anywhere in the message.
 *
 * Returns 1 and writes the total value in cents to *out_cents on success.
 * Returns 0 if no recognised USD pattern is found or the number is invalid.
 *
 * Parsing rules:
 *   • Tries "$" prefix first, then "USD" prefix.
 *   • Optional spaces between prefix and the first digit are skipped.
 *   • Fractional part (cents) is optional.
 *   • Only the first two decimal digits are used; trailing digits ignored.
 *   • A single fractional digit is treated as tenths (e.g. ".9" → 90 cents).
 * ----------------------------------------------------------------------- */
static uint8_t parse_dollar_amount(const char *sms, uint32_t *out_cents) {
    if (!sms || !out_cents) return 0; /* NULL guard */

    const char *p = NULL;

    /* --- Try "$" prefix (handles "$2.95", "$ 95", etc.) --- */
    p = strstr(sms, "of");
    if (p) {
        p += 3; /* skip 'of' */
    } else {
        /* --- Try "USD" prefix (handles "USD 10789.29", "USD95") --- */
        p = strstr(sms, "USD");
        if (!p) return 0; /* no recognised prefix found */
        p += 4; /* skip 'USD' */
    }

    while (*p == ' ') p++;               /* skip optional spaces after prefix */
    if (*p < '0' || *p > '9') return 0; /* must start with a digit */

    char *end;
    unsigned long whole = strtoul(p, &end, 10); /* parse integer dollar part */
    if (end == p) return 0;                     /* no digits consumed */

    uint32_t cents = 0;

    if (*end == '.') {                          /* fractional (cents) part present */
        const char *frac = end + 1;
        if (*frac >= '0' && *frac <= '9') {
            uint8_t d1 = (uint8_t)(*frac - '0');   /* tenths digit  */
            uint8_t d2 = 0;
            if (*(frac + 1) >= '0' && *(frac + 1) <= '9') {
                d2 = (uint8_t)(*(frac + 1) - '0'); /* hundredths digit */
            }
            cents = (uint32_t)(d1 * 10u + d2);
        }
    }

    *out_cents = (uint32_t)(whole * 100UL) + cents;
    return 1; /* success */
}

/* -----------------------------------------------------------------------
 * Application state machine
 * ----------------------------------------------------------------------- */

typedef enum {
    STATE_IDLE,        /* Waiting for SMS or button event */
    STATE_ANNOUNCING,  /* Audio announcement in progress (blocking) */
} AppState;

static AppState app_state = STATE_IDLE; /* Initial state: idle */

/* -----------------------------------------------------------------------
 * main
 * ----------------------------------------------------------------------- */

int main(void) {

    /* ---- HAL + peripheral init ---------------------------------------- */
    HAL_Init();            /* Initialise HAL, configure SysTick at priority 0 */
    SystemClock_Config();  /* Configure HSI clock at 8 MHz, no PLL */

    MX_GPIO_Init();        /* Configure all GPIO; arms EXTI NVIC — buttons live from here */
    MX_USART1_UART_Init(); /* USART1: SIM800L @ 9600 */
    MX_USART2_UART_Init(); /* USART2: DFPlayer @ 9600 */
    MX_USART3_UART_Init(); /* USART3: Debug → TeraTerm @ 9600 */

    /* ---- Debug banner ------------------------------------------------- */
    printf("\r\n=== USD Payment Announcer — Boot ===\r\n");  /* Project banner */
    printf("USART1=SIM800L  USART2=DFPlayer  USART3=Debug\r\n");
    printf("Buttons: VOL+ PB3(EXTI3)  VOL- PB4(EXTI4)  PREV PB5(EXTI9_5)\r\n");
    printf("BUSY: PA4 polled @ 10ms (EXTI4 owned by VOL-)\r\n\r\n");

    /* ---- DFPlayer init ------------------------------------------------ */
    /* dfplayer_init() blocks ~4.3 s with internal HAL_Delay calls.
     * dfplayer_ready = 0 here so ISR discards any buttons pressed during boot. */
    printf("[INIT] DFPlayer init...\r\n");
    dfplayer_init();        /* Boot wait → reset → TF source → volume → amp on */
    dfplayer_ready = 1;     /* From this point buttons are accepted */
    printf("[INIT] DFPlayer ready. Volume=%u\r\n", dfplayer_get_volume());

    /* ---- GSM init ----------------------------------------------------- */
    /* gsm_init() blocks ~5-8 s. Arm USART1 IT first so bytes are captured. */
    gsm_rx_start();         /* Arm USART1 IT receive before gsm_init() polls */
    printf("[INIT] GSM init...\r\n");
    gsm_init();             /* Hardware reset → AT handshake → SMS text mode */
    printf("[INIT] GSM ready.\r\n");

    /* ---- Startup SMS -------------------------------------------------- */
    printf("[SMS] Sending startup message...\r\n");
    gsm_send_sms("+919664644881", "Soundbox_Dollar Initiated"); /* Send startup SMS to configured number */

    uint32_t sms_wait_start = HAL_GetTick(); /* Record start time for SMS send timeout */
    uint8_t  sms_sent_ok    = 0;             /* 0 until +CMGS + OK is received */

    /* Wait up to 10 s for SMS send confirmation, pumping volume hold in the loop */
    while (!ELAPSED(sms_wait_start, 10000)) {  /* Loop until 10 s elapsed */
        if (gsm_sms_sent_occurred()) {         /* Check if SMS confirmed sent */
            sms_sent_ok = 1;                   /* Mark success */
            break;                             /* Exit wait loop immediately */
        }
        process_volume_hold(); /* Service hold-repeat volume during this wait */
        HAL_Delay(10);         /* 10 ms poll interval */
    }

    if (sms_sent_ok) {
        printf("[SMS] Startup SMS sent OK. Blinking 2x.\r\n");
        led_blink(2); /* 2 blinks = success */
    } else {
        printf("[SMS] Startup SMS FAILED. Blinking 3x.\r\n");
        led_blink(3); /* 3 blinks = failure */
    }

    /* ---- Main loop ---------------------------------------------------- */
    printf("[LOOP] Entering main loop. Waiting for SMS...\r\n\r\n");

    while (1) {

        /* ---- Process volume hold-repeat first (highest urgency) ------- */
        process_volume_hold(); /* Apply repeat volume step if button still held */

        /* ---- Check for incoming SMS ----------------------------------- */
        if (gsm_sms_received_occurred()) { /* Non-blocking; clears internal flag */
        	led_blink(2);
            const char *sender  = gsm_get_sender(); /* Pointer to static sender buffer */
            const char *message = gsm_get_sms();    /* Pointer to static SMS body buffer */

            printf("[SMS] From: %s\r\n", sender);   /* Log sender number */
            printf("[SMS] Body: %s\r\n", message);  /* Log SMS body */

            uint32_t amount_cents = 0; /* Will hold parsed USD value in cents */

            if (parse_dollar_amount(message, &amount_cents)) { /* Try to extract $ amount */

                printf("[PAY] Amount Received: %lu cents ($%lu.%02lu)\r\n",
                       (unsigned long) amount_cents,
                       (unsigned long)(amount_cents / 100),
                       (unsigned long)(amount_cents % 100));

                if (app_state == STATE_IDLE) { /* Only announce if not already playing */

                    /* ---- Update payment history BEFORE announcing -----
                     * Push current → previous only when current is already
                     * valid (non-zero). This prevents the first payment ever
                     * pushing 0 into previous_payment_amount.              */
                    if (current_payment_amount > 0) {
                        /* A real earlier payment exists; shift it to previous */
                        previous_payment_amount = current_payment_amount; /* Save old current */
                        has_previous_payment    = 1; /* Flag: PREV button now has valid data */
                    }
                    current_payment_amount = amount_cents; /* Store new payment in cents */

                    printf("[AUD] Starting announcement for %lu cents...\r\n",
                           (unsigned long) current_payment_amount);

                    app_state = STATE_ANNOUNCING; /* Mark announcer as busy */

                    /* Blocking call — process_volume_hold() is called every
                     * 10 ms inside dfplayer_wait_until_finished() via
                     * dfplayer_poll_hook(), so volume buttons work during audio. */
                    dfplayer_announce_payment(current_payment_amount);

                    app_state = STATE_IDLE; /* Announcement done — return to idle */
                    printf("[AUD] Announcement complete.\r\n\r\n");

                } else {
                    printf("[AUD] Busy — SMS dropped. Try again.\r\n\r\n");
                }

            } else {
                printf("[PAY] No '$' or 'USD' amount found in SMS. Ignoring.\r\n\r\n");
            }
        }

        /* ---- Short yield before button checks ------------------------- */
        HAL_Delay(5); /* 5 ms yield — keeps main loop responsive */

        /* ---- PLAY_PREV button ---------------------------------------- */
        if (prev_pressed) {             /* Flag was set in ISR (with 500 ms debounce) */
            prev_pressed = 0;           /* Clear the flag before acting on it */

            if (!has_previous_payment && current_payment_amount == 0) {
                /* No payment has ever been received — nothing at all to replay.
                 * Silently ignore so we never announce "Zero Rupees". */
                printf("[PREV] No payment recorded yet. PREV ignored.\r\n");

            } else if (!has_previous_payment && current_payment_amount > 0) {
                /* Only one payment has been received so far — there is no distinct
                 * "previous" amount, but replaying the current one is the right
                 * behaviour (the user just wants to hear the amount again). */
                printf("[PREV] Only one payment so far — replaying current: %lu cents\r\n",
                       (unsigned long) current_payment_amount);
                app_state = STATE_ANNOUNCING;                             /* Mark busy */
                dfplayer_announce_payment(current_payment_amount);        /* Replay current */
                app_state = STATE_IDLE;                                   /* Done */
                printf("[PREV] Replay complete.\r\n\r\n");

            } else if (app_state != STATE_IDLE) {
                /* Currently announcing — do not interrupt */
                printf("[PREV] Busy — PREV request ignored.\r\n");

            } else {
                /* Valid previous payment exists and announcer is idle — replay it */
                printf("[PREV] Replaying previous payment: %lu cents\r\n",
                       (unsigned long) previous_payment_amount);
                app_state = STATE_ANNOUNCING;                               /* Mark busy */
                dfplayer_announce_payment(previous_payment_amount);         /* Replay */
                app_state = STATE_IDLE;                                     /* Done */
                printf("[PREV] Previous announcement complete.\r\n\r\n");
            }
        }

    } /* end while(1) */
}

/* -----------------------------------------------------------------------
 * SystemClock_Config — HSI @ 8 MHz, no PLL
 * ----------------------------------------------------------------------- */
void SystemClock_Config(void) {
    RCC_OscInitTypeDef     RCC_OscInitStruct = { 0 }; /* Oscillator init struct */
    RCC_ClkInitTypeDef     RCC_ClkInitStruct = { 0 }; /* Clock tree init struct */
    RCC_PeriphCLKInitTypeDef PeriphClkInit   = { 0 }; /* Peripheral clock init struct */

    /* Use internal HSI oscillator, no PLL */
    RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_HSI; /* Select HSI */
    RCC_OscInitStruct.HSIState            = RCC_HSI_ON;             /* Enable HSI */
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT; /* Factory trim */
    RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_NONE;           /* PLL disabled */

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) { /* Apply oscillator config */
        Error_Handler(); /* Hang on failure */
    }

    /* Route HSI directly to SYSCLK; all bus dividers = 1 */
    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                     | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_HSI; /* SYSCLK = HSI = 8 MHz */
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;      /* HCLK  = 8 MHz */
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;        /* PCLK1 = 8 MHz */
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;        /* PCLK2 = 8 MHz */

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK) { /* Apply clock config */
        Error_Handler(); /* Hang on failure */
    }

    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;    /* Configure ADC clock */
    PeriphClkInit.AdcClockSelection    = RCC_ADCPCLK2_DIV2;    /* ADC clock = PCLK2/2 = 4 MHz */

    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK) { /* Apply peripheral clock config */
        Error_Handler(); /* Hang on failure */
    }
}

/* -----------------------------------------------------------------------
 * Error_Handler — disable IRQs, print message, blink rapidly forever
 * ----------------------------------------------------------------------- */
void Error_Handler(void) {
    __disable_irq();                                   /* Mask all interrupts */
    printf("[ERR] Error_Handler triggered!\r\n");      /* Log error via debug UART */
    while (1) {                                        /* Hang forever with fast blink */
        led_on();       /* LED on */
        HAL_Delay(100); /* 100 ms on */
        led_off();      /* LED off */
        HAL_Delay(100); /* 100 ms off */
    }
}

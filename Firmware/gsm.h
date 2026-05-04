// =============================================================================
// File: gsm.h
// GSM/SIM800L driver — STM32F103C8T6 (Blue Pill)
//
// Pin Connections (from main.h / CubeMX):
//   SIM800L RST  => PA13  (SIM800L_RST_Pin  / SIM800L_RST_GPIO_Port)
//   SIM800L RING => PA11  (SIM800L_RING_Pin / SIM800L_RING_GPIO_Port, EXTI falling)
//   SIM800L DTR  => PA12  (SIM800L_DTR_Pin  / SIM800L_DTR_GPIO_Port)
//   SIM800L TX   => PA10  (USART1 RX on MCU)
//   SIM800L RX   => PA9   (USART1 TX on MCU)
//
// UART: USART1 (huart1), 9600 baud, NVIC global interrupt enabled.
//
// !! PA13 = SWDIO on Blue Pill !!
//   Using PA13 as RST blocks SWD re-programming while the pin is held LOW.
//   The reset pulse is only 200 ms — power-cycle the board immediately after
//   flashing to regain SWD, or reassign RST to PB0/PA8 in your next revision.
//
// !! PA11 / PA12 on Blue Pill !!
//   PA11 = USB D−  and  PA12 = USB D+
//   USB transceiver may hold PA11 LOW, causing false RING detections.
//   Use gsm_ring_poll() (EXTI + 5 s debounce) NOT gsm_ring_active() in
//   the main loop to avoid false triggers.
//
// NOTE: No printf() in this driver. All logging is done in main.c.
// =============================================================================

#ifndef GSM_H
#define GSM_H

#include "main.h"           /* CubeMX pin label defines */
#include "stm32f1xx_hal.h"  /* HAL types               */
#include <stdint.h>

/* -----------------------------------------------------------------------
 * Allowed sender — only SMS from this number triggers the payment flow
 * ----------------------------------------------------------------------- */
#define GSM_ALLOWED_SENDER   "+919664644881"

/* -----------------------------------------------------------------------
 * Initialisation
 * ----------------------------------------------------------------------- */

/**
 * @brief  Hardware-reset SIM800L and run AT initialisation sequence.
 *         Blocks ~5-8 s (reset pulse + boot wait + AT handshake).
 *         Call once after peripheral init, before the main loop.
 */
void gsm_init(void);

/* -----------------------------------------------------------------------
 * Transmit
 * ----------------------------------------------------------------------- */

/**
 * @brief  Send a raw AT command string over USART1 (blocking).
 * @param  cmd  Null-terminated string, e.g. "AT\r\n"
 */
void gsm_send(const char *cmd);

/**
 * @brief  Send an SMS message.
 * @param  number   Destination in E.164 format, e.g. "+919104480314"
 * @param  message  Null-terminated body (≤160 chars for single SMS)
 *
 * Confirmation arrives asynchronously via the UART ISR.
 * Poll gsm_sms_sent_occurred() in the main loop.
 */
void gsm_send_sms(const char *number, const char *message);

/* -----------------------------------------------------------------------
 * RX parser — feed from UART ISR
 * ----------------------------------------------------------------------- */

/**
 * @brief  Feed one received byte into the line parser.
 *         Call from HAL_UART_RxCpltCallback() for USART1 only.
 */
void gsm_rx_byte_handler(uint8_t byte);

/* -----------------------------------------------------------------------
 * Event polling — non-blocking, call from main loop
 * ----------------------------------------------------------------------- */

/** @brief Returns 1 (and clears flag) when an SMS was confirmed sent (+CMGS + OK). */
uint8_t gsm_sms_sent_occurred(void);

/** @brief Returns 1 (and clears flag) when an SMS from the allowed sender arrived. */
uint8_t gsm_sms_received_occurred(void);

/* -----------------------------------------------------------------------
 * SMS data accessors
 * ----------------------------------------------------------------------- */

/** @brief Returns 1 if a new SMS body is ready to read. */
uint8_t gsm_sms_available(void);

/**
 * @brief  Retrieve last received SMS body and mark it consumed.
 * @return Pointer to static internal buffer — copy if persistence needed.
 */
const char* gsm_get_sms(void);

/** @brief Return the sender number of the last received SMS. */
const char* gsm_get_sender(void);

/* -----------------------------------------------------------------------
 * Call handling — auto-reject
 * ----------------------------------------------------------------------- */

/**
 * @brief  Reject an incoming call by sending ATH.
 *         Called automatically inside gsm_rx_byte_handler() on "RING" URC.
 *         AT+GSMBUSY=1 (set in gsm_init) is the faster primary path.
 *         This is exposed for diagnostic use — you do not need to call it.
 */
void gsm_reject_call(void);

/* -----------------------------------------------------------------------
 * RING detection (hardware pin via EXTI)
 * ----------------------------------------------------------------------- */

/**
 * @brief  Call from HAL_GPIO_EXTI_Callback() on the PA11 falling-edge IRQ.
 *         Records the event without blocking (ISR-safe).
 */
void gsm_ring_isr_notify(void);

/**
 * @brief  Non-blocking poll for a new RING event.
 *         5 s debounce window suppresses repeated triggers and USB noise.
 *         Call in the main loop instead of gsm_ring_active().
 * @return 1 if a new RING event outside the debounce window, else 0.
 */
uint8_t gsm_ring_poll(void);

/**
 * @brief  Direct RING pin read.
 *         Avoid in the main loop — use gsm_ring_poll() instead.
 */
uint8_t gsm_ring_active(void);

/* -----------------------------------------------------------------------
 * DTR (sleep control)
 * ----------------------------------------------------------------------- */

/** @brief Assert DTR LOW  (wake SIM800L from sleep). */
void gsm_dtr_wake(void);

/** @brief De-assert DTR HIGH (allow SIM800L to enter sleep). */
void gsm_dtr_sleep(void);

#endif /* GSM_H */

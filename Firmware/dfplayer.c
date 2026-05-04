// =============================================================================
// File: dfplayer.c
// DFPlayer Mini Driver — STM32F103C8T6 (Blue Pill) — USD Currency Edition
//
// Audio File Layout (34 files):
//   0001–0020  One … Twenty
//   0021–0027  Thirty … Ninety
//   0028       Hundred
//   0029       Thousand
//   0030       Cents
//   0031       "You Have Received A Payment Of"
//   0032       Dollar
//   0033       And
//   0034       Zero
//
// Announcement format:
//   "You Have Received A Payment Of
//    [X Thousand] [Y Hundred] [remainder] Dollar [And Z Cents]"
//
// Hardware Wiring:
//   MCU USART2 TX (PA2) -> DFPlayer RX
//   MCU USART2 RX (PA3) -> DFPlayer TX
//   MCU PA4  (DFPLAYER_BUSY_Pin)  -> DFPlayer BUSY (LOW = Playing)
//   MCU PB15 (PAM8403_SHDWN_Pin)  -> PAM8403 SHDN  (HIGH = On)
//   MCU PA8  (PAM8403_MUTE_Pin)   -> PAM8403 MUTE  (HIGH = Un-Muted)
//
// BUSY Pin Strategy:
//   DFPLAYER_BUSY_Pin (PA4) is on EXTI line 4.
//   VOL_DOWN_Pin (PB4) is also on EXTI line 4.
//   Both cannot share the same EXTI line — BUSY is handled by polling only.
//   Polling interval is 10 ms — fast enough to be imperceptible at word
//   boundaries and costs negligible CPU since we are blocking anyway.
//
// Delay Architecture (why each delay exists):
//
//   dfplayer_send_command() — NO blanket delay.
//     The old 100 ms delay here was applied unconditionally after every packet,
//     including during announcement playback where we block on BUSY anyway.
//     Removing it saves ~100 ms per word boundary. Callers that genuinely
//     need a post-command settle (init, volume, prev) add their own delay.
//
//   dfplayer_play_audio() — 30 ms after CMD_PLAY_TRACK.
//     DFPlayer clone firmware takes ~20-30 ms to begin asserting BUSY after
//     receiving the play command. Without this, dfplayer_wait_until_finished()
//     sees BUSY still HIGH, interprets it as "never started", and times out.
//
//   dfplayer_wait_until_finished() — polls every 10 ms, 30 ms post-BUSY settle.
//     Old interval was 50 ms — contributed ~35 ms average overshoot per word
//     (poll can miss the rising edge by up to one full interval).
//     New 10 ms interval reduces average overshoot to ~5 ms.
//     Post-BUSY settle reduced from 100 ms to 30 ms — empirically sufficient
//     for clone hardware to complete internal state cleanup before the next
//     CMD_PLAY_TRACK. Going below ~25 ms risks the command being ignored.
//
//   dfplayer_volume_up/down/set/play_previous() — 50 ms post-command.
//     Volume and navigation commands do not use BUSY for synchronisation.
//     50 ms lets the module process the command before the next one arrives.
//     These are user-triggered (button ISR → main loop) so 50 ms is invisible.
//
// Net effect on announcement timing:
//   Old gap between words: ~250 ms  (100 cmd + 50 poll-miss + 100 settle)
//   New gap between words:  ~65 ms  (30 play-start + 10 poll-miss + 30 settle)
//
// FIX RETAINED (Clone DFPlayer CMD_PLAYBACK_SOURCE auto-play bug):
//   After CMD_PLAYBACK_SOURCE + 200 ms, CMD_PAUSE is sent so the module
//   is in a clean paused state before any play command is issued.
//   See original comment block for full explanation.
//
// NOTE:
//   - No printf() in this file. All logging is in main.c.
//   - Buttons (PB3/PB4/PB5) ISR callbacks are handled in main.c.
// =============================================================================

#include "dfplayer.h"

/* ============================================================
 *  Module-Private State
 * ============================================================ */
static uint8_t current_volume = DFPLAYER_DEFAULT_VOLUME;

/* ============================================================
 *  Internal Helpers
 * ============================================================ */

/**
 * Two's-complement checksum for a DFPlayer packet.
 * Checksum = 0 - (sum of bytes from Version through Para_Low).
 */
static uint16_t dfplayer_checksum(uint8_t ver, uint8_t len, uint8_t cmd,
		uint8_t fb, uint8_t ph, uint8_t pl) {
	return (uint16_t) (0u - (uint16_t) (ver + len + cmd + fb + ph + pl));
}

/**
 * Build and transmit a 10-byte DFPlayer serial packet.
 * Packet format: [0x7E][0xFF][0x06][CMD][FB][ParaH][ParaL][CkH][CkL][0xEF]
 *
 * NO blanket delay here — see file header for rationale.
 * Each call site adds only the delay its hardware context requires.
 */
static void dfplayer_send_command(uint8_t cmd, uint8_t para_h, uint8_t para_l) {
	uint8_t pkt[10];

	pkt[0] = DFPLAYER_START_BYTE;
	pkt[1] = DFPLAYER_VERSION;
	pkt[2] = DFPLAYER_LENGTH;
	pkt[3] = cmd;
	pkt[4] = DFPLAYER_NO_FEEDBACK;
	pkt[5] = para_h;
	pkt[6] = para_l;

	uint16_t ck = dfplayer_checksum(pkt[1], pkt[2], pkt[3], pkt[4], pkt[5],
			pkt[6]);
	pkt[7] = (uint8_t) (ck >> 8);
	pkt[8] = (uint8_t) (ck & 0xFF);
	pkt[9] = DFPLAYER_END_BYTE;

	HAL_UART_Transmit(&DFPLAYER_UART, pkt, 10, HAL_MAX_DELAY);
	/* No HAL_Delay() here — callers manage their own post-command timing */
}

/* ============================================================
 *  PAM8403 Amplifier Control
 * ============================================================ */

void pam8403_enable(void) {
	HAL_GPIO_WritePin(PAM8403_SHDWN_GPIO_Port, PAM8403_SHDWN_Pin, GPIO_PIN_SET);
	HAL_GPIO_WritePin(PAM8403_MUTE_GPIO_Port, PAM8403_MUTE_Pin, GPIO_PIN_SET);
}

void pam8403_disable(void) {
	HAL_GPIO_WritePin(PAM8403_SHDWN_GPIO_Port, PAM8403_SHDWN_Pin,
			GPIO_PIN_RESET);
}

void pam8403_mute(void) {
	HAL_GPIO_WritePin(PAM8403_MUTE_GPIO_Port, PAM8403_MUTE_Pin, GPIO_PIN_RESET);
}

void pam8403_unmute(void) {
	HAL_GPIO_WritePin(PAM8403_MUTE_GPIO_Port, PAM8403_MUTE_Pin, GPIO_PIN_SET);
}

/* ============================================================
 *  Public API
 * ============================================================ */

void dfplayer_init(void) {
	/* Each HAL_Delay() here is load-bearing — the module genuinely needs
	 * this time during boot/reset. They are NOT copied from the old blanket
	 * delay in dfplayer_send_command(). */
	HAL_Delay(2000); /* Boot wait */
	dfplayer_send_command(CMD_RESET, 0x00, 0x00);
	HAL_Delay(1500); /* Reset settle */
	dfplayer_send_command(CMD_PLAYBACK_SOURCE, 0x00, 0x02); /* TF card */
	HAL_Delay(500); /* TF enumeration */

	/* Clone firmware auto-starts playback after CMD_PLAYBACK_SOURCE.
	 * Pause immediately so CMD_PLAY_TRACK later starts the correct track. */
	dfplayer_send_command(CMD_PAUSE, 0x00, 0x00);
	HAL_Delay(200); /* Pause settle */

	dfplayer_set_volume(current_volume);
	HAL_Delay(200); /* Volume settle */

	pam8403_enable();
	HAL_Delay(300); /* Amp settle */
}

/**
 * Jump directly to track N (1-based) and play it.
 * Waits 30 ms after the command so the module has time to begin asserting
 * BUSY before dfplayer_wait_until_finished() starts polling.
 */
void dfplayer_play_audio(uint8_t file_number) {
	dfplayer_send_command(CMD_PLAY_TRACK, 0x00, file_number);
	HAL_Delay(30); /* Allow DFPlayer ~20-30 ms to assert BUSY */
}

/** Returns 1 if BUSY pin is LOW (playing), 0 if idle. */
uint8_t dfplayer_is_busy(void) {
	return (HAL_GPIO_ReadPin(DFPLAYER_BUSY_GPIO_Port, DFPLAYER_BUSY_Pin)
			== GPIO_PIN_RESET) ? 1u : 0u;
}

/* ============================================================
 *  dfplayer_poll_hook — weak default (no-op)
 *  Override in main.c to call process_volume_hold() so volume
 *  buttons work during blocking announcement playback.
 * ============================================================ */
__weak void dfplayer_poll_hook(void) {
    /* Default: do nothing. Override in main.c. */
}

/**
 * Block until the current track finishes.
 *
 * Phase 1 — wait for BUSY to assert (track started).
 *   Timeout 500 ms: if BUSY never goes LOW the command was likely rejected.
 *   Poll every 10 ms — fast enough to catch assertion within one interval.
 *
 * Phase 2 — wait for BUSY to de-assert (track finished).
 *   Poll every 10 ms (was 50 ms).
 *   Average overshoot reduced from ~25 ms to ~5 ms per word boundary.
 *
 * Phase 3 — 30 ms post-BUSY settle.
 *   Gives the module's internal state machine time to complete cleanup
 *   before the next CMD_PLAY_TRACK arrives. Empirically 25-30 ms is the
 *   minimum reliable value on common clone hardware; 100 ms was excessive.
 */
void dfplayer_wait_until_finished(void) {
	/* Phase 1: wait up to 500 ms for BUSY to assert */
	uint32_t start = HAL_GetTick();
	while (!dfplayer_is_busy()) {
		if ((HAL_GetTick() - start) >= 500u) {
			return; /* BUSY never asserted — bail out */
		}
		dfplayer_poll_hook(); /* service volume buttons during wait */
		HAL_Delay(10);
	}

	/* Phase 2: wait for BUSY to de-assert (track complete) */
	while (dfplayer_is_busy()) {
		dfplayer_poll_hook(); /* service volume buttons during wait */
		HAL_Delay(10); /* poll every 10 ms — was 50 ms */
	}

	/* Phase 3: post-BUSY settle before next command */
	HAL_Delay(30); /* was 100 ms — 30 ms is sufficient for clone hardware */
}

/* ---- Volume controls ---------------------------------------------------- */

void dfplayer_volume_up(void) {
	if (current_volume < 30) {
		current_volume++;
		dfplayer_send_command(CMD_VOL_UP, 0x00, 0x00);
		HAL_Delay(50); /* volume commands need ~50 ms to take effect */
	}
}

void dfplayer_volume_down(void) {
	if (current_volume > 0) {
		current_volume--;
		dfplayer_send_command(CMD_VOL_DOWN, 0x00, 0x00);
		HAL_Delay(50);
	}
}

void dfplayer_set_volume(uint8_t volume) {
	if (volume > 30)
		volume = 30;
	current_volume = volume;
	dfplayer_send_command(CMD_SET_VOLUME, 0x00, volume);
	HAL_Delay(50);
}

uint8_t dfplayer_get_volume(void) {
	return current_volume;
}

/* ---- Playback controls -------------------------------------------------- */

void dfplayer_play_previous(void) {
	dfplayer_send_command(CMD_PREV, 0x00, 0x00);
	HAL_Delay(50);
}

void dfplayer_play_next(void) {
	dfplayer_send_command(CMD_NEXT, 0x00, 0x00);
	HAL_Delay(50);
}

void dfplayer_pause(void) {
	dfplayer_send_command(CMD_PAUSE, 0x00, 0x00);
	HAL_Delay(50);
}

void dfplayer_resume(void) {
	dfplayer_send_command(CMD_PLAY, 0x00, 0x00);
	HAL_Delay(50);
}

void dfplayer_stop(void) {
	dfplayer_send_command(CMD_PAUSE, 0x00, 0x00);
	HAL_Delay(50);
}

/* ============================================================
 *  Payment Announcement — Private Helpers
 * ============================================================ */

/**
 * Play one audio file and block until it finishes.
 * Thin wrapper to keep announcement code readable.
 */
static void play_and_wait(uint8_t file_num) {
	dfplayer_play_audio(file_num); /* sends command + 30 ms BUSY-assert wait */
	dfplayer_wait_until_finished(); /* polls at 10 ms + 30 ms settle */
}

/**
 * Announce a number 1–99 using the correct English words.
 *
 *  1–9   → direct digit file
 * 10–19  → teen file
 * 20–99  → tens word + optional ones digit
 *
 * Does nothing for n == 0.
 */
static void announce_two_digit(uint32_t n) {
	if (n == 0)
		return;

	if (n <= 9) {
		play_and_wait((uint8_t) (AUDIO_FILE_ONE + (n - 1)));

	} else if (n <= 19) {
		play_and_wait((uint8_t) (AUDIO_FILE_TEN + (n - 10)));

	} else {
		uint32_t tens = n / 10;
		uint32_t ones = n % 10;

		/* Tens: 20→File20(Twenty), 30→File21(Thirty) … 90→File27(Ninety) */
		play_and_wait((uint8_t) (AUDIO_FILE_TWENTY + (tens - 2)));

		if (ones > 0) {
			play_and_wait((uint8_t) (AUDIO_FILE_ONE + (ones - 1)));
		}
	}
}

/**
 * Announce the whole-dollar amount (1–99999).
 * Handles: [X Thousand] [Y Hundred] [remainder]
 * Used internally by dfplayer_announce_payment().
 * Does nothing for n == 0.
 *
 * Max supported: 99,999 dollars (five digits).
 * Extend with a millions layer if larger amounts are needed.
 */
static void announce_dollar_amount(uint32_t dollars) {
	if (dollars == 0)
		return;

	uint32_t thousands = dollars / 1000;
	uint32_t remainder = dollars % 1000;

	if (thousands > 0) {
		announce_two_digit(thousands);
		play_and_wait(AUDIO_FILE_THOUSAND);
	}

	uint32_t hundreds = remainder / 100;
	uint32_t sub_100  = remainder % 100;

	if (hundreds > 0) {
		play_and_wait((uint8_t) (AUDIO_FILE_ONE + (hundreds - 1)));
		play_and_wait(AUDIO_FILE_HUNDRED);
	}

	announce_two_digit(sub_100);
}

/* ============================================================
 *  dfplayer_announce_payment — Public
 *
 *  Singular/plural rules (no "And" between dollars and cents):
 *    dollars == 1  → DOLLAR  (file 32)   dollars >= 2 → DOLLARS (file 33)
 *    cents   == 1  → CENT    (file 30)   cents   >= 2 → CENTS   (file 31)
 *
 *  @param amount_cents  Total amount in cents (100 cents = $1).
 *
 *  Examples:
 *    $1        → "...Of" ONE DOLLAR
 *    $2.95     → "...Of" TWO DOLLARS NINETY FIVE CENTS
 *    $95       → "...Of" NINETY FIVE DOLLARS
 *    $10789.29 → "...Of" TEN THOUSAND SEVEN HUNDRED EIGHTY NINE DOLLARS
 *                         TWENTY NINE CENTS
 *    $0.01     → "...Of" ONE CENT
 *    $2.01     → "...Of" TWO DOLLARS ONE CENT
 * ============================================================ */
void dfplayer_announce_payment(uint32_t amount_cents) {
	uint32_t dollars = amount_cents / 100;
	uint32_t cents   = amount_cents % 100;

	/* Opening phrase: "You Have Received A Payment Of" */
	play_and_wait(AUDIO_FILE_PAYMENT_MSG);

	/* Edge case: zero */
	if (amount_cents == 0) {
		play_and_wait(AUDIO_FILE_ZERO);
		play_and_wait(AUDIO_FILE_DOLLARS);   /* "Zero Dollars" */
		return;
	}

	/* ---- Dollars section ---- */
	if (dollars > 0) {
		announce_dollar_amount(dollars);
		if (dollars == 1) {
			play_and_wait(AUDIO_FILE_DOLLAR);    /* file 32 — "Dollar"  */
		} else {
			play_and_wait(AUDIO_FILE_DOLLARS);   /* file 33 — "Dollars" */
		}
	}

	/* ---- Cents section (only when non-zero, no "And") ---- */
	if (cents > 0) {
		announce_two_digit(cents);
		if (cents == 1) {
			play_and_wait(AUDIO_FILE_CENT);      /* file 30 — "Cent"  */
		} else {
			play_and_wait(AUDIO_FILE_CENTS);     /* file 31 — "Cents" */
		}
	}
}

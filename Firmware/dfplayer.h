// =============================================================================
// File: dfplayer.h
// DFPlayer Mini Driver Header — STM32F103C8T6 (Blue Pill)
//
// Hardware wiring:
//   MCU USART2 TX (PA2) → DFPlayer RX
//   MCU USART2 RX (PA3) → DFPlayer TX
//   MCU PA4  (DFPLAYER_BUSY_Pin)  → DFPlayer BUSY (LOW = playing, EXTI rising)
//   MCU PB15 (PAM8403_SHDWN_Pin)  → PAM8403 SHDN  (HIGH = on)
//   MCU PA8  (PAM8403_MUTE_Pin)   → PAM8403 MUTE  (HIGH = un-muted)
//   MCU PB3  (VOL_UP_Pin)         → Volume Up   button (polled, active LOW)
//   MCU PB4  (VOL_DOWN_Pin)       → Volume Down button (polled, active LOW)
//   MCU PB5  (PLAY_PREV_Pin)      → Play Prev   button (polled, active LOW)
//
// NOTE: No printf() in this driver. All logging is done in main.c.
//       Buttons are polled in main.c for maximum responsiveness.
// =============================================================================

#ifndef DFPLAYER_H
#define DFPLAYER_H

#include "main.h"   /* CubeMX pin label defines */
#include "usart.h"  /* huart2, huart3            */
#include <stdint.h>

/* ============================================================
 *  DFPlayer Serial Protocol
 *  Packet: [0x7E][0xFF][0x06][CMD][FB][ParaH][ParaL][CkH][CkL][0xEF]
 * ============================================================ */
#define DFPLAYER_START_BYTE     0x7E
#define DFPLAYER_VERSION        0xFF
#define DFPLAYER_LENGTH         0x06
#define DFPLAYER_END_BYTE       0xEF
#define DFPLAYER_NO_FEEDBACK    0x00

/* DFPlayer Command Codes */
#define CMD_NEXT                0x01
#define CMD_PREV                0x02
#define CMD_PLAY_TRACK          0x03
#define CMD_VOL_UP              0x04
#define CMD_VOL_DOWN            0x05
#define CMD_SET_VOLUME          0x06
#define CMD_REPEAT_PLAY         0x08
#define CMD_PLAYBACK_SOURCE     0x09
#define CMD_SLEEP               0x0A
#define CMD_WAKE                0x0B
#define CMD_RESET               0x0C
#define CMD_PLAY                0x0D
#define CMD_PAUSE               0x0E
#define CMD_SELECT_FOLDER       0x0F
#define CMD_VOL_ADJUST	     	0x10
#define CMD_REPEAT				0x11

/* ============================================================
 *  UART — DFPlayer on USART2 (PA2=TX, PA3=RX) @ 9600
 * ============================================================ */
#define DFPLAYER_UART           huart2

/* ============================================================
 *  Default volume on init (0–30)
 * ============================================================ */
#define DFPLAYER_DEFAULT_VOLUME 20

/* ============================================================
 *  Audio File Number Mappings — USD Currency
 *
 *  DFPlayer CMD_PLAY_TRACK uses 1-based track numbers.
 *  Track N = file 000N.mp3 — perfect 1:1 mapping.
 *  35 files total.
 *
 *  Track  Filename   Word
 *  -----  --------   ----
 *    1    0001.mp3   One
 *    2    0002.mp3   Two
 *    3    0003.mp3   Three
 *    4    0004.mp3   Four
 *    5    0005.mp3   Five
 *    6    0006.mp3   Six
 *    7    0007.mp3   Seven
 *    8    0008.mp3   Eight
 *    9    0009.mp3   Nine
 *   10    0010.mp3   Ten
 *   11    0011.mp3   Eleven
 *   12    0012.mp3   Twelve
 *   13    0013.mp3   Thirteen
 *   14    0014.mp3   Fourteen
 *   15    0015.mp3   Fifteen
 *   16    0016.mp3   Sixteen
 *   17    0017.mp3   Seventeen
 *   18    0018.mp3   Eighteen
 *   19    0019.mp3   Nineteen
 *   20    0020.mp3   Twenty
 *   21    0021.mp3   Thirty
 *   22    0022.mp3   Forty
 *   23    0023.mp3   Fifty
 *   24    0024.mp3   Sixty
 *   25    0025.mp3   Seventy
 *   26    0026.mp3   Eighty
 *   27    0027.mp3   Ninety
 *   28    0028.mp3   Hundred
 *   29    0029.mp3   Thousand
 *   30    0030.mp3   Cent        (singular — exactly 1 cent)
 *   31    0031.mp3   Cents       (plural  — 2 or more cents)
 *   32    0032.mp3   Dollar      (singular — exactly 1 dollar)
 *   33    0033.mp3   Dollars     (plural  — 2 or more dollars)
 *   34    0034.mp3   "You Have Received A Payment Of"
 *   35    0035.mp3   Zero
 *
 *  Announcement rules (no "And" word):
 *   $1        → "...Payment Of" ONE DOLLAR
 *   $2.95     → "...Payment Of" TWO DOLLARS NINETY FIVE CENTS
 *   $95       → "...Payment Of" NINETY FIVE DOLLARS
 *   $10789.29 → "...Payment Of" TEN THOUSAND SEVEN HUNDRED EIGHTY NINE DOLLARS
 *                                TWENTY NINE CENTS
 *   $0.01     → "...Payment Of" ONE CENT
 *   $2.01     → "...Payment Of" TWO DOLLARS ONE CENT
 * ============================================================ */
#define AUDIO_FILE_ONE          1   /* 0001.mp3 */
#define AUDIO_FILE_TWO          2   /* 0002.mp3 */
#define AUDIO_FILE_THREE        3   /* 0003.mp3 */
#define AUDIO_FILE_FOUR         4   /* 0004.mp3 */
#define AUDIO_FILE_FIVE         5   /* 0005.mp3 */
#define AUDIO_FILE_SIX          6   /* 0006.mp3 */
#define AUDIO_FILE_SEVEN        7   /* 0007.mp3 */
#define AUDIO_FILE_EIGHT        8   /* 0008.mp3 */
#define AUDIO_FILE_NINE         9   /* 0009.mp3 */
#define AUDIO_FILE_TEN          10  /* 0010.mp3 */
#define AUDIO_FILE_ELEVEN       11  /* 0011.mp3 */
#define AUDIO_FILE_TWELVE       12  /* 0012.mp3 */
#define AUDIO_FILE_THIRTEEN     13  /* 0013.mp3 */
#define AUDIO_FILE_FOURTEEN     14  /* 0014.mp3 */
#define AUDIO_FILE_FIFTEEN      15  /* 0015.mp3 */
#define AUDIO_FILE_SIXTEEN      16  /* 0016.mp3 */
#define AUDIO_FILE_SEVENTEEN    17  /* 0017.mp3 */
#define AUDIO_FILE_EIGHTEEN     18  /* 0018.mp3 */
#define AUDIO_FILE_NINETEEN     19  /* 0019.mp3 */
#define AUDIO_FILE_TWENTY       20  /* 0020.mp3 */
#define AUDIO_FILE_THIRTY       21  /* 0021.mp3 */
#define AUDIO_FILE_FORTY        22  /* 0022.mp3 */
#define AUDIO_FILE_FIFTY        23  /* 0023.mp3 */
#define AUDIO_FILE_SIXTY        24  /* 0024.mp3 */
#define AUDIO_FILE_SEVENTY      25  /* 0025.mp3 */
#define AUDIO_FILE_EIGHTY       26  /* 0026.mp3 */
#define AUDIO_FILE_NINETY       27  /* 0027.mp3 */
#define AUDIO_FILE_HUNDRED      28  /* 0028.mp3 */
#define AUDIO_FILE_THOUSAND     29  /* 0029.mp3 */
#define AUDIO_FILE_CENT         30  /* 0030.mp3 — "Cent"    (singular: exactly 1 cent)  */
#define AUDIO_FILE_CENTS        31  /* 0031.mp3 — "Cents"   (plural:   2+ cents)        */
#define AUDIO_FILE_DOLLAR       32  /* 0032.mp3 — "Dollar"  (singular: exactly 1 dollar)*/
#define AUDIO_FILE_DOLLARS      33  /* 0033.mp3 — "Dollars" (plural:   2+ dollars)      */
#define AUDIO_FILE_PAYMENT_MSG  34  /* 0034.mp3 — "You Have Received A Payment Of"      */
#define AUDIO_FILE_ZERO         35  /* 0035.mp3 — "Zero"                                */

#define DFPLAYER_TOTAL_FILES    35

/* ============================================================
 *  Function Prototypes
 * ============================================================ */

/**
 * Optional poll hook — called every 10 ms inside dfplayer_wait_until_finished().
 * Implement this function in main.c (it is declared __weak in dfplayer.c so the
 * linker uses your version if provided, or a no-op if not).
 *
 * Use this to call process_volume_hold() so that hold-to-repeat volume works
 * during blocking announcement playback.
 *
 * IMPORTANT: Called from non-ISR context only. Safe to call dfplayer_volume_up()
 * / dfplayer_volume_down() from inside it.
 */
void dfplayer_poll_hook(void);

/** Initialise DFPlayer: boot wait → reset → TF source → volume → PAM8403 on. */
void dfplayer_init(void);

/** Play file by 1-based index (0001.mp3 = 1). */
void dfplayer_play_audio(uint8_t file_number);

/** Returns 1 if BUSY pin is LOW (playing), 0 if idle. */
uint8_t dfplayer_is_busy(void);

/** Block until current track finishes (polls BUSY every 50 ms). */
void dfplayer_wait_until_finished(void);

void dfplayer_volume_up(void);
void dfplayer_volume_down(void);
void dfplayer_set_volume(uint8_t volume);
uint8_t dfplayer_get_volume(void);

void dfplayer_play_previous(void);
void dfplayer_play_next(void);
void dfplayer_pause(void);
void dfplayer_resume(void);
void dfplayer_stop(void);

/**
 * Announce a full payment amount in USD via audio files.
 *
 * @param amount_cents  Amount in cents (100 cents = $1).
 *                      Pass dollars × 100 for whole-dollar amounts.
 *
 * Singular/plural rules:
 *   1 dollar  → DOLLAR (file 32)   |  2+ dollars → DOLLARS (file 33)
 *   1 cent    → CENT   (file 30)   |  2+ cents   → CENTS   (file 31)
 *   No "And" between dollars and cents.
 *
 * Examples:
 *   $1        → "...Of" ONE DOLLAR
 *   $2.95     → "...Of" TWO DOLLARS NINETY FIVE CENTS
 *   $95       → "...Of" NINETY FIVE DOLLARS
 *   $10789.29 → "...Of" TEN THOUSAND SEVEN HUNDRED EIGHTY NINE DOLLARS
 *                        TWENTY NINE CENTS
 *   $0.01     → "...Of" ONE CENT
 *   $2.01     → "...Of" TWO DOLLARS ONE CENT
 */
void dfplayer_announce_payment(uint32_t amount_cents);

/* PAM8403 amplifier control */
void pam8403_enable(void);
void pam8403_disable(void);
void pam8403_mute(void);
void pam8403_unmute(void);

#endif /* DFPLAYER_H */

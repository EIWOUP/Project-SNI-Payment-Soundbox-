// =============================================================================
// File: gsm.c
// GSM/SIM800L Driver — STM32F103C8T6 (Blue Pill)
//
// Pin Connections (from main.h / CubeMX):
//   SIM800L RST  => PA13  (SIM800L_RST_Pin  / SIM800L_RST_GPIO_Port)
//   SIM800L RING => PA11  (SIM800L_RING_Pin / SIM800L_RING_GPIO_Port)
//   SIM800L DTR  => PA12  (SIM800L_DTR_Pin  / SIM800L_DTR_GPIO_Port)
//   SIM800L TX   => PA10  (USART1 RX On MCU — huart1)
//   SIM800L RX   => PA9   (USART1 TX On MCU — huart1)
//
// UART: USART1 @ 9600 Baud, NVIC Global Interrupt Enabled.
//
// FIX APPLIED (Bug 1 — RING Blocking SMS):
//   Root Cause: SIM800L Sends "RING" URC Every ~4s For Incoming Calls.
//   The Old Code Called gsm_reject_call() (Sends "ATH\r\n") Unconditionally
//   Inside The UART ISR Whenever "RING" Was Seen. If A "RING" Line Arrived
//   Between A "+CMT:" Header And Its Body Line, The "OK"/"NO CARRIER"
//   Response From ATH Was Injected Into The Stream — The Parser Saw
//   "OK" As The SMS Body, parse_rupee_amount() Found No "Rs." In "OK",
//   And The Payment Was Silently Dropped.
//
//   Fix: Guard gsm_reject_call() With (!waiting_for_sms_body).
//   While We Are Between A +CMT Header And Its Body (waiting_for_sms_body==1),
//   We Must NOT Transmit Anything On USART1 — It Would Corrupt The Body Line.
//   AT+GSMBUSY=1 (Set In gsm_init) Already Rejects The Call At Hardware Level;
//   The ATH Is Only A Software Fallback And Is Safe To Skip During SMS Rx.
//
// NOTE: No printf() In This File. All Logging Is Done In main.c.
// =============================================================================

#include "gsm.h"        // Include GSM Driver Header For All Declarations
#include "usart.h"      // Include USART Header To Access huart1 Handle

#include <string.h>     // For strstr(), strcmp(), strncmp(), strchr(), strncpy(), memcpy()
#include <stdio.h>      // For snprintf()

// -----------------------------------------------------------------------------
// Internal State
// -----------------------------------------------------------------------------

/* Event Flags — Set In ISR Context, Cleared By Polling Functions */
static volatile uint8_t gsm_sms_sent_event = 0; // Flag: SMS Was Successfully Sent (+CMGS + OK)
static volatile uint8_t gsm_sms_received_event = 0; // Flag: A Valid SMS From Allowed Sender Arrived

/* RX Line Buffer — Filled One Byte At A Time By gsm_rx_byte_handler() */
static volatile char gsm_rx_line[256]; // Raw UART Receive Buffer, One Line At A Time
static volatile uint16_t gsm_rx_index = 0;    // Write Cursor Into gsm_rx_line[]

/* Raised By ISR Each Time A \n-Terminated Line Is Complete */
static volatile uint8_t gsm_line_ready = 0; // Set To 1 When A Full Line Is In gsm_rx_line[]

/* SMS Data Buffers */
static char sms_sender_number[20]; // Stores The Sender's Phone Number From +CMT Header
static char received_sms_message[200];               // Stores The SMS Body Text

/* Parser State */
static uint8_t waiting_for_sms_body = 0; // 1 = Next Non-Empty Line Is The SMS Body
static uint8_t waiting_for_cmgs_ok = 0; // 1 = Waiting For "OK" After "+CMGS:" Reference
static uint8_t sms_ready = 0;         // 1 = A New SMS Body Is Available To Read

/* RING Debounce */
static volatile uint8_t gsm_ring_event = 0; // Set By EXTI ISR When RING Pin Fires
static uint32_t gsm_ring_last_tick = 0; // Timestamp Of Last Accepted RING Event
#define RING_DEBOUNCE_MS  5000U                         // Minimum Gap (ms) Between Two Accepted RING Events

// -----------------------------------------------------------------------------
// Private: gsm_wait_for
// -----------------------------------------------------------------------------
/**
 * Spin Until A Received Line Contains `token`, Or timeout_ms Elapses.
 * Returns As Soon As The Token Is Found — Zero Extra Latency.
 */
static uint8_t gsm_wait_for(const char *token, uint32_t timeout_ms) {
	uint32_t start = HAL_GetTick(); // Record The Start Time For Timeout Tracking

	while ((HAL_GetTick() - start) < timeout_ms) { // Keep Looping Until Timeout

		if (!gsm_line_ready)     // If No New Line Has Arrived Yet, Keep Waiting
			continue;

		/* Atomically Snapshot The Volatile Buffer */
		char local[256];          // Local Copy Of The Received Line
		uint16_t len;                 // Number Of Bytes In The Line

		__disable_irq();    // Disable Interrupts To Read Volatile Buffer Safely
		len = gsm_rx_index;           // Read How Many Bytes Are In The Buffer
		if (len >= sizeof(local))
			len = sizeof(local) - 1;   // Clamp Length To Fit Local Buffer
		memcpy(local, (const char*) gsm_rx_line, len); // Copy Buffer Contents To Local Array
		__enable_irq();          // Re-Enable Interrupts After Safe Copy Is Done

		local[len] = '\0'; // Null-Terminate The Local Copy For String Functions
		gsm_line_ready = 0;      // Clear The Flag So ISR Can Fill The Next Line

		if (strstr(local, token) != NULL) // Check If The Expected Token Is Present In This Line
			return 1;                // Token Found — Return Success Immediately
	}
	return 0;                        // Timeout Expired Without Seeing The Token
}

// -----------------------------------------------------------------------------
// Private: gsm_hardware_reset
// -----------------------------------------------------------------------------
static void gsm_hardware_reset(void) {
	/* Pull RST LOW For 200 ms (Hardware Minimum), Then Release */
	HAL_GPIO_WritePin(SIM800L_RST_GPIO_Port, SIM800L_RST_Pin, GPIO_PIN_RESET); // Assert RST Low
	HAL_Delay(200);                   // Hold RST Low For 200ms Reset Pulse
	HAL_GPIO_WritePin(SIM800L_RST_GPIO_Port, SIM800L_RST_Pin, GPIO_PIN_SET); // Release RST High

	/* Wait For "SMS Ready" URC (Module Fully Booted).
	 * Exits Early The Moment It Arrives — 4s Is The Fallback Timeout. */
	if (!gsm_wait_for("SMS Ready", 4000)) { // Wait Up To 4 Seconds For Module Boot URC
		HAL_Delay(500);    // Some Firmware Variants Skip This URC — Wait Anyway
	}
}

// =============================================================================
// gsm_init
// =============================================================================
void gsm_init(void) {
	gsm_hardware_reset();     // Perform Hardware Reset And Wait For Module Boot

	/* Autobaud Sync — Up To 3 Attempts, Exits On First "OK" */
	for (uint8_t i = 0; i < 3; i++) {    // Retry AT Up To 3 Times For Baud Sync
		gsm_send("AT\r\n");         // Send Basic AT Command To Probe The Module
		if (gsm_wait_for("OK", 300))
			break;      // Exit Loop On First Successful OK Response
	}

	/* Disable Echo — Send Twice To Handle The Echo Of The First ATE0 Itself */
	gsm_send("ATE0\r\n"); // Disable Command Echo (First Send — May Echo Itself)
	gsm_wait_for("OK", 300);          // Wait For OK Confirmation
	gsm_send("ATE0\r\n"); // Send Again Now That Echo Is Off — Confirm ATE0 Accepted
	gsm_wait_for("OK", 300);          // Wait For OK Confirmation

	/* SMS Text Mode */
	gsm_send("AT+CMGF=1\r\n");        // Set SMS Format To Text Mode (Not PDU)
	gsm_wait_for("OK", 300);          // Wait For OK Confirmation

	/* GSM Character Set */
	gsm_send("AT+CSCS=\"GSM\"\r\n"); // Set Character Encoding To GSM 7-Bit Alphabet
	gsm_wait_for("OK", 300);          // Wait For OK Confirmation

	/* Route New SMS Directly To TE As +CMT Unsolicited Result Code */
	gsm_send("AT+CNMI=2,2,0,0,0\r\n"); // Configure New SMS Notification — Deliver +CMT Directly
	gsm_wait_for("OK", 300);              // Wait For OK Confirmation

	/* Auto-Reject Incoming Voice Calls With Busy Tone (Hardware Path) */
	gsm_send("AT+GSMBUSY=1\r\n"); // Enable Hardware Busy Tone — Rejects Calls Before RING URC
	gsm_wait_for("OK", 300);          // Wait For OK Confirmation
}

// =============================================================================
// gsm_send
// =============================================================================
void gsm_send(const char *cmd) {
	/* Transmit The Entire Command String Over USART1 In Blocking Mode */
	HAL_UART_Transmit(&huart1, (uint8_t*) cmd, (uint16_t) strlen(cmd),
	HAL_MAX_DELAY);
}

// =============================================================================
// gsm_send_sms
// =============================================================================
void gsm_send_sms(const char *number, const char *message) {
	char cmd[64];   // Temporary Buffer To Build The AT+CMGS Command String

	/* Open Send Command And Wait For ">" Prompt */
	snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"\r\n", number); // Format: AT+CMGS="<number>"
	gsm_send(cmd);                    // Send The AT+CMGS Command To SIM800L

	if (!gsm_wait_for(">", 5000)) { // Wait Up To 5 Seconds For The ">" Input Prompt
		/* Prompt Never Arrived — Abort With ESC */
		uint8_t esc = 0x1B; // ESC Character (ASCII 27) Cancels The CMGS Operation
		HAL_UART_Transmit(&huart1, &esc, 1, HAL_MAX_DELAY); // Send ESC To Abort SMS Entry
		return;                   // Return Without Sending — No SMS Transmitted
	}

	/* Send Body Immediately (Prompt Already Received) */
	gsm_send(message);                // Transmit The SMS Body Text Over USART1

	/* CTRL+Z Triggers Actual SMS Transmission */
	uint8_t ctrl_z = 0x1A; // CTRL+Z (ASCII 26) Signals End Of SMS Body To SIM800L
	HAL_UART_Transmit(&huart1, &ctrl_z, 1, HAL_MAX_DELAY); // Send CTRL+Z To Finalize SMS

	/* Confirmation (+CMGS: <ref> Then OK) Arrives Via ISR.
	 * Poll gsm_sms_sent_occurred() In The Main Loop. */
}

// =============================================================================
// gsm_reject_call
// =============================================================================
void gsm_reject_call(void) {
	gsm_send("ATH\r\n");   // Send ATH (Hang Up) Command — Fire And Forget
}

// =============================================================================
// gsm_rx_byte_handler — Called From USART1 RX ISR, One Byte At A Time
// =============================================================================
void gsm_rx_byte_handler(uint8_t c) {
	/* Discard Carriage Return (\r) — SIM800L Uses \r\n Line Endings */
	if (c == '\r')
		return;   // CR Has No Meaning On Its Own — Discard It

	/* Special Case: ">" Prompt Has No Trailing \n.
	 * Detect It Immediately When It Arrives As The First Character.
	 * Synthesise A Single-Character Line So gsm_wait_for(">") Sees It. */
	if (c == '>' && gsm_rx_index == 0) { // ">" Must Arrive At Start Of A New Line
		gsm_rx_line[0] = '>';              // Place ">" Into The Receive Buffer
		gsm_rx_line[1] = '\0';            // Null-Terminate The Synthetic Line
		gsm_rx_index = 1;               // Mark 1 Byte As Received
		gsm_line_ready = 1;              // Signal That A Complete Line Is Ready
		return;              // Done — Don't Fall Through To Normal Accumulation
	}

	/* Accumulate Normal Characters Until \n */
	if (c != '\n') {                  // Not A Line-End — Accumulate Into Buffer
		if (gsm_rx_index < (uint16_t) (sizeof(gsm_rx_line) - 1)) { // Check Buffer Has Space
			gsm_rx_line[gsm_rx_index++] = (char) c; // Store Byte And Advance The Write Cursor
		} else {
			gsm_rx_index = 0;  // Buffer Overflow — Discard Everything And Reset
		}
		return;          // Not End Of Line Yet — Return And Wait For More Bytes
	}

	/* \n Received — Line Is Complete */
	gsm_rx_line[gsm_rx_index] = '\0';     // Null-Terminate The Completed Line

	if (gsm_rx_index == 0) {      // Empty Line (Just \r\n With Nothing Between)
		return;                  // Skip Empty Lines — They Carry No Useful Data
	}

	gsm_line_ready = 1;        // Signal gsm_wait_for() That A New Line Is Ready

	/* ------------------------------------------------------------------
	 * Async Event Parsing — Runs At All Times After Init
	 * ------------------------------------------------------------------ */

	/* +CMGS: <ref> — Send Reference Received; Arm OK Watcher */
	if (strncmp((const char*) gsm_rx_line, "+CMGS:", 6) == 0) { // Check For SMS Send Reference
		waiting_for_cmgs_ok = 1;     // Next "OK" Line Confirms The SMS Was Sent
	} else if (waiting_for_cmgs_ok
			&& strcmp((const char*) gsm_rx_line, "OK") == 0) { // Check For "OK" While Waiting For CMGS Confirm
		gsm_sms_sent_event = 1;   // Raise The SMS-Sent Event Flag For Main Loop
		waiting_for_cmgs_ok = 0;          // Clear The CMGS-OK Wait Flag
	}

	/* ERROR — Clear Pending State So The Parser Does Not Get Stuck */
	if (strcmp((const char*) gsm_rx_line, "ERROR") == 0) { // Check For "ERROR" Response
		waiting_for_sms_body = 0;         // Cancel Any Pending SMS Body Wait
		waiting_for_cmgs_ok = 0;         // Cancel Any Pending CMGS OK Wait
	}

	/* RING — Incoming Voice Call URC (~4s Per Cycle).
	 *
	 * FIX (Bug 1): The Old Code Called gsm_reject_call() Unconditionally Here.
	 * Problem: If "RING" Arrives While We Are Between The +CMT Header And
	 * Its Body (waiting_for_sms_body == 1), Calling gsm_reject_call() Sends
	 * "ATH\r\n" Over USART1. SIM800L Responds With "OK" Or "NO CARRIER".
	 * That Response Line Then Arrives As The "SMS Body" — The Parser Stores
	 * "OK" In received_sms_message, parse_rupee_amount() Finds No "Rs." In
	 * "OK", And The Payment Notification Is Silently Dropped.
	 *
	 * Fix: Only Call gsm_reject_call() When We Are NOT Mid-SMS Reception.
	 * AT+GSMBUSY=1 (Configured In gsm_init) Already Rejects The Call At
	 * Hardware Level Before The RING URC Even Reaches Us In Most Cases.
	 * The ATH Is A Software Fallback — Safe To Skip During SMS Body Wait.
	 */
	if (strcmp((const char*) gsm_rx_line, "RING") == 0) { // Check For Incoming Call URC
		if (!waiting_for_sms_body) { // ONLY Reject If We Are NOT Mid-SMS-Reception
			gsm_reject_call(); // Send ATH — Safe Here, No SMS Body Expected Right Now
		}
		/* If waiting_for_sms_body == 1: Skip ATH Entirely.
		 * AT+GSMBUSY=1 Has Already Rejected The Call At The Hardware Level.
		 * Sending ATH Now Would Inject "OK"/"NO CARRIER" Into The Stream
		 * And Corrupt The SMS Body That Is About To Arrive On The Next Line. */
	}

	/* +CMT: "<number>","","timestamp" — Incoming SMS Header */
	if (strncmp((const char*) gsm_rx_line, "+CMT:", 5) == 0) { // Check For New SMS Arrival Header
		waiting_for_sms_body = 0; // Reset In Case A Previous Body Never Arrived

		/* Extract Sender Number From Between The First Pair Of Quotes */
		const char *p1 = strchr((const char*) gsm_rx_line, '"'); // Find Opening Quote
		if (p1) {
			const char *p2 = strchr(p1 + 1, '"');          // Find Closing Quote
			if (p2) {
				uint16_t len = (uint16_t) (p2 - p1 - 1); // Length Of Number String Between Quotes
				if (len >= (uint16_t) sizeof(sms_sender_number))
					len = (uint16_t) (sizeof(sms_sender_number) - 1); // Clamp To Buffer Size
				strncpy(sms_sender_number, p1 + 1, len); // Copy Number Into Storage Buffer
				sms_sender_number[len] = '\0'; // Null-Terminate The Number String
			}
		}
		waiting_for_sms_body = 1; // Set Flag — Next Non-Empty Line Is The SMS Body Text
	}
	/* SMS Body — The Line Immediately After The +CMT: Header */
	else if (waiting_for_sms_body) { // This Line Is The SMS Body We've Been Waiting For
		strncpy(received_sms_message, (const char*) gsm_rx_line,
				sizeof(received_sms_message) - 1); // Copy SMS Body Into Storage Buffer
		received_sms_message[sizeof(received_sms_message) - 1] = '\0'; // Ensure Null-Termination

		waiting_for_sms_body = 0;      // Body Has Arrived — Clear The Wait Flag

		/* Only Process SMS From The Whitelisted Sender */
		if (strcmp(sms_sender_number, GSM_ALLOWED_SENDER) == 0) { // Verify Sender Is Authorised
			sms_ready = 1;    // Mark SMS As Available For Main Loop To Read
			gsm_sms_received_event = 1;   // Raise The SMS-Received Event Flag
		}
	}

	gsm_rx_index = 0; // Reset Write Cursor So Next Line Starts From The Beginning Of The Buffer
}

// =============================================================================
// Event Polling (Non-Blocking — Main Loop Only)
// =============================================================================

uint8_t gsm_sms_sent_occurred(void) {
	if (gsm_sms_sent_event) {     // Check If The SMS-Sent Event Has Been Raised
		gsm_sms_sent_event = 0;  // Clear The Flag Before Returning (Auto-Reset)
		return 1;                         // Return 1 — SMS Was Confirmed Sent
	}
	return 0;                       // Return 0 — No SMS-Sent Event Has Occurred
}

uint8_t gsm_sms_received_occurred(void) {
	if (gsm_sms_received_event) { // Check If The SMS-Received Event Has Been Raised
		gsm_sms_received_event = 0; // Clear The Flag Before Returning (Auto-Reset)
		return 1;                      // Return 1 — A New SMS Has Been Received
	}
	return 0;                             // Return 0 — No New SMS Has Arrived
}

// =============================================================================
// SMS Data Accessors
// =============================================================================

uint8_t gsm_sms_available(void) {
	return sms_ready;
}                         // Return 1 If New SMS Body Is Ready
const char* gsm_get_sender(void) {
	return sms_sender_number;
}                  // Return Pointer To Sender Number Buffer
const char* gsm_get_sms(void) {
	sms_ready = 0;
	return received_sms_message;
} // Return SMS Body And Mark It Consumed

// =============================================================================
// RING — EXTI-Driven, Non-Blocking Debounce
// =============================================================================

void gsm_ring_isr_notify(void) {
	gsm_ring_event = 1;
}   // Called From EXTI ISR — Just Set The Flag, Do Nothing Else

uint8_t gsm_ring_poll(void) {
	if (!gsm_ring_event)
		return 0;       // No RING Event Pending — Return Immediately
	gsm_ring_event = 0;                  // Clear The Event Flag
	uint32_t now = HAL_GetTick();        // Get Current Timestamp
	if ((now - gsm_ring_last_tick) >= RING_DEBOUNCE_MS) { // Check If Outside The Debounce Window
		gsm_ring_last_tick = now;     // Update The Last Accepted RING Timestamp
		return 1;                        // Return 1 — Valid RING Event Accepted
	}
	return 0;                 // Return 0 — Still Inside Debounce Window, Ignore
}

uint8_t gsm_ring_active(void) {
	/* Direct RING Pin Read — Low = Active Ring Signal */
	return (HAL_GPIO_ReadPin(SIM800L_RING_GPIO_Port, SIM800L_RING_Pin)
			== GPIO_PIN_RESET);
}

// =============================================================================
// DTR
// =============================================================================

void gsm_dtr_wake(void) {
	/* Pull DTR LOW To Wake SIM800L From Sleep Mode */
	HAL_GPIO_WritePin(SIM800L_DTR_GPIO_Port, SIM800L_DTR_Pin, GPIO_PIN_RESET); // DTR Low = Wake
}

void gsm_dtr_sleep(void) {
	/* Release DTR HIGH To Allow SIM800L To Enter Sleep Mode */
	HAL_GPIO_WritePin(SIM800L_DTR_GPIO_Port, SIM800L_DTR_Pin, GPIO_PIN_SET); // DTR High = Sleep
}

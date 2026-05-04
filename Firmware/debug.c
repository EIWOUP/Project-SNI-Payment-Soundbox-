// =============================================================================
// File: debug.c
// Description: printf() redirect to TeraTerm via USART3 on STM32F103C8T6
//
// UART assignment:
//   USART2 (PA2/PA3)   — DFPlayer Mini
//   USART3 (PB10/PB11) — Debug console → TeraTerm  (PB10=TX, PB11=RX)
//
// TeraTerm settings to match:
//   Baud: 9600 | Data: 8-bit | Parity: None | Stop: 1 | Flow: None
//   Receive: AUTO  |  Transmit newline: CR+LF
// =============================================================================

#include "debug.h"                  // Include our own debug header
#include "usart.h"                  // CubeMX-generated; provides extern UART_HandleTypeDef huart3
#include <string.h>                 // For strlen()

// =============================================================================
// _write  — Newlib Syscall Hook
// =============================================================================
/**
 * The GNU ARM C runtime calls _write() whenever your code calls printf(),
 * puts(), putchar(), etc. By implementing it here and pointing it at USART3
 * we transparently redirect all standard-output to the TeraTerm terminal.
 *
 * @param file  Ignored (we always write to USART3 regardless of fd).
 * @param ptr   Pointer to the data buffer to transmit.
 * @param len   Number of bytes to transmit.
 * @return      Number of bytes written (always == len on success).
 */
int _write(int file, char *ptr, int len) {
	(void) file; // Suppress unused-parameter warning — we don't use the file descriptor

	// Transmit 'len' bytes from 'ptr' over USART3 (debug UART to TeraTerm), blocking until done
	HAL_UART_Transmit(&huart3, (uint8_t*) ptr, (uint16_t) len, HAL_MAX_DELAY);

	return len;       // Return the number of bytes written (required by newlib)
}

// =============================================================================
// debug_print  — Lightweight Raw-String Transmit
// =============================================================================
/**
 * Sends a pre-built string directly over USART3 without printf overhead.
 * Handy when you want a quick log line without format arguments.
 */
void debug_print(const char *msg) {
	if (msg == NULL) // Guard against NULL pointer — do nothing if message is NULL
		return;

	// Transmit the entire string (strlen bytes) over USART3, blocking until done
	HAL_UART_Transmit(&huart3, (uint8_t*) msg, (uint16_t) strlen(msg),
			HAL_MAX_DELAY);
}

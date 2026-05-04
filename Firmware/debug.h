// =============================================================================
// File: debug.h
// Description: Redirect printf() to TeraTerm via USART3 (PB10=TX, PB11=RX).
//
// USART2 (PA2/PA3) is used for DFPlayer Mini communication.
// Debug output uses USART3 instead.
//
// CubeMX setup for debug UART (USART3):
//   - Mode: Asynchronous
//   - Baud: 9600  (match this in TeraTerm — must be 9600, NOT 115200)
//   - Word length: 8 bits, no parity, 1 stop bit
//   - No DMA, no NVIC interrupt needed (TX-only, polling)
//   - Pins: PB10 = USART3_TX, PB11 = USART3_RX  (RX optional for debug)
// =============================================================================

#ifndef DEBUG_H                     // Include guard — prevents multiple inclusions of this header
#define DEBUG_H

#include "stm32f1xx_hal.h"          // Include STM32 HAL for UART handle types

/**
 * @brief  Newlib _write() syscall override.
 *         Routes all printf / puts output through USART3 → TeraTerm.
 *         This function is called automatically by the C runtime — you do
 *         NOT need to call it directly. Just use printf() as normal.
 */
int _write(int file, char *ptr, int len); // Newlib syscall hook — redirects printf to USART3

/**
 * @brief  Transmit a raw string over the debug UART (USART3) without
 *         going through printf formatting. Useful for ISR-safe logging
 *         where you already have a pre-built string.
 * @param  msg  Null-terminated string
 */
void debug_print(const char *msg); // Lightweight string transmit over USART3 without printf

#endif /* DEBUG_H */

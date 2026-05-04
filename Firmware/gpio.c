/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file    gpio.c
 * @brief   GPIO configuration for UPI Payment Announcer.
 *          STM32F103C8T6 (Blue Pill)
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "gpio.h"

/* USER CODE BEGIN 0 */
/* USER CODE END 0 */

/*
 * EXTI LINE ASSIGNMENT — STM32F103C8T6
 * ======================================
 * On this device each EXTI line N can be connected to pin N of ANY port,
 * but only ONE port at a time. Conflicts are resolved below.
 *
 *  Line  Winner          Loser (reason)
 *  ----  ------          ------
 *  4     PB4 VOL_DOWN    PA4 DFPLAYER_BUSY — BUSY is polled instead (10 ms)
 *
 * Final EXTI assignment:
 *  EXTI3       PB3  VOL_UP_Pin       falling, pull-up → EXTI3_IRQn      pri 1
 *  EXTI4       PB4  VOL_DOWN_Pin     falling, pull-up → EXTI4_IRQn      pri 1
 *  EXTI5       PB5  PLAY_PREV_Pin    falling, pull-up → EXTI9_5_IRQn    pri 1
 *  EXTI11      PA11 SIM800L_RING_Pin falling, pull-up → EXTI15_10_IRQn  pri 1
 *
 * NVIC PRIORITY RULES (why these values):
 *  Priority 0 — SysTick (set by HAL_Init, not here)
 *               USART1  (set in usart.c MspInit)
 *    SysTick must be the absolute highest so HAL_GetTick() is always live.
 *    USART1 shares priority 0 so GSM bytes are never dropped.
 *
 *  Priority 1 — All EXTI lines (buttons + RING)
 *    EXTI callbacks only set flags — no HAL_Delay(), no UART TX.
 *    Running at priority 1 means SysTick (priority 0) always pre-empts them,
 *    so HAL_Delay() called from non-ISR context never hangs even if an EXTI
 *    fires at the same instant.
 *
 *  CRITICAL: Never set any EXTI to priority 0. If an EXTI shares priority 0
 *  with SysTick and the EXTI callback calls HAL_Delay(), SysTick cannot
 *  pre-empt the EXTI — HAL_Delay() spins forever and the chip hangs.
 */

void MX_GPIO_Init(void) {

	GPIO_InitTypeDef GPIO_InitStruct = { 0 };

	/* GPIO Ports Clock Enable */
	__HAL_RCC_GPIOC_CLK_ENABLE();
	__HAL_RCC_GPIOD_CLK_ENABLE();
	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();

	/* Output initial levels ------------------------------------------------ */
	HAL_GPIO_WritePin(USER_LED_GPIO_Port, USER_LED_Pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(GPIOA, VBAT_MOSFET_Pin | SIM800L_DTR_Pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(PAM8403_SHDWN_GPIO_Port, PAM8403_SHDWN_Pin, GPIO_PIN_SET);
	HAL_GPIO_WritePin(GPIOA, PAM8403_MUTE_Pin | SIM800L_RST_Pin, GPIO_PIN_SET);

	/* USER_LED — PC13, push-pull output ------------------------------------ */
	GPIO_InitStruct.Pin = USER_LED_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_PULLDOWN;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(USER_LED_GPIO_Port, &GPIO_InitStruct);

	/* Unused analog — PA0, PA1, PA5, PA14, PA15 --------------------------- */
	GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_5 | GPIO_PIN_14
			| GPIO_PIN_15;
	GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
	HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

	/* DFPLAYER_BUSY — PA4, plain polled input (NOT EXTI)               ----
	 * EXTI line 4 is taken by VOL_DOWN (PB4). BUSY is read by
	 * dfplayer_is_busy() which is called every 10 ms inside
	 * dfplayer_wait_until_finished(). No interrupt needed.               */
	GPIO_InitStruct.Pin = DFPLAYER_BUSY_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull = GPIO_PULLUP;
	HAL_GPIO_Init(DFPLAYER_BUSY_GPIO_Port, &GPIO_InitStruct);

	/* VBAT_MOSFET — PA6, push-pull output --------------------------------- */
	GPIO_InitStruct.Pin = VBAT_MOSFET_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_PULLDOWN;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(VBAT_MOSFET_GPIO_Port, &GPIO_InitStruct);

	/* PAM8403_SHDWN — PB15, push-pull output ------------------------------ */
	GPIO_InitStruct.Pin = PAM8403_SHDWN_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(PAM8403_SHDWN_GPIO_Port, &GPIO_InitStruct);

	/* PAM8403_MUTE, SIM800L_DTR, SIM800L_RST — push-pull outputs ---------- */
	GPIO_InitStruct.Pin = PAM8403_MUTE_Pin | SIM800L_DTR_Pin | SIM800L_RST_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

	/* SIM800L_RING — PA11, EXTI falling edge, pull-up --------------------- */
	GPIO_InitStruct.Pin = SIM800L_RING_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
	GPIO_InitStruct.Pull = GPIO_PULLUP;
	HAL_GPIO_Init(SIM800L_RING_GPIO_Port, &GPIO_InitStruct);

	/* Unused analog — PB0..PB2, PB6..PB9, PB12..PB14 --------------------- */
	GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_6
			| GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_12 | GPIO_PIN_13
			| GPIO_PIN_14;
	GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
	HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

	/* Buttons — PB3 / PB4 / PB5, EXTI falling edge, pull-up -------------- */
	GPIO_InitStruct.Pin = VOL_UP_Pin | VOL_DOWN_Pin | PLAY_PREV_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
	GPIO_InitStruct.Pull = GPIO_PULLUP;
	HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

	/* NVIC — EXTI lines --------------------------------------------------- */

	/* VOL_UP PB3 — EXTI3, priority 1 */
	HAL_NVIC_SetPriority(EXTI3_IRQn, 1, 0);
	HAL_NVIC_EnableIRQ(EXTI3_IRQn);

	/* VOL_DOWN PB4 — EXTI4, priority 1 */
	HAL_NVIC_SetPriority(EXTI4_IRQn, 1, 0);
	HAL_NVIC_EnableIRQ(EXTI4_IRQn);

	/* PLAY_PREV PB5 — EXTI9_5, priority 1 */
	HAL_NVIC_SetPriority(EXTI9_5_IRQn, 1, 0);
	HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

	/* SIM800L_RING PA11 — EXTI15_10, priority 1 */
	HAL_NVIC_SetPriority(EXTI15_10_IRQn, 1, 0);
	HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

	/*
	 * USART1 priority is set in HAL_UART_MspInit() inside usart.c.
	 * Verify it is 0 there — if it was left at 1 it needs to be 0 to
	 * match the priority map described at the top of this file.
	 */
}

/* USER CODE BEGIN 2 */
/* USER CODE END 2 */

/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file    stm32f1xx_it.c
 * @brief   Interrupt Service Routines.
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
#include "main.h"
#include "stm32f1xx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */
/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
extern UART_HandleTypeDef huart1;
/* USER CODE BEGIN EV */
/* USER CODE END EV */

/*
 * INTERRUPT PRIORITY MAP FOR THIS PROJECT
 * ----------------------------------------
 * Priority 0 (highest) — SysTick_Handler
 *   Set by HAL_Init() via HAL_NVIC_SetPriority(SysTick_IRQn, 0, 0).
 *   MUST be the highest priority in the system so that HAL_GetTick() always
 *   increments and HAL_Delay() never hangs, even when called from within
 *   any lower-priority ISR. Do not raise any other IRQ to priority 0.
 *
 * Priority 0 (highest) — USART1_IRQHandler
 *   GSM byte reception. Must not be delayed by button presses. Sharing
 *   priority 0 with SysTick is fine — they will not pre-empt each other
 *   but neither will stall for long (UART handler is a few µs max).
 *
 * Priority 1 — EXTI3_IRQHandler   (VOL_UP_Pin  PB3)
 * Priority 1 — EXTI4_IRQHandler   (VOL_DOWN_Pin PB4)
 * Priority 1 — EXTI9_5_IRQHandler (PLAY_PREV_Pin PB5)
 * Priority 1 — EXTI15_10_IRQHandler (SIM800L_RING_Pin PA11)
 *   All EXTI handlers set flags only — no HAL_Delay(), no UART TX.
 *   They complete in < 1 µs so there is no risk of starving higher-
 *   priority handlers even if multiple fire simultaneously.
 *
 * WHY THIS MATTERS FOR BUTTONS:
 *   The previous code called HAL_Delay(50) inside HAL_GPIO_EXTI_Callback().
 *   HAL_Delay() spins waiting for HAL_GetTick() to advance. HAL_GetTick()
 *   advances only when SysTick_Handler fires. SysTick_Handler cannot pre-empt
 *   an ISR of equal or lower priority. If SysTick and EXTI share priority 1,
 *   SysTick can NEVER fire while we are inside the EXTI callback — HAL_Delay()
 *   spins forever and the chip hangs. Fix: EXTI at priority 1, SysTick at
 *   priority 0; and never call HAL_Delay() from any ISR.
 */

/******************************************************************************/
/*           Cortex-M3 Processor Interruption and Exception Handlers          */
/******************************************************************************/

void NMI_Handler(void) {
	/* USER CODE BEGIN NonMaskableInt_IRQn 0 */
	/* USER CODE END NonMaskableInt_IRQn 0 */
	/* USER CODE BEGIN NonMaskableInt_IRQn 1 */
	while (1) {
	}
	/* USER CODE END NonMaskableInt_IRQn 1 */
}

void HardFault_Handler(void) {
	/* USER CODE BEGIN HardFault_IRQn 0 */
	/* USER CODE END HardFault_IRQn 0 */
	while (1) {
		/* USER CODE BEGIN W1_HardFault_IRQn 0 */
		/* USER CODE END W1_HardFault_IRQn 0 */
	}
}

void MemManage_Handler(void) {
	/* USER CODE BEGIN MemoryManagement_IRQn 0 */
	/* USER CODE END MemoryManagement_IRQn 0 */
	while (1) {
		/* USER CODE BEGIN W1_MemoryManagement_IRQn 0 */
		/* USER CODE END W1_MemoryManagement_IRQn 0 */
	}
}

void BusFault_Handler(void) {
	/* USER CODE BEGIN BusFault_IRQn 0 */
	/* USER CODE END BusFault_IRQn 0 */
	while (1) {
		/* USER CODE BEGIN W1_BusFault_IRQn 0 */
		/* USER CODE END W1_BusFault_IRQn 0 */
	}
}

void UsageFault_Handler(void) {
	/* USER CODE BEGIN UsageFault_IRQn 0 */
	/* USER CODE END UsageFault_IRQn 0 */
	while (1) {
		/* USER CODE BEGIN W1_UsageFault_IRQn 0 */
		/* USER CODE END W1_UsageFault_IRQn 0 */
	}
}

void SVC_Handler(void) {
	/* USER CODE BEGIN SVCall_IRQn 0 */
	/* USER CODE END SVCall_IRQn 0 */
	/* USER CODE BEGIN SVCall_IRQn 1 */
	/* USER CODE END SVCall_IRQn 1 */
}

void DebugMon_Handler(void) {
	/* USER CODE BEGIN DebugMonitor_IRQn 0 */
	/* USER CODE END DebugMonitor_IRQn 0 */
	/* USER CODE BEGIN DebugMonitor_IRQn 1 */
	/* USER CODE END DebugMonitor_IRQn 1 */
}

void PendSV_Handler(void) {
	/* USER CODE BEGIN PendSV_IRQn 0 */
	/* USER CODE END PendSV_IRQn 0 */
	/* USER CODE BEGIN PendSV_IRQn 1 */
	/* USER CODE END PendSV_IRQn 1 */
}

/**
 * @brief SysTick — increments HAL tick counter every 1 ms.
 *        HAL_Init() sets this to priority 0 (highest). Do not change.
 *        This must pre-empt all EXTI handlers (priority 1) so that
 *        HAL_GetTick() is always live and HAL_Delay() never hangs.
 */
void SysTick_Handler(void) {
	/* USER CODE BEGIN SysTick_IRQn 0 */
	/* USER CODE END SysTick_IRQn 0 */
	HAL_IncTick();
	/* USER CODE BEGIN SysTick_IRQn 1 */
	/* USER CODE END SysTick_IRQn 1 */
}

/******************************************************************************/
/* STM32F1xx Peripheral Interrupt Handlers                                    */
/******************************************************************************/

/**
 * @brief EXTI line 3 — VOL_UP_Pin (PB3), falling edge, active LOW button.
 *        Priority 1 (set in gpio.c). Calls HAL which clears the pending bit
 *        and invokes HAL_GPIO_EXTI_Callback(VOL_UP_Pin).
 *        Added: was not present before (PB3 was a polled input).
 */
void EXTI3_IRQHandler(void) {
	/* USER CODE BEGIN EXTI3_IRQn 0 */
	/* USER CODE END EXTI3_IRQn 0 */
	HAL_GPIO_EXTI_IRQHandler(VOL_UP_Pin);
	/* USER CODE BEGIN EXTI3_IRQn 1 */
	/* USER CODE END EXTI3_IRQn 1 */
}

/**
 * @brief EXTI line 4 — VOL_DOWN_Pin (PB4), falling edge, active LOW button.
 *        Priority 1 (set in gpio.c).
 *        Changed: was HAL_GPIO_EXTI_IRQHandler(DFPLAYER_BUSY_Pin).
 *        DFPLAYER_BUSY (PA4) is on EXTI line 4. VOL_DOWN (PB4) is also on
 *        EXTI line 4. Only one source per line is allowed. VOL_DOWN wins;
 *        DFPLAYER_BUSY is handled by polling in dfplayer_wait_until_finished().
 */
void EXTI4_IRQHandler(void) {
	/* USER CODE BEGIN EXTI4_IRQn 0 */
	/* USER CODE END EXTI4_IRQn 0 */
	HAL_GPIO_EXTI_IRQHandler(VOL_DOWN_Pin); /* was DFPLAYER_BUSY_Pin */
	/* USER CODE BEGIN EXTI4_IRQn 1 */
	/* USER CODE END EXTI4_IRQn 1 */
}

/**
 * @brief EXTI lines 5–9 — PLAY_PREV_Pin (PB5), falling edge, active LOW button.
 *        Priority 1 (set in gpio.c). Vector is shared for pins 5..9 but only
 *        PB5 is configured as EXTI in gpio.c, so only PLAY_PREV fires this.
 *        Added: was not present before (PB5 was a polled input).
 */
void EXTI9_5_IRQHandler(void) {
	/* USER CODE BEGIN EXTI9_5_IRQn 0 */
	/* USER CODE END EXTI9_5_IRQn 0 */
	HAL_GPIO_EXTI_IRQHandler(PLAY_PREV_Pin);
	/* USER CODE BEGIN EXTI9_5_IRQn 1 */
	/* USER CODE END EXTI9_5_IRQn 1 */
}

/**
 * @brief USART1 global interrupt — SIM800L RX/TX on huart1.
 *        Priority 0 — same as SysTick, must not be starved by button presses.
 *        Unchanged from original.
 */
void USART1_IRQHandler(void) {
	/* USER CODE BEGIN USART1_IRQn 0 */
	/* USER CODE END USART1_IRQn 0 */
	HAL_UART_IRQHandler(&huart1);
	/* USER CODE BEGIN USART1_IRQn 1 */
	/* USER CODE END USART1_IRQn 1 */
}

/**
 * @brief EXTI lines 10–15 — SIM800L_RING_Pin (PA11), falling edge.
 *        Priority 1. Unchanged from original.
 */
void EXTI15_10_IRQHandler(void) {
	/* USER CODE BEGIN EXTI15_10_IRQn 0 */
	/* USER CODE END EXTI15_10_IRQn 0 */
	HAL_GPIO_EXTI_IRQHandler(SIM800L_RING_Pin);
	/* USER CODE BEGIN EXTI15_10_IRQn 1 */
	/* USER CODE END EXTI15_10_IRQn 1 */
}

/* USER CODE BEGIN 1 */
/* USER CODE END 1 */

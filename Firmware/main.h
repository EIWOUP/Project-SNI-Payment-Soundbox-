/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f1xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define USER_LED_Pin GPIO_PIN_13
#define USER_LED_GPIO_Port GPIOC
#define DFPLAYER_TX_Pin GPIO_PIN_2
#define DFPLAYER_TX_GPIO_Port GPIOA
#define DFPLAYER_RX_Pin GPIO_PIN_3
#define DFPLAYER_RX_GPIO_Port GPIOA
#define DFPLAYER_BUSY_Pin GPIO_PIN_4
#define DFPLAYER_BUSY_GPIO_Port GPIOA
#define DFPLAYER_BUSY_EXTI_IRQn EXTI4_IRQn
#define VBAT_MOSFET_Pin GPIO_PIN_6
#define VBAT_MOSFET_GPIO_Port GPIOA
#define VBAT_READ_Pin GPIO_PIN_7
#define VBAT_READ_GPIO_Port GPIOA
#define DEBUG_TX_Pin GPIO_PIN_10
#define DEBUG_TX_GPIO_Port GPIOB
#define DEBUG_RX_Pin GPIO_PIN_11
#define DEBUG_RX_GPIO_Port GPIOB
#define PAM8403_SHDWN_Pin GPIO_PIN_15
#define PAM8403_SHDWN_GPIO_Port GPIOB
#define PAM8403_MUTE_Pin GPIO_PIN_8
#define PAM8403_MUTE_GPIO_Port GPIOA
#define SIM800L_TX_Pin GPIO_PIN_9
#define SIM800L_TX_GPIO_Port GPIOA
#define SIM800L_RX_Pin GPIO_PIN_10
#define SIM800L_RX_GPIO_Port GPIOA
#define SIM800L_RING_Pin GPIO_PIN_11
#define SIM800L_RING_GPIO_Port GPIOA
#define SIM800L_RING_EXTI_IRQn EXTI15_10_IRQn
#define SIM800L_DTR_Pin GPIO_PIN_12
#define SIM800L_DTR_GPIO_Port GPIOA
#define SIM800L_RST_Pin GPIO_PIN_13
#define SIM800L_RST_GPIO_Port GPIOA
#define VOL_UP_Pin GPIO_PIN_3
#define VOL_UP_GPIO_Port GPIOB
#define VOL_DOWN_Pin GPIO_PIN_4
#define VOL_DOWN_GPIO_Port GPIOB
#define PLAY_PREV_Pin GPIO_PIN_5
#define PLAY_PREV_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */

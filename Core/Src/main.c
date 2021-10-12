/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2021 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */

/*
 * (Added `USER CODE')
 * Copyright (c) 2021 Sakura Internet Inc.
 *
 * SPDX-License-Identifier: MIT
 */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include "sipf_client.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define SW_POLL_TIMEOUT	(100)
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
DMA_HandleTypeDef hdma_usart1_rx;

/* USER CODE BEGIN PV */
static uint8_t buff[256];
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */
void SipfClientUartInit(UART_HandleTypeDef *puart);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
int print_msg(const char *fmt, ...)
{
    static char msg[128];
    int  len;

    va_list list;
    va_start(list, fmt);
    len = vsprintf(msg, fmt, list);
    va_end(list);

    HAL_UART_Transmit(&huart2, (uint8_t*)msg, len, 100);

    return len;
}

static void requestResetModule(void)
{
	// Reset request.
	HAL_GPIO_WritePin(OUTPUT_WAKE_IN_GPIO_Port, OUTPUT_WAKE_IN_Pin, GPIO_PIN_SET);
	HAL_Delay(10);
	HAL_GPIO_WritePin(OUTPUT_WAKE_IN_GPIO_Port, OUTPUT_WAKE_IN_Pin, GPIO_PIN_RESET);

	HAL_Delay(200);
}

static int waitBootModule(void)
{
	int len, is_echo = 0;

	// Wait READY message.
	for (;;) {
		len = SipfUtilReadLine(buff, sizeof(buff), 65000);
		if (len < 0) {
			// ERROR or BUSY or TIMEOUT
			return len;
		}
		if (len == 0) {
			continue;
		}
		if (len >= 13) {
			if (memcmp(buff, "*** SIPF Client", 15) == 0) {
				is_echo = 1;
			}
			//Detect READY message.
			if (memcmp(buff, "+++ Ready +++", 13) == 0) {
				break;
			}
			if (memcmp(buff, "ERR:Faild", 9) == 0) {
				print_msg("%s\r\n", buff);
				return -1;
			}
		}
		if (is_echo) {
			print_msg("%s\r\n", buff);
		}
	}
	return 0;
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */
  int ret;
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART2_UART_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  print_msg("*** SIPF Client for Nucleo(");
#if defined(SAMPLE_TX)
  print_msg("TX SAMPLE APP");
#elif defined(SAMPLE_RX)
  print_msg("RX SAMPLE APP");
#else
#error Please select Sample Application.(Declare SAMPLE_XX to '1' on main.h)
#endif
  print_msg(")***\r\n");

  SipfClientUartInit(&huart1);

  print_msg("Request module reset.\r\n");
  requestResetModule();

  print_msg("Waiting module boot\r\n");
  print_msg("### MODULE OUTPUT ###\r\n");
  ret = waitBootModule();
  if (ret != 0) {
	  print_msg("FAILED(%d)\r\n", ret);
	  return -1;
  }
  print_msg("#####################\r\n");
  print_msg("OK\r\n");

  HAL_Delay(100);
#if AUTH_MODE
  print_msg("Set Auth mode... ");
  ret = SipfSetAuthMode(0x01);
  if (ret != 0) {
	print_msg((char *)buff, "FAILED(%d)\r\n", ret);
	return -1;
  }
  print_msg("OK\r\n");
#endif
  SipfClientFlushReadBuff();

  print_msg("+++ Ready +++\r\n");
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
#if defined(SAMPLE_TX)
  uint32_t count_tx = 1;
#endif
  uint32_t poll_timeout = uwTick + SW_POLL_TIMEOUT;
  GPIO_PinState prev_ps = GPIO_PIN_SET;
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
	uint8_t b;
	for (;;) {
		if (SipfClientUartReadByte(&b) != -1) {
			HAL_UART_Transmit(&huart2, &b, 1, 0);
		} else {
			break;
		}
	}
	for (;;) {
		if (HAL_UART_Receive(&huart2, &b, 1, 0) == HAL_OK) {
			SipfClientUartWriteByte(b);
		} else {
			break;
		}
	}

	// B2 Push
	if (((int)poll_timeout - (int)uwTick) <= 0) {
		poll_timeout = uwTick + SW_POLL_TIMEOUT;
		GPIO_PinState ps;
		ps = HAL_GPIO_ReadPin(B1_GPIO_Port, B1_Pin);
		if ((prev_ps == GPIO_PIN_SET) && (ps == GPIO_PIN_RESET)) {
			memset(buff, 0, sizeof(buff));
#if defined(SAMPLE_TX)
			print_msg("B1 PUSHED\r\nTX(tag_id: 0x01, type: 0x04, value: %d)\r\n", count_tx);

			ret = SipfCmdTx(0x01, 0x04, (uint8_t*)&count_tx, 4, buff);
			switch (ret) {
			case 0:
				print_msg("OK(OTID: %s)\r\n", buff);
				count_tx++;
				break;
			case -3:
				print_msg("Receive Timeout...\r\n");
				break;
			default:
				print_msg("NG\r\n");
				break;
			}
#elif defined(SAMPLE_RX)
			print_msg("B1 PUSHED\r\nRX\r\n");
			SipfObjObject objs[16];
			uint64_t stm, rtm;
			uint8_t remain, qty;
			ret = SipfCmdRx(buff, &stm, &rtm, &remain, &qty, objs, 16);
			if (ret > 0) {
				time_t t;
				struct tm *ptm;
				static char ts[128];
				print_msg("OTID:%s\r\n", buff);
				/* USER_SEND_DATE_TIME*/
				t = (time_t)stm / 1000;
				ptm = localtime(&t);
				strftime(ts, sizeof(ts),"User send datetime(UTC)    : %Y/%m/%d %H:%M:%S\r\n", ptm);
				print_msg(ts);
				/* SIPF_RECEIVE_DATE_TIME*/
				t = (time_t)rtm / 1000;
				ptm = localtime(&t);
				strftime(ts, sizeof(ts),"SIPF received datetime(UTC): %Y/%m/%d %H:%M:%S\r\n", ptm);
				print_msg(ts);
				/* REMAIN / QTY */
				print_msg("remain:%d\r\nqty:%d\r\n", remain, qty);

				SipfObjPrimitiveType v;
				for (int i = 0; i < ret; i++) {
					//受信データあった
					print_msg("obj[%d]: type=0x%02x, tag=0x%02x, len=%d, value=", i, objs[i].type, objs[i].tag_id, objs[i].value_len);
					uint8_t *p_value = objs[i].value;
					switch (objs[i].type) {
					case OBJ_TYPE_UINT8:
						memcpy(v.b, p_value, sizeof(uint8_t));
						print_msg("%u\r\n", v.u8);
						break;
					case OBJ_TYPE_INT8:
						memcpy(v.b, p_value, sizeof(int8_t));
						print_msg("%d\r\n", v.i8);
						break;
					case OBJ_TYPE_UINT16:
						memcpy(v.b, p_value, sizeof(uint16_t));
						print_msg("%u\r\n", v.u16);
						break;
					case OBJ_TYPE_INT16:
						memcpy(v.b, p_value, sizeof(int16_t));
						print_msg("%d\r\n", v.i16);
						break;
					case OBJ_TYPE_UINT32:
						memcpy(v.b, p_value, sizeof(uint32_t));
						print_msg("%u\r\n", v.u32);
						break;
					case OBJ_TYPE_INT32:
						memcpy(v.b, p_value, sizeof(int32_t));
						print_msg("%d\r\n", v.i32);
						break;
					case OBJ_TYPE_UINT64:
						memcpy(v.b, p_value, sizeof(uint64_t));
						print_msg("%llu\r\n", v.u64);
						break;
					case OBJ_TYPE_INT64:
						memcpy(v.b, p_value, sizeof(int64_t));
						print_msg("%lld\r\n", v.i64);
						break;
					case OBJ_TYPE_FLOAT32:
						memcpy(v.b, p_value, sizeof(float));
						print_msg("%f\r\n", v.f);
						break;
					case OBJ_TYPE_FLOAT64:
						memcpy(v.b, p_value, sizeof(double));
						print_msg("%lf\r\n", v.d);
						break;
					case OBJ_TYPE_BIN:
						print_msg("0x");
						for (int j = 0; j < objs[i].value_len; j++) {
							print_msg("%02x", objs[i].value[j]);
						}
						print_msg("\r\n");
						break;
					case OBJ_TYPE_STR_UTF8:
						for (int j = 0; j < objs[i].value_len; j++) {
							print_msg("%c", objs[i].value[j]);
						}
						print_msg("\r\n");
						break;
					default:
						break;
					}
					// LED2 ON/OFF サンプル
					if ((objs[i].tag_id == 0x4c) && (objs[i].type == OBJ_TYPE_UINT8)) {
						//Tag='L' で Type=uint8の場合
						if (*(uint8_t*)objs[i].value == 0) {
							// LED消す
							print_msg("LED2(GREEN): OFF\r\n");
							HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
						} else {
							// LED付ける
							print_msg("LED2(GREEN): ON\r\n");
							HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);
						}
					}
				}
				print_msg("RX done.\r\n");
			} else if (ret == 0) {
				//受信データなかった
				print_msg("SipfCmdRx() empty\r\n");
			} else {
				//エラーだった
				print_msg("SipfCmdRx() failed: %d\r\n", ret);
			}
#endif
		}

		prev_ps = ps;
	}
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA2_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA2_Stream2_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA2_Stream2_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream2_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(OUTPUT_WAKE_IN_GPIO_Port, OUTPUT_WAKE_IN_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : LD2_Pin */
  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : OUTPUT_WAKE_IN_Pin */
  GPIO_InitStruct.Pin = OUTPUT_WAKE_IN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(OUTPUT_WAKE_IN_GPIO_Port, &GPIO_InitStruct);

}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/

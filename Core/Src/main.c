/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef hlpuart1;

TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim3;

/* USER CODE BEGIN PV */
uint8_t current_mode = 0;          // mode 0 = 節拍器;mode 1 = Python 分貝計
uint32_t current_bpm = 120;        // 預設節拍速度 (BPM)
uint8_t is_muted = 0;              // 蜂鳴器靜音狀態：0 = 正常發聲; 1 = 靜音

// 按鈕長按檢測變數
uint32_t button_press_start = 0;   // 記錄按下按鈕時的系統 Tick
uint8_t button_is_pressed = 0;     // 按鈕按下旗標

// 節拍計時變數
uint32_t last_beat_tick = 0;       // 上一次節拍觸發的時間點
uint32_t red_led_fade_start = 0;   // 紅燈漸暗起始時間

// UART 接收緩衝區與指令解析
uint8_t uart_rx_byte;              // 單字元接收暫存
char uart_rx_buffer[32];           // 接收命令字串緩衝區
uint8_t uart_rx_index = 0;         // 緩衝區索引指標
uint16_t py_audio_val = 0;         // 接收來自 Python 的音量數值 (0-999)
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_LPUART1_UART_Init(void);
static void MX_TIM2_Init(void);
static void MX_TIM3_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
// LPUART1 接收中斷回調函數：非阻塞式接收電腦端指令
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == LPUART1) {
        // 若收到換行符號或回車符號，代表指令結束
        if (uart_rx_byte == '\n' || uart_rx_byte == '\r') {
            uart_rx_buffer[uart_rx_index] = '\0'; // 字串結尾
            if (uart_rx_index > 0) {
                // 指令解析：查詢狀態指令 "st"
                if (strcmp(uart_rx_buffer, "st") == 0) {
                    char msg[64];
                    int len = sprintf(msg, "Mode:%d, BPM:%lu, Mute:%d\r\n", current_mode, current_bpm, is_muted);
                    HAL_UART_Transmit_IT(&hlpuart1, (uint8_t*)msg, len);
                }
                // 指令解析：切換靜音指令 "mute"
                else if (strcmp(uart_rx_buffer, "mute") == 0) {
                    is_muted = !is_muted;
                    char *msg = is_muted ? "Buzzer Muted\r\n" : "Buzzer Unmuted\r\n";
                    HAL_UART_Transmit_IT(&hlpuart1, (uint8_t*)msg, strlen(msg));
                }
                // 指令解析：動態調整 BPM（例如 "bpm 140"）
                else if (strncmp(uart_rx_buffer, "bpm ", 4) == 0) {
                    uint32_t new_bpm = atoi(&uart_rx_buffer[4]);
                    if (new_bpm >= 40 && new_bpm <= 240) {
                        current_bpm = new_bpm;
                        char msg[32];
                        int len = sprintf(msg, "BPM updated to %lu\r\n", current_bpm);
                        HAL_UART_Transmit_IT(&hlpuart1, (uint8_t*)msg, len);
                    }
                }
                // Mode 1 下：解析 Python 傳入的純數字音量 (0 ~ 999)
                else if (current_mode == 1) {
                    int val = atoi(uart_rx_buffer);
                    if (val >= 0 && val <= 999) {
                        py_audio_val = (uint16_t)val;
                    }
                }
            }
            uart_rx_index = 0; // 重置緩衝區索引
        } else {
            // 累積接收字元至緩衝區
            if (uart_rx_index < sizeof(uart_rx_buffer) - 1) {
                uart_rx_buffer[uart_rx_index++] = uart_rx_byte;
            }
        }
        // 重新啟動下一個位元組的非阻塞接收中斷
        HAL_UART_Receive_IT(&hlpuart1, &uart_rx_byte, 1);
    }
}
// 外部中斷回調函數：處理 PC13 按鈕動作（長按 3 秒切換模式）
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == B1_Pin) { // 假設 CubeMX 生成的 B1 定義為 B1_Pin (PC13)
        GPIO_PinState pin_state = HAL_GPIO_ReadPin(B1_GPIO_Port, B1_Pin);

        // 假設 NUCLEO 按鈕按下時為準位變化（依硬體上下拉而定，此處以偵測按下計時為例）
        // 實務上可透過時間差判定按下與釋放
        uint32_t current_tick = HAL_GetTick();

        if (pin_state == GPIO_PIN_RESET) { // 按下瞬間 (Pull-down 下降緣或視硬體電路)
            button_press_start = current_tick;
            button_is_pressed = 1;
        } else if (button_is_pressed) { // 釋放瞬間
            uint32_t press_duration = current_tick - button_press_start;
            if (press_duration >= 3000) { // 長按大於或等於 3000 毫秒 (3秒)
                current_mode = !current_mode; // 切換模式 0 ↔ 1
                char msg[40];
                int len = sprintf(msg, "\r\n[Switch Mode] Current Mode: %d\r\n", current_mode);
                HAL_UART_Transmit_IT(&hlpuart1, (uint8_t*)msg, len);
            }
            button_is_pressed = 0;
        }
    }
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

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
  MX_LPUART1_UART_Init();
  MX_TIM2_Init();
  MX_TIM3_Init();
  /* USER CODE BEGIN 2 */
  // 啟動 TIM2 三個通道的 PWM 輸出
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_3);

    // 啟動 TIM3 蜂鳴器 PWM 輸出
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);

    // 啟動 LPUART1 接收中斷
    HAL_UART_Receive_IT(&hlpuart1, &uart_rx_byte, 1);

    char *start_msg = "System Initialized. Dual-Mode Ready.\r\n";
    HAL_UART_Transmit_IT(&hlpuart1, (uint8_t*)start_msg, strlen(start_msg));
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	  uint32_t current_tick = HAL_GetTick();

	        if (current_mode == 0) {
	            // ==========================================
	            // 模式 0：節拍教練 (Rhythm Coach)
	            // ==========================================
	            uint32_t beat_interval = 60000 / current_bpm; // 計算每拍間隔毫秒數 (60000ms / BPM)

	            // 檢查是否到達下一個節拍點
	            if (current_tick - last_beat_tick >= beat_interval) {
	                last_beat_tick = current_tick;
	                red_led_fade_start = current_tick; // 記錄 LED 漸暗起始點

	                // 蜂鳴器發聲：若未靜音，開啟 CH3 50% 占空比 (568)
	                if (!is_muted) {
	                    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 568);
	                }

	                // 紅燈與綠燈在節拍瞬間達到最高亮度 (999)
	                __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, 999);
	                __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, 999);
	                __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, 999);
	            }

	            // 蜂鳴器防護：每拍開始 50ms 後自動關閉蜂鳴器，實現短暫嗶聲
	            if (!is_muted && (current_tick - last_beat_tick >= 50)) {
	                __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0);
	            }

	            // LED 指數/線性漸暗特效 (Fade-out 模擬殘影)
	            uint32_t elapsed_since_beat = current_tick - red_led_fade_start;
	            if (elapsed_since_beat < beat_interval) {
	                // 隨著時間過去，亮度從 999 遞減至 0
	                int32_t red_brightness = 999 - (int32_t)((elapsed_since_beat * 999) / beat_interval);
	                if (red_brightness < 0) red_brightness = 0;

	                __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, (uint32_t)red_brightness);
	                __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, (uint32_t)red_brightness);
	                __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, (uint32_t)red_brightness);
	            }

	        } else {
	            // ==========================================
	            // 模式 1：Python 音訊派對燈 (Audio-Reactive LED)
	            // ==========================================
	            // 蜂鳴器在 Mode 1 強制靜音
	            __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0);

	            // 紅色 LED (CH1)：正向隨音量大小變化 (聲音越大越亮)
	            __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, py_audio_val);

	            // 黃色 LED (CH2)：保持 OFF (熄滅)
	            __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, 0);

	            // 綠色 LED (CH3)：反態漸變 (無聲或低音量最亮，聲音越大越暗，形成強烈對比)
	            uint16_t green_val = 999 - py_audio_val;
	            __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, green_val);
	        }

	        // 避免 CPU 耗盡，加入微小延遲（同時確保不阻礙中斷與非阻塞計時）
	        HAL_Delay(5);
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
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
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV4;
  RCC_OscInitStruct.PLL.PLLN = 85;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
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
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief LPUART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_LPUART1_UART_Init(void)
{

  /* USER CODE BEGIN LPUART1_Init 0 */

  /* USER CODE END LPUART1_Init 0 */

  /* USER CODE BEGIN LPUART1_Init 1 */

  /* USER CODE END LPUART1_Init 1 */
  hlpuart1.Instance = LPUART1;
  hlpuart1.Init.BaudRate = 209700;
  hlpuart1.Init.WordLength = UART_WORDLENGTH_8B;
  hlpuart1.Init.StopBits = UART_STOPBITS_1;
  hlpuart1.Init.Parity = UART_PARITY_NONE;
  hlpuart1.Init.Mode = UART_MODE_TX_RX;
  hlpuart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  hlpuart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  hlpuart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  hlpuart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&hlpuart1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&hlpuart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&hlpuart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&hlpuart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN LPUART1_Init 2 */

  /* USER CODE END LPUART1_Init 2 */

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 169;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 999;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */
  HAL_TIM_MspPostInit(&htim2);

}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 169;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 1136;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */
  HAL_TIM_MspPostInit(&htim3);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
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
#ifdef USE_FULL_ASSERT
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

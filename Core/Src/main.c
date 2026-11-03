/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : NUCLEO-G474RE 雙模式互動燈光與節拍系統
  *                   Mode 0: 節拍教練 (Rhythm Coach)
  *                   Mode 1: Python 音訊派對燈 (Audio-Reactive LED)
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"                                  /* 包含 CubeMX 生成的主標頭檔 */

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>                                 /* 包含標準輸入輸出庫，用於 sprintf 格式化字串 */
#include <stdlib.h>                                /* 包含標準工具庫，用於 atoi 字串轉整數 */
#include <string.h>                                /* 包含字串處理庫，用於 strcmp, strncmp, strlen */
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
UART_HandleTypeDef hlpuart1;                      /* LPUART1 串口控制代碼 */

TIM_HandleTypeDef htim2;                          /* TIM2 定時器控制代碼 (三色 LED PWM) */
TIM_HandleTypeDef htim3;                          /* TIM3 定時器控制代碼 (無源蜂鳴器 PWM) */

/* USER CODE BEGIN PV */
/* ================= 系統模式與參數變數 ================= */
uint8_t current_mode = 0;                         /* 當前系統模式：0 = 節拍器模式, 1 = Python 音訊派對燈 */
uint32_t current_bpm = 120;                       /* Mode 0 預設節拍速度 (BPM) */
uint8_t is_muted = 0;                             /* 蜂鳴器靜音狀態開關：0 = 正常發聲, 1 = 靜音 */

/* ================= PC13 按鈕長按檢測變數 ================= */
uint32_t button_press_start = 0;                  /* 紀錄按鈕按下的起始時間戳記 (ms) */
uint8_t button_is_pressed = 0;                    /* 按鈕狀態旗標：0 = 未按下, 1 = 按下計時中, 2 = 已觸發切換 */

/* ================= Mode 0 節拍與時間控制變數 ================= */
uint32_t last_beat_tick = 0;                      /* 上一次觸發節拍的時間點 (ms) */
uint32_t red_led_fade_start = 0;                  /* LED 漸暗效果的起始時間點 (ms) */

/* ================= LPUART1 通訊與指令解析變數 ================= */
uint8_t uart_rx_byte;                             /* UART 單一位元組中斷接收暫存器 */
char uart_rx_buffer[32];                          /* 接收指令的字串緩衝區 */
uint8_t uart_rx_index = 0;                        /* 字串緩衝區寫入索引指標 */
uint16_t py_audio_val = 0;                        /* Mode 1 解析來自 Python 的音量強弱數值 (0 ~ 999) */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);                    /* 系統時脈設定函式原型 */
static void MX_GPIO_Init(void);                   /* GPIO 初始化函式原型 */
static void MX_LPUART1_UART_Init(void);            /* LPUART1 通訊初始化函式原型 */
static void MX_TIM2_Init(void);                   /* TIM2 PWM 初始化函式原型 */
static void MX_TIM3_Init(void);                   /* TIM3 PWM 初始化函式原型 */
/* USER CODE BEGIN PFP */
void Process_Button_Check(uint32_t current_tick);  /* PC13 長按 3 秒非阻塞檢測函式 */
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
  * @brief  LPUART1 接收中斷回調函數：非阻塞式接收電腦端傳入指令
  * @param  huart: UART 控制代碼指標
  * @retval None
  */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == LPUART1) {             /* 確認觸發中斷的是 LPUART1 */
        /* 若接收到換行符號 ('\n') 或回車符號 ('\r')，代表一串指令結束 */
        if (uart_rx_byte == '\n' || uart_rx_byte == '\r') {
            uart_rx_buffer[uart_rx_index] = '\0';  /* 為接收到的字串加上結束符號 */
            if (uart_rx_index > 0) {
                /* 指令 1：查詢當前系統狀態 "st" */
                if (strcmp(uart_rx_buffer, "st") == 0) {
                    char msg[64];
                    int len = sprintf(msg, "Mode:%d, BPM:%lu, Mute:%d\r\n", current_mode, current_bpm, is_muted);
                    HAL_UART_Transmit_IT(&hlpuart1, (uint8_t*)msg, len); /* 非阻塞傳送狀態 */
                }
                /* 指令 2：切換靜音狀態 "mute" */
                else if (strcmp(uart_rx_buffer, "mute") == 0) {
                    is_muted = !is_muted;         /* 切換靜音旗標 */
                    char *msg = is_muted ? "Buzzer Muted\r\n" : "Buzzer Unmuted\r\n";
                    HAL_UART_Transmit_IT(&hlpuart1, (uint8_t*)msg, strlen(msg));
                }
                /* 指令 3：動態調整 BPM 節拍速度 (例如 "bpm 140") */
                else if (strncmp(uart_rx_buffer, "bpm ", 4) == 0) {
                    uint32_t new_bpm = atoi(&uart_rx_buffer[4]); /* 解析 bpm 後方數字 */
                    if (new_bpm >= 30 && new_bpm <= 300) {      /* 限制合法 BPM 範圍 */
                        current_bpm = new_bpm;    /* 更新當前 BPM */
                        char msg[32];
                        int len = sprintf(msg, "BPM updated to %lu\r\n", current_bpm);
                        HAL_UART_Transmit_IT(&hlpuart1, (uint8_t*)msg, len);
                    }
                }
                /* 指令 4：Mode 1 下解析 Python 傳送之音量數值 (0 ~ 999) */
                else if (current_mode == 1) {
                    int val = atoi(uart_rx_buffer);
                    if (val >= 0 && val <= 999) {
                        py_audio_val = (uint16_t)val;            /* 更新音量動態數值 */
                    }
                }
            }
            uart_rx_index = 0;                    /* 重置緩衝區寫入索引，準備下一條指令 */
        } else {
            /* 累積接收字元至緩衝區，防範邊界溢位 */
            if (uart_rx_index < sizeof(uart_rx_buffer) - 1) {
                uart_rx_buffer[uart_rx_index++] = uart_rx_byte;
            }
        }
        /* 重新啟用 UART 接收中斷，持續進行非阻塞監聽 */
        HAL_UART_Receive_IT(&hlpuart1, &uart_rx_byte, 1);
    }
}

/**
  * @brief  PC13 藍色按鈕長按 3 秒判定函式 (非阻塞時間差檢測)
  * @param  current_tick: 當前系統滴答時間 (ms)
  * @retval None
  */
void Process_Button_Check(uint32_t current_tick) {
    /* NUCLEO-G474RE 板載 B1 按鈕預設為下拉 (Pull-down)，按下時輸出高準位 (HIGH) */
    GPIO_PinState pin_state = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13);

    if (pin_state == GPIO_PIN_SET) {               /* 檢測到按鈕處於按下狀態 */
        if (button_is_pressed == 0) {
            button_press_start = current_tick;     /* 紀錄剛按下時的時間點 */
            button_is_pressed = 1;                 /* 標記按鈕開始進入按下狀態 */
        } else if (button_is_pressed == 1) {
            /* 若持續按下時間達到或超過 3000 毫秒 (3秒) */
            if (current_tick - button_press_start >= 3000) {
                current_mode = !current_mode;      /* 切換系統模式 (Mode 0 ↔ Mode 1) */

                /* 模式切換時強制關閉蜂鳴器 PWM 輸出，確保零雜音 */
                __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0);

                /* 發送模式切換成功通知至電腦 */
                char msg[40];
                int len = sprintf(msg, "\r\n[Switch Mode] Current Mode: %d\r\n", current_mode);
                HAL_UART_Transmit(&hlpuart1, (uint8_t*)msg, len,100);

                button_is_pressed = 2;             /* 設為 2，防止持續長按時重複觸發切換 */
            }
        }
    } else {
        button_is_pressed = 0;                     /* 按鈕放開，重置長按判定旗標 */
    }
}

/* USER CODE END 0 */

/**
  * @brief  主程式進入點 (The application entry point)
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* 重置所有週邊，初始化 Flash 介面與 SysTick 滴答定時器 */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* 設定系統時脈 (高頻 170MHz 運作) */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* 初始化所有已配置的硬體週邊 */
  MX_GPIO_Init();
  MX_LPUART1_UART_Init();
  MX_TIM2_Init();
  MX_TIM3_Init();

  /* USER CODE BEGIN 2 */
  /* 啟動 TIM2 三通道 PWM 輸出 (PA0:紅燈, PA1:黃燈, PA9:綠燈) */
  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);       /* 啟動 CH1 PWM */
  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2);       /* 啟動 CH2 PWM */
  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_3);       /* 啟動 CH3 PWM */

  /* 啟動 TIM3 單通道 PWM 輸出 (PB0:無源蜂鳴器) */
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);       /* 啟動 TIM3_CH3 PWM */
  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0);/* 初始設置蜂鳴器為靜音 (CCR=0) */

  /* 啟動 LPUART1 接收中斷，開始非阻塞接收第一個位元組 */
  HAL_UART_Receive_IT(&hlpuart1, &uart_rx_byte, 1);

  /* 發送開機系統初始化完成訊息 */
  char *start_msg = "System Initialized. Dual-Mode Ready.\r\n";
  HAL_UART_Transmit_IT(&hlpuart1, (uint8_t*)start_msg, strlen(start_msg));
  /* USER CODE END 2 */

  /* 無限主迴圈 (Infinite loop) */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
      uint32_t current_tick = HAL_GetTick();      /* 獲取當前系統滴答時間 (毫秒) */

      /* 執行 PC13 按鈕長按 3 秒非阻塞切換檢測 */
      Process_Button_Check(current_tick);

      if (current_mode == 0) {
          /* ========================================================
           * 模式 0：節拍教練 (Rhythm Coach)
           * ======================================================== */
          uint32_t beat_interval = 60000 / current_bpm; /* 計算每拍間隔毫秒數 (60000ms / BPM) */

          /* 判斷是否到達下一個節拍點 */
          if (current_tick - last_beat_tick >= beat_interval) {
              last_beat_tick = current_tick;       /* 更新上一次節拍點時間戳記 */
              red_led_fade_start = current_tick;   /* 記錄 LED 指數漸暗起始時間點 */

              /* 若系統未開啟靜音，開啟 TIM3_CH3 50% 占空比發聲 (ARR=1136, CCR=568) */
              if (!is_muted) {
                  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 568);
              }

              /* 節拍觸發瞬間，三色 LED 亮度瞬間達到最高 peak (999) */
              __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, 999); /* 紅燈全亮 */
              __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, 999); /* 黃燈全亮 */
              __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, 999); /* 綠燈全亮 */
          }

          /* 蜂鳴器防護：每拍開始滿 50ms 後自動關閉發聲，實現短促清脆嗶聲 */
          if (!is_muted && (current_tick - last_beat_tick >= 50)) {
              __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0);     /* 關閉蜂鳴器 */
          }

          /* LED 依當前節拍時間差進行指數/線性漸暗 (Fade-out 視覺殘影) */
          uint32_t elapsed_since_beat = current_tick - red_led_fade_start;
          if (elapsed_since_beat < beat_interval) {
              /* 依時間經過比例，將亮度從 999 遞減至 0 */
              int32_t brightness = 999 - (int32_t)((elapsed_since_beat * 999) / beat_interval);
              if (brightness < 0) brightness = 0;  /* 防止臨界數值計算溢位變負數 */

              __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, (uint32_t)brightness); /* 更新紅燈 */
              __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, (uint32_t)brightness); /* 更新黃燈 */
              __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, (uint32_t)brightness); /* 更新綠燈 */
          }

      } else {
          /* ========================================================
           * 模式 1：Python 音訊派對燈 (Audio-Reactive LED)
           * ======================================================== */
          /* Mode 1 下強制保持蜂鳴器靜音 */
          __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0);

          /* 紅色 LED (CH1)：正向隨音量大小變化 (音量越大越亮，做打擾警告燈) */
          __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, py_audio_val);

          /* 黃色 LED (CH2)：保持 OFF (熄滅)，聚焦於紅白與綠視覺對比 */
          __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, 0);

          /* 綠色 LED (CH3)：反態漸變 (無聲最亮 999，音量越大越暗 0) */
          uint16_t green_val = 999 - py_audio_val;
          __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, green_val);
      }

      /* 微小非阻塞式延遲，避免過度鎖死 CPU 主頻，同時保持極高響應度 */
      HAL_Delay(1);

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief 系統時脈配置函式 (System Clock Configuration)
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** 配置主內部電壓調節器輸出電壓 (Scale 1 Boost mode)
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

  /** 初始化 RCC 振盪器 (採用 HSI 內部 16MHz 經過 PLL 倍頻至 170MHz)
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

  /** 初始化 CPU, AHB 與 APB 匯流排時脈
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
  * @brief LPUART1 初始化函式
  * @retval None
  */
static void MX_LPUART1_UART_Init(void)
{

  /* USER CODE BEGIN LPUART1_Init 0 */

  /* USER CODE END LPUART1_Init 0 */

  /* USER CODE BEGIN LPUART1_Init 1 */

  /* USER CODE END LPUART1_Init 1 */
  hlpuart1.Instance = LPUART1;
  hlpuart1.Init.BaudRate = 115200;                 /* 通訊鮑率：115200 bps */
  hlpuart1.Init.WordLength = UART_WORDLENGTH_8B;    /* 資料長度：8 Bits */
  hlpuart1.Init.StopBits = UART_STOPBITS_1;        /* 停止位元：1 Bit */
  hlpuart1.Init.Parity = UART_PARITY_NONE;          /* 校驗位元：無 */
  hlpuart1.Init.Mode = UART_MODE_TX_RX;            /* 啟用全雙工 TX 與 RX */
  hlpuart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;    /* 無硬體流控制 */
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
  * @brief TIM2 初始化函式 (LED PWM 控制)
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
  htim2.Init.Prescaler = 169;                       /* Prescaler=169, 170MHz/(169+1)=1MHz 計數頻率 */
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;      /* 上升計數模式 */
  htim2.Init.Period = 999;                          /* ARR=999, PWM 頻率 1kHz (1000 階解析度) */
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
  sConfigOC.Pulse = 0;                              /* 初始 CCR 占空比 = 0 */
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
  * @brief TIM3 初始化函式 (無源蜂鳴器 PWM 控制)
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
  htim3.Init.Prescaler = 169;                       /* Prescaler=169, 170MHz/(169+1)=1MHz 計數頻率 */
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;      /* 上升計數模式 */
  htim3.Init.Period = 1136;                         /* ARR=1136, 1MHz / 1137 ≈ 880Hz (A5 音高) */
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
  sConfigOC.Pulse = 0;                              /* 初始 CCR3 占空比 = 0 (靜音) */
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
  * @brief GPIO 初始化函式
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* 開啟 GPIO 埠時脈 (GPIOC, GPIOF, GPIOA, GPIOB) */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /* 配置 PC13 (板載 B1 藍色按鈕) 為 GPIO 輸入模式 (Pull-down 下拉) */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;           /* 一般 GPIO 輸入 */
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;             /* 下拉電阻 (按鈕按下輸出高準位 HIGH) */
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  錯誤處理函式 (Error Handler)
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* 當系統發生 HAL 庫嚴重錯誤時，關閉中斷並卡死在此無窮迴圈中 */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
/**
  * @brief  報告 Full Assert 錯誤發生的檔案名稱與行號
  * @param  file: 指向檔案名稱字串的指標
  * @param  line: 錯誤發生程式碼行號
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

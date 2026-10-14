/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : STM32G031J6 controller for MAX20078 buck LED driver
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
  *
  * PIN MAP
  *   PB7  LED   open-drain, LED anode to 3V3, low = lit
  *   PA13 INCU  active-high from debounce IC, internal pull-down
  *   PA15 INCD  active-high from debounce IC, internal pull-down
  *   PA12 FLT   MAX20078 open-drain fault, active low, internal pull-up
  *   PB0  PWM   TIM3_CH3 -> MAX20078 DIM (active high, 10k pull-up on board)
  *
  * TIMER
  *   PSC = 4, ARR = 31999  ->  64MHz / 5 / 32000 = 400 Hz, 32000 steps.
  *   Period is 2.5ms, so DUTY_MIN of 560 counts is a 43us DIM pulse --
  *   about 20 cycles of the 460kHz switching frequency, enough for the
  *   average-current loop to settle. (At 2 kHz the same count is only
  *   8.75us and the low end stops regulating.)
  *
  * RAMP
  *   Level is computed from total elapsed time since the press began, not
  *   accumulated per iteration. Accumulating truncated increments loses
  *   almost everything when the loop runs faster than 1ms per step; anchoring
  *   to a start time makes a full sweep in exactly SWEEP_MS by construction.
  *
  * BEHAVIOUR
  *   Hold INCU / INCD to ramp brightness smoothly. Holding INCD past minimum
  *   turns the output fully off; INCU wakes it back to minimum.
  *
  * MAX20078 NOTES
  *   DIM low for >200ms puts the part into shutdown, which pulls FLT low.
  *   That is expected in off mode, not a fault, so the fault latch is gated
  *   on off_mode and on a wake window after leaving it. On a genuine fault
  *   the output is held at DUTY_MIN rather than stopped -- stopping the timer
  *   would release PB0 to the board's 10k pull-up on DIM and drive the LED to
  *   full brightness.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* Declared here, ahead of all code, so CubeMX regeneration can't reorder them
 * behind a call site. */
static void     latch_fault(void);
static void     write_duty(uint16_t d);
static uint16_t level_to_duty(uint16_t lv);
static void     apply_level(void);
static void     go_off(void);
static void     go_on(void);
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
TIM_HandleTypeDef htim3;

/* USER CODE BEGIN PV */
/* --- PWM range ----------------------------------------------------------- */
#define DUTY_MAX      32000u   /* = ARR + 1, 100% duty */
#define DUTY_MIN        560u   /* 1.75% = 43us on-time at 400Hz */
#define DUTY_OFF          0u   /* full off; DIM low, controller shuts down */

/* --- perceptual level ---------------------------------------------------- */
#define LEVEL_MAX      1000u   /* linear user-facing level, gamma applied below */
#define LEVEL_MIN         0u
#define LEVEL_START     500u   /* level on power-up and on wake from off */
#define SWEEP_MS       3000u   /* full 0..LEVEL_MAX sweep while held */

/* --- timing -------------------------------------------------------------- */
#define DEBOUNCE_N      200u   /* consecutive stable samples before acting */
#define BLINK_MS        100u   /* indicator toggle interval while adjusting */
#define STARTUP_MS      300u   /* > 200ms shutdown timer + controller startup */
#define WAKE_MS         400u   /* ignore FLT this long after leaving off mode */

#define LED_ON()  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET)
#define LED_OFF() HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET)

volatile uint8_t  fault     = 0;   /* latched, clears only on power cycle */
static   uint16_t level     = LEVEL_START;
static   uint8_t  off_mode  = 0;
volatile uint32_t wake_tick = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM3_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

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
  MX_TIM3_Init();
  /* USER CODE BEGIN 2 */
  /* Discard any EXTI edge captured while the GPIO block was configured. */
  __HAL_GPIO_EXTI_CLEAR_FALLING_IT(FLT_Pin);

  LED_OFF();

  /* Start mid-range. Never start at DUTY_OFF: DIM low for >200ms puts the
   * MAX20078 into shutdown, which pulls FLT low. */
  level = LEVEL_START;
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);
  apply_level();

  /* Let the controller finish its own startup before believing FLT. */
  HAL_Delay(STARTUP_MS);
  if (HAL_GPIO_ReadPin(FLT_GPIO_Port, FLT_Pin) == GPIO_PIN_RESET)
  {
    latch_fault();
  }

  uint32_t blink_t = HAL_GetTick();
  uint32_t ramp_t0 = 0;      /* tick when the current press began */
  uint16_t ramp_l0 = 0;      /* level when the current press began */
  uint8_t  ramping = 0;      /* a press is currently in progress */
  uint8_t  ramp_up = 0;      /* direction of the current press */
  uint16_t up_cnt  = 0;
  uint16_t dn_cnt  = 0;
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    if (fault)
    {
      LED_ON();                 /* solid = latched fault, clears on power cycle */
      continue;
    }

    /* ---- read buttons with an integrating debounce ----------------------
     * A coupled switching glitch never survives DEBOUNCE_N consecutive
     * samples; a real press does. Compensates for the weak internal
     * pull-downs, since no external ones are fitted. */
    up_cnt = (HAL_GPIO_ReadPin(INCU_GPIO_Port, INCU_Pin) == GPIO_PIN_SET)
               ? (uint16_t)(up_cnt + 1u) : 0u;
    dn_cnt = (HAL_GPIO_ReadPin(INCD_GPIO_Port, INCD_Pin) == GPIO_PIN_SET)
               ? (uint16_t)(dn_cnt + 1u) : 0u;

    if (up_cnt > DEBOUNCE_N) up_cnt = (uint16_t)(DEBOUNCE_N + 1u);  /* no rollover */
    if (dn_cnt > DEBOUNCE_N) dn_cnt = (uint16_t)(DEBOUNCE_N + 1u);

    uint8_t up = (up_cnt > DEBOUNCE_N);
    uint8_t dn = (dn_cnt > DEBOUNCE_N);

    /* Both directions at once is physically meaningless: treat as noise. */
    if (up && dn) { up = 0; dn = 0; }

    uint32_t now = HAL_GetTick();

    if (up || dn)
    {
      /* Anchor on a new press, or when direction reverses mid-hold. */
      if (!ramping || (ramp_up != up))
      {
        ramping = 1;
        ramp_up = up;
        ramp_t0 = now;
        ramp_l0 = level;
      }

      /* Level from TOTAL elapsed time. A single division on the whole
       * interval, so holding for exactly SWEEP_MS always covers the full
       * range -- no truncation loss from summing per-iteration steps. */
      uint32_t elapsed = now - ramp_t0;
      uint32_t travel  = ((uint32_t)LEVEL_MAX * elapsed) / SWEEP_MS;

      if (up)
      {
        if (off_mode)
        {
          go_on();                                /* off -> lowest brightness */
          ramp_t0 = now;
          ramp_l0 = level;
        }
        else
        {
          level = ((uint32_t)ramp_l0 + travel > LEVEL_MAX)
                    ? LEVEL_MAX : (uint16_t)(ramp_l0 + travel);
          apply_level();
        }
      }
      else if (!off_mode)
      {
        if (travel >= (uint32_t)ramp_l0)          /* would go below minimum */
        {
          if (level == LEVEL_MIN)
          {
            go_off();                             /* already at minimum -> off */
          }
          else
          {
            level = LEVEL_MIN;
            apply_level();
          }
        }
        else
        {
          level = (uint16_t)(ramp_l0 - travel);
          apply_level();
        }
      }

      /* Blink the indicator on its own timebase, so ramp rate and blink
       * rate stay independent. */
      if ((now - blink_t) >= BLINK_MS)
      {
        blink_t = now;
        HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
      }
    }
    else
    {
      ramping = 0;
      LED_OFF();
      blink_t = now;
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
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV1;
  RCC_OscInitStruct.PLL.PLLN = 8;
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
                              |RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
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

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 4;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 31999;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
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
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin : LED_Pin */
  GPIO_InitStruct.Pin = LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : FLT_Pin */
  GPIO_InitStruct.Pin = FLT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(FLT_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : INCU_Pin INCD_Pin */
  GPIO_InitStruct.Pin = INCU_Pin|INCD_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI4_15_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI4_15_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/**
  * @brief Write the compare register.
  *
  * DUTY_OFF (0) passes through deliberately; every other value is held at or
  * above DUTY_MIN so the DIM pulse always covers enough switching cycles for
  * the controller's current loop to settle.
  */
static void write_duty(uint16_t d)
{
  if (d != DUTY_OFF)
  {
    if (d < DUTY_MIN) d = DUTY_MIN;
    if (d > DUTY_MAX) d = DUTY_MAX;
  }
  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, d);
}

/**
  * @brief Map a linear 0..LEVEL_MAX level onto duty with a square-law curve.
  *
  * Perceived brightness rises faster than duty, so a linear duty ramp appears
  * to race through the dim end and crawl at the top. Square law corrects most
  * of that. A full cubic is too aggressive here: DUTY_MIN is already a hard
  * floor, so cubing compresses most of the travel into the bottom stop and
  * the ramp looks like it does nothing until it is most of the way up.
  */
static uint16_t level_to_duty(uint16_t lv)
{
  if (lv > LEVEL_MAX) lv = LEVEL_MAX;

  uint32_t l = (uint32_t)lv;
  uint32_t g = (l * l) / LEVEL_MAX;          /* 0..LEVEL_MAX */

  return (uint16_t)(DUTY_MIN + (g * (DUTY_MAX - DUTY_MIN)) / LEVEL_MAX);
}

/**
  * @brief Push the current level to the timer.
  */
static void apply_level(void)
{
  write_duty(level_to_duty(level));
}

/**
  * @brief Enter off mode: DIM held low, output fully dark.
  *
  * After ~200ms the MAX20078 shuts down its VCC regulator and pulls FLT low.
  * That is expected here, so the fault latch is suppressed while off_mode set.
  */
static void go_off(void)
{
  off_mode = 1;
  write_duty(DUTY_OFF);
}

/**
  * @brief Leave off mode at the lowest usable brightness.
  *
  * The controller must restart its VCC regulator, during which FLT may still
  * read low, so a wake window suppresses the fault latch briefly.
  */
static void go_on(void)
{
  off_mode  = 0;
  level     = LEVEL_MIN;
  wake_tick = HAL_GetTick();
  apply_level();
}

/**
  * @brief Latch a fault. Output held at DUTY_MIN -- not stopped, not zeroed.
  *
  * Stopping the timer would release PB0 to the board's 10k pull-up on DIM and
  * drive the LED to full brightness. Zeroing the duty would hold DIM low past
  * the 200ms shutdown timer, which itself asserts FLT and makes the fault
  * impossible to clear.
  */
static void latch_fault(void)
{
  fault    = 1;
  off_mode = 0;
  level    = LEVEL_MIN;
  apply_level();
}

/**
  * @brief FLT falling edge, confirmed by re-read.
  *
  * A genuine MAX20078 fault holds FLT low, so it survives the re-read.
  * A glitch coupled from the switching node does not.
  */
void HAL_GPIO_EXTI_Falling_Callback(uint16_t pin)
{
  if (pin != FLT_Pin) return;
  if (off_mode) return;                              /* shutdown-mode FLT, expected */
  if ((HAL_GetTick() - wake_tick) < WAKE_MS) return; /* controller still restarting */

  if (HAL_GPIO_ReadPin(FLT_GPIO_Port, FLT_Pin) == GPIO_PIN_RESET)
  {
    latch_fault();
  }
}
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

/* USER CODE BEGIN Header */
/**
  
  * @file           : main.c
  * @brief          : Lecture du capteur HX711 ,
  *                   affichage sur écran LCD I2C et envoi du poids via USART3.
 
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
#include "i2c.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "be.h"       // Pilote HX711 
#include "lcd.h"      // Pilote écran LCD RGB I2C
#include <stdio.h>
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

/* USER CODE BEGIN PV */
hx711_t loadcell;   // Structure de configuration du capteur HX711
float   weight;     // Poids brut retourné par le HX711 
char    lcd_buf[16]; // Buffer de formatage pour l'affichage LCD
rgb_lcd lcdData;    // Structure de configuration de l'écran LCD RGB
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);

/* USER CODE BEGIN PFP */

/**
  * @brief  Affiche le poids sur l'écran LCD I2C.
  *         Ligne 0 : étiquette "Poids:"
  *         Ligne 1 : valeur 
  * @param  W  
  */
void lcd_affichage(int W)
{
    clearlcd();

    /* Afficher l'étiquette sur la première ligne */
    lcd_position(&hi2c1, 0, 0);
    lcd_print(&hi2c1, "Poids:");

    /* Formater et afficher la valeur sur la deuxième ligne */
    sprintf(lcd_buf, "%d.%d g", (W / 100), (W % 100));
    lcd_position(&hi2c1, 0, 1);
    lcd_print(&hi2c1, lcd_buf);
}

/**
  * @brief  Envoie la valeur de masse formatée vers la carte WL55JC1
  *         via USART3 
  
  */
void envoimass(int m)
{
    char msg[64];
    int len = snprintf(msg, sizeof(msg), "Poids:%d.%d g\r\n", (m / 100), (m % 100));
    HAL_UART_Transmit(&huart3, (uint8_t *)msg, len, HAL_MAX_DELAY);
}

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */


int main(void)
{
    /* USER CODE BEGIN 1 */

    /* USER CODE END 1 */

    /* MCU Configuration -------------------------------------------------------*/
    HAL_Init();

    /* USER CODE BEGIN Init */

    /* USER CODE END Init */

    SystemClock_Config();

    /* USER CODE BEGIN SysInit */

    /* USER CODE END SysInit */

    /* Initialisation des périphériques configurés */
    MX_GPIO_Init();
    MX_USART2_UART_Init();
    MX_TIM1_Init();
    MX_I2C1_Init();
    MX_USART3_UART_Init();  // USART3 : liaison vers la carte WL55JC1 (SENDER)

    /* USER CODE BEGIN 2 */

    /* Initialiser l'écran LCD RGB via I2C */
    lcd_init(&hi2c1, &lcdData);

    /* Initialiser le capteur HX711 avec les broches CLK et DATA */
    hx711_init(&loadcell,
               HX711_CLK_GPIO_Port,  HX711_CLK_Pin,
               HX711_DATA_GPIO_Port, HX711_DATA_Pin);

    /* Appliquer le coefficient de calibration (déterminé après étalonnage) */
    hx711_coef_set(&loadcell, 354.5);

    /* Effectuer la mise à zéro (tare) sur 10 mesures */
    hx711_tare(&loadcell, 10);

    /* USER CODE END 2 */

    /* Boucle infinie ----------------------------------------------------------*/
    /* USER CODE BEGIN WHILE */
    while (1)
    {
        /* USER CODE END WHILE */

        /* USER CODE BEGIN 3 */

        /* Lire le poids (moyenne sur 10 échantillons) */
        weight = hx711_weight(&loadcell, 10);

        /* Afficher sur le LCD */
        lcd_affichage(weight);

        /* Envoyer via USART3 vers la carte LoRa */
        envoimass(weight);

        /* Pause de 100 ms entre chaque mesure */
        HAL_Delay(100);
    }
    /* USER CODE END 3 */
}


void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
        Error_Handler();

    RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState            = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource       = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM            = 1;
    RCC_OscInitStruct.PLL.PLLN            = 10;
    RCC_OscInitStruct.PLL.PLLP            = RCC_PLLP_DIV7;
    RCC_OscInitStruct.PLL.PLLQ            = RCC_PLLQ_DIV2;
    RCC_OscInitStruct.PLL.PLLR            = RCC_PLLR_DIV2;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
        Error_Handler();

    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                     | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
        Error_Handler();
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */


void Error_Handler(void)
{
    /* USER CODE BEGIN Error_Handler_Debug */
    __disable_irq();
    while (1) {}
    /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
/**
  * @brief  Rapporte le fichier source et la ligne où l'erreur assert s'est produite.
  * @param  file : pointeur vers le nom du fichier source
  * @param  line : numéro de ligne de l'erreur
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
    /* USER CODE BEGIN 6 */
    /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
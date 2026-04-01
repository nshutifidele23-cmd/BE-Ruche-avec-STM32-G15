/* USER CODE BEGIN Header */
/**
  
  * @file           : main.c
  * @brief          : Pont USART1 → LoRa → terminal USART2
  *                   Carte A : reçoit les données sur USART1, les transmet via LoRa
  *                   Carte B : reçoit les paquets LoRa, les affiche sur le terminal USART2
  *
  *   Pour choisir le rôle de chaque carte, définir l'une des deux macros :
  *       #define BOARD_SENDER    (reçoit sur USART1, émet via LoRa)
  *   ou
  *       #define BOARD_RECEIVER  (reçoit via LoRa, affiche sur USART2)
  *   dans la section RÔLE DE LA CARTE ci-dessous, ou via un flag de compilation.
 
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "usart.h"
#include "subghz.h"
#include "gpio.h"

/* USER CODE BEGIN Includes */
#include "radio_driver.h"
#include "stm32wlxx_nucleo.h"
/* USER CODE END Includes */

/* USER CODE BEGIN PD */

/*  RÔLE DE LA CARTE 

#define BOARD_SENDER      // Cette carte : entrée USART1 → sortie LoRa
//#define BOARD_RECEIVER  // Cette carte : entrée LoRa  → terminal USART2

/*  Paramètres RF / LoRa */
#define MAX_BUFFER_SIZE         256
#define RF_FREQ                 868000000   // 868 MHz
#define TX_POWER                14          // dBm
#define LORA_BANDWIDTH          LORA_BW_125
#define LORA_SPREADING_FACTOR   LORA_SF7
#define LORA_CODING_RATE        LORA_CR_4_5
#define LORA_PREAMBLE_LENGTH    8
#define RX_TIMEOUT_MS           5000        // ms 

/* USER CODE END PD */

/* USER CODE BEGIN PV */


static PacketParams_t packetParams;

/*  USART1 : carte SENDER 
static uint8_t  usart1Byte[1];             
static char     usart1Line[MAX_BUFFER_SIZE];
static uint16_t usart1Count  = 0;
static bool     lineReady    = false;      // mis à true par l'ISR, remis à false par la boucle principale

/*   réception LoRa (carte RECEIVER uniquement)*/
static char     loraRxBuf[MAX_BUFFER_SIZE];
static uint8_t  loraRxLen = 0;
static bool     loraRxReady = false;       // mis à true par le gestionnaire d'IRQ, remis à false par la boucle principale

/*  Pointeur d'événement IRQ → boucle principale */
static void (*volatile pendingEvent)(void) = NULL;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);

/* USER CODE BEGIN PFP */
static void UART2_Print(const char *s);
static void Radio_Init(void);
static void Radio_IRQ_Handler(RadioIrqMasks_t irq);
static void LoRa_StartRX(void);
static void LoRa_Transmit(const uint8_t *payload, uint8_t size);

/*  Gestionnaires d'événements */
static void Event_TxDone(void);
static void Event_RxDone(void);
static void Event_Timeout(void);
static void Event_CrcError(void);
/* USER CODE END PFP */

/* USER CODE BEGIN 0 */



/** Envoie une chaîne terminée par '\0' vers le terminal PC (USART2). */
static void UART2_Print(const char *s)
{
    HAL_UART_Transmit(&huart2, (const uint8_t *)s, strlen(s), HAL_MAX_DELAY);
}

/* USER CODE END 0 */


int main(void)
{
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_SUBGHZ_Init();
    MX_USART2_UART_Init();  // Terminal PC 
    MX_USART1_UART_Init();  // Liaison avec le STM32L476RG
    //MX_LPUART1_UART_Init(); 

    BSP_LED_Init(LED_BLUE);
    BSP_LED_Init(LED_GREEN);
    BSP_LED_Init(LED_RED);

    Radio_Init();

#if defined(BOARD_SENDER)
    /* SENDER : armer USART1 pour recevoir les lignes du STM32L476RG ─*/
    UART2_Print("\r\n[SENDER] En attente de donnees sur USART1...\r\n");
    BSP_LED_On(LED_BLUE);
    HAL_UART_Receive_IT(&huart1, usart1Byte, 1); //  réception du premier octet

    while (1)
    {
        /* Attendre  l'ISR  */
        if (lineReady)
        {
            lineReady = false;
            BSP_LED_Toggle(LED_GREEN);

            /* Transmettre la ligne via LoRa */
            uint8_t len = (uint8_t)strlen(usart1Line);
            LoRa_Transmit((uint8_t *)usart1Line, len);

            /*  écho sur le terminal local pour le débogage */
            //UART2_Print("[TX] ");
            //UART2_Print(usart1Line);
            //UART2_Print("\r\n");
        }

        
        if (pendingEvent)
        {
            void (*evt)(void) = pendingEvent;
            pendingEvent = NULL;
            evt();
        }
    }

#elif defined(BOARD_RECEIVER)
    /*  RECEIVER : rester en réception continue, afficher chaque paquet*/
    UART2_Print("\r\n[RECEIVER] En ecoute de paquets LoRa...\r\n");
    BSP_LED_On(LED_BLUE);
    LoRa_StartRX();

    while (1)
    {
        if (loraRxReady)
        {
            loraRxReady = false;
            BSP_LED_Toggle(LED_GREEN);

            /* Afficher le poids reçue sur le terminal */
            UART2_Print("[RX] ");
            UART2_Print(loraRxBuf);
            UART2_Print("\r\n");
        }

        
        if (pendingEvent)
        {
            void (*evt)(void) = pendingEvent;
            pendingEvent = NULL;
            evt();
        }
    }

#else
    #error "Définir BOARD_SENDER ou BOARD_RECEIVER avant la compilation."
#endif
}



static void LoRa_StartRX(void)
{
    uint16_t mask = IRQ_RX_DONE | IRQ_RX_TX_TIMEOUT | IRQ_CRC_ERROR | IRQ_HEADER_ERROR;
    SUBGRF_SetDioIrqParams(mask, mask, IRQ_RADIO_NONE, IRQ_RADIO_NONE);
    SUBGRF_SetSwitch(RFO_LP, RFSWITCH_RX);
    packetParams.Params.LoRa.PayloadLength = 0xFF;
    SUBGRF_SetPacketParams(&packetParams);
    
    SUBGRF_SetRx(RX_TIMEOUT_MS << 6);
}

static void LoRa_Transmit(const uint8_t *payload, uint8_t size)
{
    uint16_t mask = IRQ_TX_DONE | IRQ_RX_TX_TIMEOUT;
    SUBGRF_SetDioIrqParams(mask, mask, IRQ_RADIO_NONE, IRQ_RADIO_NONE);
    SUBGRF_SetSwitch(RFO_LP, RFSWITCH_TX);
    
    SUBGRF_WriteRegister(0x0889, SUBGRF_ReadRegister(0x0889) | 0x04);
    packetParams.Params.LoRa.PayloadLength = size;
    SUBGRF_SetPacketParams(&packetParams);
    SUBGRF_SendPayload((uint8_t *)payload, size, 0); 
}

/* ── Callback IRQ radio */
static void Radio_IRQ_Handler(RadioIrqMasks_t irq)
{
    switch (irq)
    {
        case IRQ_TX_DONE:
            pendingEvent = Event_TxDone;
            break;

        case IRQ_RX_DONE:
            
            SUBGRF_WriteRegister(0x0920, 0x00);
            SUBGRF_WriteRegister(0x0944, SUBGRF_ReadRegister(0x0944) | 0x02);
            SUBGRF_GetPayload((uint8_t *)loraRxBuf, &loraRxLen, 0xFF);
            loraRxBuf[loraRxLen] = '\0';
            loraRxReady = true;
            pendingEvent = Event_RxDone;
            break;

        case IRQ_RX_TX_TIMEOUT:
            pendingEvent = Event_Timeout;
            break;

        case IRQ_CRC_ERROR:
        case IRQ_HEADER_ERROR:
            pendingEvent = Event_CrcError;
            break;

        default:
            break;
    }
}



static void Event_TxDone(void)
{
   
    BSP_LED_Off(LED_RED);
}

static void Event_RxDone(void)
{
      
#if defined(BOARD_RECEIVER)
    LoRa_StartRX();
#endif
}

static void Event_Timeout(void)
{
    UART2_Print("[WARN] Timeout radio\r\n");
    BSP_LED_Toggle(LED_RED);

#if defined(BOARD_RECEIVER)
    LoRa_StartRX(); // réarmement
#endif
}

static void Event_CrcError(void)
{
    UART2_Print("[ERR] Erreur CRC / en-tete\r\n");
    BSP_LED_On(LED_RED);

#if defined(BOARD_RECEIVER)
    LoRa_StartRX();
#endif
}

/* ============================================================
 *  Callbacks HAL
 * ============================================================*/

/**
  * @brief  Réception UART complète – seul USART1  est
  *         concerné sur la carte SENDER.
  */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1)
        return;
    BSP_LED_Toggle(LED_GREEN);

    uint8_t ch = usart1Byte[0];

    if (ch == '\n' || ch == '\r')
    {
        if (usart1Count > 0)
        {
            usart1Line[usart1Count] = '\0';
            usart1Count = 0;
            lineReady = true;   // signaler la boucle principale
        }
    }
    else
    {
        if (usart1Count < MAX_BUFFER_SIZE - 1)
            usart1Line[usart1Count++] = (char)ch;
    }

    /* Réarmer la réception pour le prochain octet */
    HAL_UART_Receive_IT(&huart1, usart1Byte, 1);
}



void Radio_Init(void)
{
    SUBGRF_Init(Radio_IRQ_Handler);

    SUBGRF_WriteRegister(SUBGHZ_SMPSC0R,
        SUBGRF_ReadRegister(SUBGHZ_SMPSC0R) | SMPS_CLK_DET_ENABLE);
    SUBGRF_SetRegulatorMode();

    SUBGRF_SetBufferBaseAddress(0x00, 0x00);
    SUBGRF_SetRfFrequency(RF_FREQ);
    SUBGRF_SetRfTxPower(TX_POWER);
    SUBGRF_SetStopRxTimerOnPreambleDetect(false);
    SUBGRF_SetPacketType(PACKET_TYPE_LORA);

    /* Mot de synchronisation privé */
    SUBGRF_WriteRegister(REG_LR_SYNCWORD,      (LORA_MAC_PRIVATE_SYNCWORD >> 8) & 0xFF);
    SUBGRF_WriteRegister(REG_LR_SYNCWORD + 1,   LORA_MAC_PRIVATE_SYNCWORD       & 0xFF);

    ModulationParams_t mod;
    mod.PacketType                    = PACKET_TYPE_LORA;
    mod.Params.LoRa.Bandwidth         = LORA_BANDWIDTH;
    mod.Params.LoRa.CodingRate        = LORA_CODING_RATE;
    mod.Params.LoRa.LowDatarateOptimize = 0x00;
    mod.Params.LoRa.SpreadingFactor   = LORA_SPREADING_FACTOR;
    SUBGRF_SetModulationParams(&mod);

    packetParams.PacketType                      = PACKET_TYPE_LORA;
    packetParams.Params.LoRa.CrcMode             = LORA_CRC_ON;
    packetParams.Params.LoRa.HeaderType          = LORA_PACKET_VARIABLE_LENGTH;
    packetParams.Params.LoRa.InvertIQ            = LORA_IQ_NORMAL;
    packetParams.Params.LoRa.PayloadLength       = 0xFF;
    packetParams.Params.LoRa.PreambleLength      = LORA_PREAMBLE_LENGTH;
    SUBGRF_SetPacketParams(&packetParams);

    
    SUBGRF_WriteRegister(0x0736, SUBGRF_ReadRegister(0x0736) | (1 << 2));
}

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_MSI;
    RCC_OscInitStruct.MSIState            = RCC_MSI_ON;
    RCC_OscInitStruct.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.MSIClockRange       = RCC_MSIRANGE_11;
    RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) Error_Handler();

    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK3 | RCC_CLOCKTYPE_HCLK
                                     | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1
                                     | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_MSI;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.AHBCLK3Divider = RCC_SYSCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) Error_Handler();
}

void Error_Handler(void)
{
    __disable_irq();
    BSP_LED_On(LED_RED);
    while (1) {}
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file; (void)line;
}
#endif
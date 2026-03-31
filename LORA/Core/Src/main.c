/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : USART1 → LoRa → USART2 terminal bridge
  *                   Board A: receives data on USART1, forwards via LoRa
  *                   Board B: receives LoRa packets, prints to USART2 terminal
  *
  *   To select which role a board plays, define either:
  *       #define BOARD_SENDER    (receives on USART1, transmits via LoRa)
  *   or
  *       #define BOARD_RECEIVER  (receives LoRa, prints to USART2)
  *   in the BOARD ROLE section below, or pass it as a compiler flag.
  ******************************************************************************
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

/* ── BOARD ROLE ─────────────────────────────────────────────────────────────
 * Uncomment exactly ONE of the two lines below before flashing each board.
 * --------------------------------------------------------------------------*/
#define BOARD_SENDER      // This board: USART1 in → LoRa out
//#define BOARD_RECEIVER  // This board: LoRa in  → USART2 terminal out

/* ── RF / LoRa parameters ───────────────────────────────────────────────────*/
#define MAX_BUFFER_SIZE         256
#define RF_FREQ                 868000000   // 868 MHz
#define TX_POWER                14          // dBm
#define LORA_BANDWIDTH          LORA_BW_125
#define LORA_SPREADING_FACTOR   LORA_SF7
#define LORA_CODING_RATE        LORA_CR_4_5
#define LORA_PREAMBLE_LENGTH    8
#define RX_TIMEOUT_MS           5000        // ms – receiver re-arms after this

/* USER CODE END PD */

/* USER CODE BEGIN PV */

/* ── Shared buffers ─────────────────────────────────────────────────────────*/
static PacketParams_t packetParams;

/* ── USART1 sensor / upstream device (SENDER board only) ───────────────────*/
static uint8_t  usart1Byte[1];             // single-byte DMA/IT staging
static char     usart1Line[MAX_BUFFER_SIZE];
static uint16_t usart1Count  = 0;
static bool     lineReady    = false;      // set by ISR, cleared by main loop

/* ── LoRa RX buffer (RECEIVER board only) ──────────────────────────────────*/
static char     loraRxBuf[MAX_BUFFER_SIZE];
static uint8_t  loraRxLen = 0;
static bool     loraRxReady = false;       // set by IRQ handler, cleared by main loop

/* ── IRQ → main-loop event pointer ─────────────────────────────────────────*/
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

/* ── Event handlers (called from main loop, never from IRQ) ─────────────────*/
static void Event_TxDone(void);
static void Event_RxDone(void);
static void Event_Timeout(void);
static void Event_CrcError(void);
/* USER CODE END PFP */

/* USER CODE BEGIN 0 */

/* ── Helpers ────────────────────────────────────────────────────────────────*/

/** Print a null-terminated string to the PC terminal (USART2). */
static void UART2_Print(const char *s)
{
    HAL_UART_Transmit(&huart2, (const uint8_t *)s, strlen(s), HAL_MAX_DELAY);
}

/* USER CODE END 0 */

/* ============================================================
 *  main
 * ============================================================*/
int main(void)
{
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_SUBGHZ_Init();
    MX_USART2_UART_Init();  // PC terminal (both boards)
    MX_USART1_UART_Init();  // Upstream device link (sender board)
    //MX_LPUART1_UART_Init(); // kept in case other peripherals need it

    BSP_LED_Init(LED_BLUE);
    BSP_LED_Init(LED_GREEN);
    BSP_LED_Init(LED_RED);

    Radio_Init();

#if defined(BOARD_SENDER)
    /* ── SENDER: arm USART1 to collect lines from the upstream device ───────*/
    UART2_Print("\r\n[SENDER] Waiting for USART1 data...\r\n");
    BSP_LED_On(LED_BLUE);
    HAL_UART_Receive_IT(&huart1, usart1Byte, 1); // arm first byte

    while (1)
    {
        /* Wait until the ISR assembles a complete line */
        if (lineReady)
        {
            lineReady = false;
            BSP_LED_Toggle(LED_GREEN);

            /* Forward the line over LoRa */
            uint8_t len = (uint8_t)strlen(usart1Line);
            LoRa_Transmit((uint8_t *)usart1Line, len);

            /* Optional: echo to local terminal for debug */
            UART2_Print("[TX] ");
            UART2_Print(usart1Line);
            UART2_Print("\r\n");
        }

        /* Process deferred radio events */
        if (pendingEvent)
        {
            void (*evt)(void) = pendingEvent;
            pendingEvent = NULL;
            evt();
        }
    }

#elif defined(BOARD_RECEIVER)
    /* ── RECEIVER: stay in continuous RX, print every packet ───────────────*/
    UART2_Print("\r\n[RECEIVER] Listening for LoRa packets...\r\n");
    BSP_LED_On(LED_BLUE);
    LoRa_StartRX();

    while (1)
    {
        if (loraRxReady)
        {
            loraRxReady = false;
            BSP_LED_Toggle(LED_GREEN);

            /* Print the received payload to the terminal */
            UART2_Print("[RX] ");
            UART2_Print(loraRxBuf);
            UART2_Print("\r\n");
        }

        /* Process deferred radio events */
        if (pendingEvent)
        {
            void (*evt)(void) = pendingEvent;
            pendingEvent = NULL;
            evt();
        }
    }

#else
    #error "Define either BOARD_SENDER or BOARD_RECEIVER before building."
#endif
}

/* ============================================================
 *  Radio helpers
 * ============================================================*/

static void LoRa_StartRX(void)
{
    uint16_t mask = IRQ_RX_DONE | IRQ_RX_TX_TIMEOUT | IRQ_CRC_ERROR | IRQ_HEADER_ERROR;
    SUBGRF_SetDioIrqParams(mask, mask, IRQ_RADIO_NONE, IRQ_RADIO_NONE);
    SUBGRF_SetSwitch(RFO_LP, RFSWITCH_RX);
    packetParams.Params.LoRa.PayloadLength = 0xFF;
    SUBGRF_SetPacketParams(&packetParams);
    /* 0 = continuous RX (no timeout). Use RX_TIMEOUT_MS << 6 for timed RX. */
    SUBGRF_SetRx(RX_TIMEOUT_MS << 6);
}

static void LoRa_Transmit(const uint8_t *payload, uint8_t size)
{
    uint16_t mask = IRQ_TX_DONE | IRQ_RX_TX_TIMEOUT;
    SUBGRF_SetDioIrqParams(mask, mask, IRQ_RADIO_NONE, IRQ_RADIO_NONE);
    SUBGRF_SetSwitch(RFO_LP, RFSWITCH_TX);
    /* SX126x errata 5.1 – set bit before each TX */
    SUBGRF_WriteRegister(0x0889, SUBGRF_ReadRegister(0x0889) | 0x04);
    packetParams.Params.LoRa.PayloadLength = size;
    SUBGRF_SetPacketParams(&packetParams);
    SUBGRF_SendPayload((uint8_t *)payload, size, 0); // 0 = no TX timeout
}

/* ── Radio IRQ callback (runs in IRQ context – keep it minimal) ─────────────*/
static void Radio_IRQ_Handler(RadioIrqMasks_t irq)
{
    switch (irq)
    {
        case IRQ_TX_DONE:
            pendingEvent = Event_TxDone;
            break;

        case IRQ_RX_DONE:
            /* Read payload here while still in IRQ – it must happen before
             * the radio state changes. Flag it, let main loop print it.     */
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

/* ── Deferred event handlers (run in main-loop context) ─────────────────────*/

static void Event_TxDone(void)
{
    /* After a successful TX, the sender goes back to waiting for USART1 data.
     * Optionally arm RX briefly here if you need ACKs in the future.         */
    BSP_LED_Off(LED_RED);
}

static void Event_RxDone(void)
{
    /* loraRxReady / loraRxBuf already set in IRQ handler.
     * Re-arm the receiver so it keeps listening.                              */
#if defined(BOARD_RECEIVER)
    LoRa_StartRX();
#endif
}

static void Event_Timeout(void)
{
    UART2_Print("[WARN] Radio timeout\r\n");
    BSP_LED_Toggle(LED_RED);

#if defined(BOARD_RECEIVER)
    LoRa_StartRX(); // re-arm
#endif
}

static void Event_CrcError(void)
{
    UART2_Print("[ERR] CRC / header error\r\n");
    BSP_LED_On(LED_RED);

#if defined(BOARD_RECEIVER)
    LoRa_StartRX();
#endif
}

/* ============================================================
 *  HAL callbacks
 * ============================================================*/

/**
  * @brief  UART RX complete – only USART1 (sensor / upstream device) matters
  *         on the SENDER board. Accumulates bytes into usart1Line[] until
  *         CR or LF, then sets lineReady for the main loop.
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
            lineReady = true;   // signal main loop
        }
    }
    else
    {
        if (usart1Count < MAX_BUFFER_SIZE - 1)
            usart1Line[usart1Count++] = (char)ch;
    }

    /* Re-arm for the next byte */
    HAL_UART_Receive_IT(&huart1, usart1Byte, 1);
}

/* ============================================================
 *  Radio and clock init (unchanged from original project)
 * ============================================================*/

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

    /* Private sync word */
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

    /* SX126x errata 15.4 – IQ polarity fix */
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

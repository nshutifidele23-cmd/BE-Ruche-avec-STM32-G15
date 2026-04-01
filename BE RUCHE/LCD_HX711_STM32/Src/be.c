#include "be.h"
#include "be_config.h"
 
 
#if (_HX711_USE_FREERTOS == 1)
#include "cmsis_os.h"
#define hx711_delay(x)    osDelay(x)
#else
#define hx711_delay(x)    HAL_Delay(x)
#endif
int8_t i;
 
/**
 * @brief  Génère un délai en microsecondes par boucle d'attente active.
 *         La durée est définie par _HX711_DELAY_US_LOOP dans be_config.h.
 */
void hx711_delay_us(void)
{
  uint32_t delay = _HX711_DELAY_US_LOOP;
  while (delay > 0)
  {
    delay--;
    __NOP(); __NOP(); __NOP(); __NOP();
  }
}

/**
 * @brief  Verrouille l'accès au capteur HX711 (mutex logiciel).
 *         Bloque tant que le verrou est actif, puis aquisition.
 * @param  hx711  Pointeur vers la structure du capteur
 */
void hx711_lock(hx711_t *hx711)
{
  while (hx711->lock)
    hx711_delay(1);
  hx711->lock = 1;
}

/**
 * @brief  Déverrouille l'accès au capteur HX711.
 * @param  hx711  Pointeur vers la structure du capteur
 */
void hx711_unlock(hx711_t *hx711)
{
  hx711->lock = 0;
}

/**
 * @brief  Initialise le capteur HX711 : configure les GPIO CLK et DATA,
 *         effectue une impulsion de réveil et lit deux valeurs.
 */
void hx711_init(hx711_t *hx711, GPIO_TypeDef *clk_gpio, uint16_t clk_pin, GPIO_TypeDef *dat_gpio, uint16_t dat_pin)
{
  hx711_lock(hx711);
  hx711->clk_gpio = clk_gpio;
  hx711->clk_pin = clk_pin;
  hx711->dat_gpio = dat_gpio;
  hx711->dat_pin = dat_pin;
 
  GPIO_InitTypeDef  gpio = {0};
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  gpio.Pin = clk_pin;
  HAL_GPIO_Init(clk_gpio, &gpio);
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  gpio.Pin = dat_pin;
  HAL_GPIO_Init(dat_gpio, &gpio);
  HAL_GPIO_WritePin(hx711->clk_gpio, hx711->clk_pin, GPIO_PIN_SET);
  hx711_delay(10);
  HAL_GPIO_WritePin(hx711->clk_gpio, hx711->clk_pin, GPIO_PIN_RESET);
  hx711_delay(10);
  hx711_value(hx711);
  hx711_value(hx711);
  hx711_unlock(hx711);
}

/**
 * @brief  Lit une valeur brute 24 bits depuis le HX711 via le protocole CLK/DATA.
 *         Attend que la broche DATA passe à l'état bas (donnée prête),
 *         puis échantillonne 24 bits en générant les impulsions d'horloge.
 *         Retourne 0 si le capteur ne répond pas dans les 150 ms.
 */
int32_t hx711_value(hx711_t *hx711)
{
  uint32_t data = 0;
  uint32_t  startTime = HAL_GetTick();
 
  while(HAL_GPIO_ReadPin(hx711->dat_gpio, hx711->dat_pin) == GPIO_PIN_SET)
  {
    if(HAL_GetTick() - startTime > 150)
      return 0;
  }
  hx711_delay(1000);
 
  for(i=0; i<24 ; i++)
  {
    HAL_GPIO_WritePin(hx711->clk_gpio, hx711->clk_pin, GPIO_PIN_SET);
    hx711_delay_us();
    HAL_GPIO_WritePin(hx711->clk_gpio, hx711->clk_pin, GPIO_PIN_RESET);
    hx711_delay_us();
    data = data << 1;
    if(HAL_GPIO_ReadPin(hx711->dat_gpio, hx711->dat_pin) == GPIO_PIN_SET)
      data ++;
  }
  data = data ^ 0x800000;
  HAL_GPIO_WritePin(hx711->clk_gpio, hx711->clk_pin, GPIO_PIN_SET);
  hx711_delay_us();
  HAL_GPIO_WritePin(hx711->clk_gpio, hx711->clk_pin, GPIO_PIN_RESET);
  hx711_delay_us();
  return data;
}

/**
 * @brief  Calcule la moyenne de plusieurs lectures brutes du HX711.
 *
 */
int32_t hx711_value_ave(hx711_t *hx711, uint16_t sample)
{
  hx711_lock(hx711);
  int64_t  ave = 0;
  for(uint16_t i=0 ; i<sample ; i++)
  {
    ave += hx711_value(hx711);
    hx711_delay(5);
  }
  int32_t answer = (int32_t)(ave / sample);
  hx711_unlock(hx711);
  return answer;
}

/**
 * @brief  Effectue la mise à zéro (tare) 

 */
void hx711_tare(hx711_t *hx711, uint16_t sample)
{
  hx711_lock(hx711);
  int64_t  ave = 0;
  for(uint16_t i=0 ; i<sample ; i++)
  {
    ave += hx711_value(hx711);
    hx711_delay(5);
  }
  hx711->offset = (int32_t)(ave / sample);
  hx711_unlock(hx711);
}

/**
 * @brief  Calibre le capteur à partir de deux points de mesure connus
 *         
 */
void hx711_calibration(hx711_t *hx711, int32_t noload_raw, int32_t load_raw, float scale)
{
  hx711_lock(hx711);
  hx711->offset = noload_raw;
  hx711->coef = (load_raw - noload_raw) / scale;
  hx711_unlock(hx711);
}
//#############################################################################################
/**
 * @brief  Calcule le poids en grammes en moyennant plusieurs échantillons,
 *         en soustrayant l'offset de tare et en appliquant le coefficient de calibration.
 
 */
float hx711_weight(hx711_t *hx711, uint16_t sample)
{
  hx711_lock(hx711);
  int64_t  ave = 0;
  for(uint16_t i=0 ; i<sample ; i++)
  {
    ave += hx711_value(hx711);
    hx711_delay(5);
  }
  int32_t data = (int32_t)(ave / sample);
  float answer =  (data - hx711->offset) / hx711->coef;
  hx711_unlock(hx711);
  return answer;
}

/**
 * @brief  Définit manuellement le coefficient de calibration du capteur.
 *        
 */
void hx711_coef_set(hx711_t *hx711, float coef)
{
  hx711->coef = coef;
}
//#############################################################################################
/**
 * @brief  Retourne le coefficient de calibration actuel du capteur.

 */
float hx711_coef_get(hx711_t *hx711)
{
  return hx711->coef;
}

/**
 * @brief  Met le capteur HX711 en mode veille (power down).
 *         Une impulsion haute maintenue sur CLK > 60 µs met le circuit en veille.

 */
void hx711_power_down(hx711_t *hx711)
{
  HAL_GPIO_WritePin(hx711->clk_gpio, hx711->clk_pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(hx711->clk_gpio, hx711->clk_pin, GPIO_PIN_SET);
  hx711_delay(1);
}
//#############################################################################################
/**
 * @brief  Réveille le capteur HX711 depuis le mode veille (power up).
 *         Remettre CLK à l'état bas suffit à relancer le circuit.
 */
void hx711_power_up(hx711_t *hx711)
{
  HAL_GPIO_WritePin(hx711->clk_gpio, hx711->clk_pin, GPIO_PIN_RESET);
}
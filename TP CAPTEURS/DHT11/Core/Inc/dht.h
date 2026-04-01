/*
 * dht.h
 *
 *  Created on: Jan 30, 2026
 *      Author: G45
 */

#ifndef INC_DHT_H_
#define INC_DHT_H_

#include "stm32l4xx_hal.h"

#define DHT11_PORT      GPIOA
#define DHT11_PIN       GPIO_PIN_8


// Data structure pour les entiers de mes donnees
typedef struct {
    uint8_t humidity_int;
    uint8_t humidity_dec;
    uint8_t temperature_int;
    uint8_t temperature_dec;
    uint8_t checksum;
} DHT11_Data;

uint8_t DHT11_Init(void);
uint8_t DHT11_Read(void);
void delay (uint16_t temp);


#endif /* INC_DHT_H_ */

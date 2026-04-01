/*
 * sht.h
 *
 *  Created on: Jan 29, 2026
 *      Author: G45
 */

#ifndef INC_SHT_H_
#define INC_SHT_H_
#include "stm32l4xx_hal.h"
#include "stm32l4xx_hal_i2c.h"

// Device I2C Adress
#define SHT_ADDRESS_A     0x88 //parceque ADDR est connecte a la masse (Adress pin input)
void sht_init(I2C_HandleTypeDef* I2Cx);

#endif /* INC_SHT_H_ */

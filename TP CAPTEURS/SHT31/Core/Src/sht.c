/*
 * lib_lcd.c
 *
 *  Created on: janvier 2020
 *      Author:
 */

#include <sht.h>
#include "usart.h"
#include "main.h"
//#include "string.h"
#include "i2c.h"



void sht_init(I2C_HandleTypeDef* I2Cx)
{
	I2C_HandleTypeDef* Handle = I2Cx;
	uint8_t data[2];
	data[0] = 0x2c;
	data[1] = 0x10;

	HAL_I2C_Master_Transmit(Handle,SHT_ADDRESS_A,data,2,5000 );
	HAL_Delay(5);



}









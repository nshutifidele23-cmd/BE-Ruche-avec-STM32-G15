/*
 * dht.c
 *
 *  Created on: Jan 30, 2026
 *      Author: G45
 */

 /*
	
    Cette portion du code est dédiée à la partie .c pour implémenter
    les fonctions du DHT11 :
    - gestion du timer (microsecondes)
    - initialisation du capteur
    - lecture des données (bits)


 */
#include "dht.h"
#include "string.h"
#include "tim.h"
#include "stm32l4xx_hal.h"


/*
 * Fonction de temporisation en microsecondes
 * Utilise TIM1 configuré avec un prescaler = 79
 */

void delay (uint16_t temp){


	__HAL_TIM_SET_COUNTER(&htim1, 0);

	// Attente bloquante jusqu'à ce que le compteur atteigne "temp"
	while ((__HAL_TIM_GET_COUNTER(&htim1))<temp);
}



uint8_t DHT11_Init(void){

	 /*
     Séquence de communication avec le DHT11 :
     1. STM32 en sortie
     2. Mettre la ligne à 0 pendant au moins 18 ms
     3. Mettre la ligne à 1 pendant 10 µs
     4. Passer la broche en entrée
     5. Attendre la réponse du DHT11
     */


		GPIO_InitTypeDef GPIO_InitStruct = {0};

		uint8_t ans = 0, pMillis, cMillis;


	     HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_RESET);
	     delay(18000);
	     HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_SET);
	     delay(10);

	     // initialisation de la broche en entree pour recevoir
	     GPIO_InitStruct.Pin = DHT11_PIN;
	     GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	     GPIO_InitStruct.Pull = GPIO_PULLUP; //floating pour utiliser la resistance interne
	     GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	     HAL_GPIO_Init(DHT11_PORT, &GPIO_InitStruct);
	     delay (40);
	     if(!(HAL_GPIO_ReadPin(DHT11_PORT,DHT11_PIN))){
	    	 delay(80);
	    	 if((HAL_GPIO_ReadPin(DHT11_PORT,DHT11_PIN))) ans = 1;
	     }
	     // 2ms attente de la lecture pour ne pas bloque
	     pMillis = HAL_GetTick();
	     cMillis = HAL_GetTick();

		  /*
     Le DHT11 doit :
     - tirer la ligne à 0 pendant ~80 µs
     - puis la remettre à 1 pendant ~80 µs
     */

	     while((HAL_GPIO_ReadPin(DHT11_PORT,DHT11_PIN)) && pMillis + 2 > cMillis){
	    	 cMillis = HAL_GetTick();
	     }
	     return ans;
}



uint8_t DHT11_Read(){

	/*
 * Fonction de lecture d'un octet (8 bits) depuis le DHT11
 * Chaque bit est codé par la durée de l'état haut
 */

 
	uint8_t a,b,cMillis,pMillis;
	for(a=0; a < 8 ;a++){
		cMillis = HAL_GetTick();
		pMillis = HAL_GetTick();
		while((HAL_GPIO_ReadPin(DHT11_PORT,DHT11_PIN)) && pMillis + 2 > cMillis){
			// Wait for the pin to go high
			cMillis = HAL_GetTick();
		}
		delay(40);

		if(!(HAL_GPIO_ReadPin(DHT11_PORT,DHT11_PIN))){
			b&= ~(1<<(7-a));
		}
		else (b|= (1<<(7-a)));

		cMillis = HAL_GetTick();
		pMillis = HAL_GetTick();
		while((HAL_GPIO_ReadPin(DHT11_PORT,DHT11_PIN)) && pMillis + 2 > cMillis){
			cMillis = HAL_GetTick();
		}
	}
	return b;
}

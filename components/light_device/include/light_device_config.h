/*
 * @Author: sky
 * @Date: 2020-03-09 18:34:28
 * @LastEditTime: 2020-03-09 18:48:19
 * @LastEditors: Please set LastEditors
 * @Description: In User Settings Edit
 * @FilePath: \mqtt_example\components\light_device\include\light_device.h
 */
#ifndef _LIGHT_DEVICE_CONFIG_H
#define _LIGHT_DEVICE_CONFIG_H

#include "driver/gpio.h"
#include "driver/uart.h"


#define CNF_PRODUCT_NAME "Invisihome_"

#define CNF_INIT_FLAG	(0X55)
#define CNF_MAX_APP_LEN	(1024)
#define CNF_MAX_TOPIC_LENGTH 257
#define CNF_MAX_BODY_LENGTH  (1024 + 256)
#define CNF_RSET_PIN	(15)
#define CNF_CA_STORE

//#define BOARD_NODEMCU

#ifdef BOARD_NODEMCU

#define CNF_TAP_UP_PIN		(GPIO_NUM_15)
#define CNF_TAP_DOWN_PIN	(GPIO_NUM_4)
#define CNF_LED_PIN			(GPIO_NUM_2)

#else

#define CNF_TAP_UP_PIN		(GPIO_NUM_16) 
#define CNF_TAP_DOWN_PIN	(GPIO_NUM_0) 
#define CNF_LED_PIN			(GPIO_NUM_17)

#endif

#define CNF_KEYS_PRESS_STATUS (0)

#define CNF_PIN_TXD  (GPIO_NUM_4)
#define CNF_PIN_RXD  (GPIO_NUM_5)
#define CNF_PIN_RTS  (UART_PIN_NO_CHANGE)
#define CNF_PIN_CTS  (UART_PIN_NO_CHANGE)

#define CNF_MAX_TAP_LENGTH  CNF_MAX_APP_LEN //1024

// control setting 
#define CNF_UPDATE_TASK_CTL (50)

#define CNF_CLOUD_YONOMI_DEV_TYPE	"invisihome-invr1"

#define CNF_ZONE_	( "PST8PDT,M3.2.0,M11.1.0" )

typedef enum DEV_TYPE_T{
	DEVTYPE_Dimmable = 0,
	DEVTYPE_Smart,
	DEVTYPE_Remote,
	DEVTYPE_NonDimmable,
	DEVTYPE_MAX
}Dev_type_t;
	
#endif  // _LIGHT_DEVICE_CONFIG_H

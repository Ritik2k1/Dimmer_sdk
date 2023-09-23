/*
 * @Author: your name
 * @Date: 2020-03-09 18:34:28
 * @LastEditTime: 2020-03-09 18:37:47
 * @LastEditors: your name
 * @Description: In User Settings Edit
 * @FilePath: \mqtt_example\components\light_device\include\light_device.h
 */

#ifndef _LIGHT_DEVICE_UART_H
#define _LIGHT_DEVICE_UART_H

#include <stdint.h>
#include "mdf_err.h"

/** config ***/


/**** function ***********/
typedef enum{
	UART_CMD_BRI,
	UART_CMD_FADE,
	UART_CMD_STATUS,
	UART_CMD_DIMMER,
	UART_CMD_LIMIT_MAX,
	UART_CMD_LIMIT_MIM,
	UART_CMD_3WAY,
	
	UART_CMD_MAX
}Uart_cmd;
	
enum UART_CMD_VALUE_T{
	HOLD_INC_UP_START = 0X01,
	HOLD_INC_UP_STOP = 0X02,
	HOLD_INC_DOWN_START = 0X11,
	HOLD_INC_DOWN_STOP = 0X12,
	HOLD_INC_UP_START_DOUBLE = 0X21,
	HOLD_INC_DOWN_START_DOUBLE = 0X22
};

int uart_send(const char *p_data, size_t len);
mdf_err_t _uart_data_alloc(uint8_t        **pp_dst, int *p_dst_len, Uart_cmd cmd, void *p_data);

int uart_cmd_send(Uart_cmd cmd, void *p_data);

int uart_receive(const char **pp_recv);
int uart_recv_handle(void);

mdf_err_t _uart_init(void);


#endif  // _LIGHT_DEVICE_UART_H

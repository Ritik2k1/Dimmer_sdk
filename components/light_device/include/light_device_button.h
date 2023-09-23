/*
 * @Author: your name
 * @Date: 2020-03-09 18:34:28
 * @LastEditTime: 2020-03-09 18:37:47
 * @LastEditors: your name
 * @Description: In User Settings Edit
 * @FilePath: \mqtt_example\components\light_device\include\light_device.h
 */

#ifndef _LIGHT_DEVICE_BUTTON_H
#define _LIGHT_DEVICE_BUTTON_H

#include <stdint.h>
#include "mdf_err.h"

/** config ***/


/**** function ***********/
void button_loop(int64_t ctime);
void button_init(void);

void led_action_set(int status, uint16_t period );


#endif  // _LIGHT_DEVICE_BUTTON_H

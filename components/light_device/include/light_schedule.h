/*
 * @Author:sky
 * @Date: 2020-03-09 18:34:28
 * @LastEditTime: 2020-03-09 18:48:19
 * @LastEditors: Please set LastEditors
 * @Description: In User Settings Edit
 * @FilePath: \mqtt_example\components\light_device\include\light_device.h
 */

#ifndef _LIGHT_SCHEDULE_H
#define _LIGHT_SCHEDULE_H

#include <stdint.h>

#include "mdf_err.h"

typedef enum {
	_SCH_CMD_ALAM,
	_SCH_CMD_TAP,
	
	_SCH_CMD_MAX
}_Sch_type_t;

typedef enum{
	
	_SCH_TAP_TU = 0,
	_SCH_TAP_TD = 1,
	_SCH_TAP_DTU = 2,
	_SCH_TAP_DTD,
	
	_SCH_TAP_MAX
}_Sch_tap_T;
mdf_err_t schedule_init(void);
mdf_err_t schedule_deinit(void);

int hue_loop_get_data(_Sch_tap_T evt, char **p_url);
int tap_event_active(_Sch_tap_T evt, int fade);
void  schedule_upate_time_get(char *p_str , int len, _Sch_type_t type);

void oat_init(void);
#endif  // _LIGHT_SCHEDULE_H

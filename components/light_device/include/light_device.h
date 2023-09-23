/*
 * @Author: your name
 * @Date: 2020-03-09 18:34:28
 * @LastEditTime: 2020-03-09 18:37:47
 * @LastEditors: your name
 * @Description: In User Settings Edit
 * @FilePath: \mqtt_example\components\light_device\include\light_device.h
 */

#ifndef _LIGHT_DEVICE_H
#define _LIGHT_DEVICE_H

#include <stdint.h>

#include "mdf_err.h"
#include "utlis.h"
#include "light_device_uart.h"
#include "light_device_button.h"

/** config ***/
#define MWIFI_ID	(0x01)
#define M_NAME	("009")

typedef struct LIGHT_STATUS_T{
	uint8_t *p_power;
	uint8_t *p_bri;
	uint16_t *p_fade;
	uint8_t *p_dimmer_dir;
}Light_status_t;
typedef enum {
	
	LSYS_STATUS_CONNECTING,
	LSYS_STATUS_OTAING,
	LSYS_STATUS_CNN,
	LSYS_STATUS_ONLINE,
	LSYS_STATUS_LOST_CNN,

	LSYS_STATUS_MAX
	
}LSys_status_t;
 mdf_err_t light_bri_set(uint8_t bri);
 uint8_t light_bri_get(void);
 mdf_err_t light_power_set(uint8_t power);
int light_bri_user_get(void);
 LSys_status_t light_status_get();

 uint8_t light_power_get(void);
 mdf_err_t light_fade_set(uint32_t fade);

 uint32_t light_fade_get(void);
 mdf_err_t light_devid_set(uint8_t *p_dev_id);

uint8_t *light_devid_get(void);
mdf_err_t light_change_user(int power, int bri, float fade, int dimmer);

mdf_err_t light_change_by_json(const char *p_src);
mdf_err_t light_status_alloc(char **pp_dst);
mdf_err_t  light_status_set(LSys_status_t status);
mdf_err_t light_get_wifi_config( );
mdf_err_t event_device_info_update(void);

void light_led_indicator(void);
void device_loop(void);
void Hue_Loop_Tap_up(char * Url);
void Hue_Loop_Tap_down(char * Url);
void Hue_Loop_exit();
void device_init( );
#endif  // _LIGHT_DEVICE_H

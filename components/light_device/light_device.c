/*
 * @Author: sky
 * @Date: 2020-03-09 18:34:28
 * @LastEditTime: 2021-02-20 09:26:04
 * @LastEditors: Please set LastEditors
 * @Description: ????,????,????
 * @FilePath: \mqtt_example\components\light_device\light_device.c
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

#include "mdf_common.h"
#include "mlink.h"

#include "mconfig_blufi.h"
#include "mconfig_chain.h"

#include "mlink_handle.h"
#include "mesh_utils.h"

#include "mdf_common.h"
#include "mwifi.h"

#include "hue.h"
#include "thincloud.h"
#include "event_queue.h"
#include "utlist.h"
#include "utlis.h"
#include "mesh_dev_table.h"
#include "light_device_config.h"
#include "light_schedule.h"
#include "light_device.h"

#include <tcpip_adapter.h>
#include "mesh_event.h"
#include "light_handle.h"
#define S_KEY_VERSION	("s_version")
#define S_KEY_DEVID	("s_devid")
#define S_KEY_DEVTYPE	("s_devtype")
#define S_KEY_VAMODE	("s_vamode")
#define S_KEY_LEDMODE	("s_ledmode")
#define S_KEY_MINBRI	("s_minbri")

#define _FADE_DIMMER	(6000)	// dimmer fade
#define HUE_LOOP_PERIOD  (400)  //hue loop delay

#define FLASH_KEY_HUB	"f_hue_info"

static const char *TAG          = "light_device";


int x = 0;   //global variable to be checked in hue loop and set in tap up and tap down
static TaskHandle_t hue_loop_handle  = NULL;
char p_hub_conn[128];

/********
** If "Dimmable" then everything works.
** If "NonDimmable" then Fade is 0 and Brightness is either 100 or 0, nothing else.
** If "Remote" then On is always false/Brightness is always 0, and OtherDeviceToControl must have a TCDeviceId. Taps will be sent to that device for processing.
** If "Smart" then On is true/Brightness is always 100, and the taps on that switch will only be to control other lights through the TapSchedule.
**********/
const char *l_dev_type_str[DEVTYPE_MAX]={
	"dimmable",
	"smart",
	"remote",
	"nondimmable",
	NULL
};


typedef struct{
	uint8_t bri;	 // 0 ~ 100
	uint8_t power;   // status
	uint8_t ways_3;
	uint8_t inclvacation;
	uint8_t min_bri;
	uint8_t ledmode;

	uint32_t fade;	// ms
	Dev_type_t type;
	LSys_status_t status;

	int64_t report_tm;

	uint8_t p_dev_id[TC_ID_LENGTH];
}LIGHT_t;

extern const char *mlink_device_get_version();

void _light_type_init(Dev_type_t type);
mdf_err_t light_change_raw(uint8_t *p_power, uint8_t *p_bri, uint32_t *p_fade, uint8_t *p_dimmer_dir);

LIGHT_t light;

mdf_err_t _sys_facotry_reset(void){

	mdf_info_erase("ap_config");
	mdf_info_erase(S_KEY_DEVID);

	return MDF_OK;
}
 mdf_err_t light_min_bri_set(int bri){

	uint8_t m_bri = (uint8_t) ( ( 255 / 100.0) * bri + .5);

	light.min_bri = bri;

	mdf_info_save(S_KEY_MINBRI, (void *)&bri, sizeof( int ) );
	uart_cmd_send(UART_CMD_LIMIT_MIM, &m_bri);
	MDF_LOGD("set min bri %d \n", bri);
	return MDF_OK;
 }
int light_min_bri_get(void){
	return light.min_bri;
}
 mdf_err_t light_inclvacation_set(uint8_t inclvacation){

	light.inclvacation = inclvacation;
	mdf_info_save(S_KEY_VAMODE, (void *)&inclvacation, sizeof( uint8_t ) );
	MDF_LOGD("set inclvacation %u \n", inclvacation);
	return MDF_OK;
 }
  uint8_t light_inclvacation_get(void){
	 return light.inclvacation;
 }

 mdf_err_t light_ledmode_set(uint8_t ledmode){

	light.ledmode = ledmode;
	mdf_info_save(S_KEY_LEDMODE, (void *)&ledmode, sizeof( uint8_t ) );
	MDF_LOGD("set ledmode %u \n", ledmode);
	light_led_indicator();
	return MDF_OK;
 }
  uint8_t light_ledmode_get(void){
	 return light.ledmode;
 }

 mdf_err_t light_bri_set(uint8_t bri){
	light.bri = bri;

	MDF_LOGD("set bri %u \n", bri);
	return MDF_OK;
}
 uint8_t light_bri_get(void){
	return light.bri;
}
int light_bri_user_get(void){
	MDF_LOGD("get bri %u \n", light.bri);

	return (int) ((light.bri * 100.0 ) / 255.0 + .5);
}
 mdf_err_t light_3ways_set(uint8_t ways_3){
	light.ways_3 = ways_3;
	return MDF_OK;
}
 mdf_err_t _light_report_tm_set(int64_t ctime){

	MDF_LOGD("Device status have been change. You need to report the time new.\n");
	light.report_tm =ctime;
	return MDF_OK;
}

 int64_t _light_report_tm_get(void){
	return light.report_tm;
}
 uint8_t light_3ways_get(void){
	return light.ways_3;
}

 mdf_err_t light_power_set(uint8_t power){
	light.power = power;

	MDF_LOGD("set power %u \n", power);
	return MDF_OK;
}

 uint8_t light_power_get(void){
	return light.power;
}

 mdf_err_t light_fade_set(uint32_t fade){
 	MDF_LOGD("set fade %d", fade);
	light.fade = fade;
	return MDF_OK;
}
 uint8_t light_type_get(void){
	return (uint8_t) light.type;
}
void light_type_set(Dev_type_t stype){
	if(stype < DEVTYPE_MAX){
		light.type = stype;
		mdf_info_save( S_KEY_DEVTYPE,  l_dev_type_str[ stype ], strlen( l_dev_type_str[ stype ]) );
		MDF_LOGD("set device type save to %s\n", l_dev_type_str[ stype ]);
		_light_type_init(light.type);

	}
}

uint32_t light_fade_get(void){
	return light.fade;
}

LSys_status_t light_status_get(){
	return light.status;
}
mdf_err_t  light_status_set(LSys_status_t status){
	MDF_PARAM_CHECK( status < LSYS_STATUS_MAX);

	switch(status){
		case LSYS_STATUS_LOST_CNN:
			led_action_set(-1, 1000); //  ???????
			break;
		case LSYS_STATUS_OTAING:
			led_action_set(-1, 300); //  ???????
			break;
		case LSYS_STATUS_CONNECTING:
			led_action_set(-1, 300); //  ???????
			break;
		case LSYS_STATUS_CNN:
			light_led_indicator(); //  ?????
			break;
		case LSYS_STATUS_ONLINE:
			light_led_indicator(); //  ?????
			break;
		default:
			break;
	}

	return MDF_OK;
}


mdf_err_t light_devid_set(uint8_t *p_dev_id){
	MDF_PARAM_CHECK( NULL != p_dev_id);
	MDF_PARAM_CHECK( 0 != strlen((char *)p_dev_id) );

	MDF_LOGD("save deviceid %s\n", p_dev_id);
	memcpy(light.p_dev_id, p_dev_id, TC_ID_LENGTH);
	return mdf_info_save(S_KEY_DEVID,light.p_dev_id, TC_ID_LENGTH);
}

uint8_t *light_devid_get(void){

	if(strlen((char *)light.p_dev_id) == 0 && mdf_info_load(S_KEY_DEVID, light.p_dev_id, TC_ID_LENGTH) != MDF_OK ){
		return NULL;
	}

	return light.p_dev_id;
}
void light_led_indicator(void){
	if(light_ledmode_get() == 0){ //led on when load off
		if(LSYS_STATUS_CNN >= light_status_get() && light_bri_get() > 0 && light_power_get() > 0){
			led_action_set(1,  0);
		}else{
			led_action_set(0,  0);
		}
	} else
	if(light_ledmode_get() == 1){ //led on when load on
		if(LSYS_STATUS_CNN >= light_status_get() && light_bri_get() > 0 && light_power_get() > 0){
			led_action_set(0,  0);
		}else{
			led_action_set(1,  0);
		}
	} else
	if(light_ledmode_get() == 2){ //led on all the time
			led_action_set(1,  0);
	} else
	if(light_ledmode_get() == 3){ //led off all the time
			led_action_set(0,  0);
		}
}
/********
** If "Dimmable" then everything works.
** If "NonDimmable" then Fade is 0 and Brightness is either 100 or 0, nothing else.
** If "Remote" then On is always false/Brightness is always 0, and OtherDeviceToControl must have a TCDeviceId. Taps will be sent to that device for processing.
** If "Smart" then On is true/Brightness is always 100, and the taps on that switch will only be to control other lights through the TapSchedule.
**********/
void _light_type_init(Dev_type_t type){
	uint8_t power = 0, bri = 0, dimmer = 0;
	uint32_t fade =0;

	switch(type){
		case DEVTYPE_Dimmable:
			break;
		case DEVTYPE_Smart:
			bri = 255;
			power = 1;
			fade =0;
			dimmer =0;
			light_change_raw(&power, &bri, &fade, &dimmer);
			break;
		case DEVTYPE_Remote:
			bri = 0;
			power = 0;
			fade =0;
			dimmer =0;

			light_change_raw(&power, &bri, &fade, &dimmer);
			break;
		case DEVTYPE_NonDimmable:
			fade = 0;
			bri = (bri > 0)?255:0;
			dimmer = 0;

			light_change_raw(&power, &bri, &fade, &dimmer);
			break;
		default:
			break;
	}

}
void _light_type_filter(uint8_t *p_power, uint8_t *p_bri, uint32_t *p_fade, uint8_t *p_dimmer){
	uint8_t power = 0, bri = 0, dimmer = 0;
	uint32_t fade =0;

	power = (p_power)?(*p_power):power;
	bri = (p_bri)?(*p_bri):bri;
	fade = (p_fade)?(*p_fade):fade;
	dimmer = (p_dimmer)?(*p_dimmer):dimmer;


	switch(light.type){
		case DEVTYPE_Dimmable:
			break;
		case DEVTYPE_Smart:
			bri = 255;
			power = 1;
			fade =0;
			dimmer =0;
			break;
		case DEVTYPE_Remote:
			bri = 0;
			power = 0;
			fade =0;
			dimmer =0;
			break;
		case DEVTYPE_NonDimmable:
			fade = 0;
			bri = (bri > 0)?255:0;
			dimmer = 0;
			break;
		default:
			break;
	}

	if(p_power)
		*p_power = power;
	if(p_bri)
		*p_bri = bri;
	if(p_fade)
		*p_fade = fade;
	if(p_dimmer)
		*p_dimmer = dimmer;

}
/**** control light status **
** power:
** bri :
** fade :
** dimmer_dir : ==0 dimmmer
** 	HOLD_INC_UP_START = 0X01,
	HOLD_INC_UP_STOP = 0X02,
	HOLD_INC_DOWN_START = 0X11,
	HOLD_INC_DOWN_STOP = 0X12
**
*****/
mdf_err_t light_change_raw(uint8_t *p_power, uint8_t *p_bri, uint32_t *p_fade, uint8_t *p_dimmer_dir){

	mdf_err_t rc = 0;
	uint8_t tmp[16] = {0};
	uint8_t power = -1;
	uint32_t old_fade = 0;
	// todo type
	_light_type_filter(p_power, p_bri, p_fade, p_dimmer_dir);

#ifndef BOARD_NODEMCU
	uint32_t ct = (uint32_t) utils_get_current_time_ms();

	// set fade
	if( p_fade ){
		light_fade_set(*p_fade);
		old_fade = *p_fade;
		if(NULL == p_power && NULL == p_bri)
			uart_cmd_send(UART_CMD_FADE, (void *)&old_fade );
	}
	old_fade = light_fade_get();
	MDF_LOGD("fade = %d ms \n", old_fade);

	// set power
	// set bri
	if(p_power)
		power = *p_power;
	if(   p_bri && power !=0 ){
		tmp[0] = *p_bri;
		memcpy(&tmp[1], &old_fade, sizeof(uint32_t) );
		light_bri_set(*p_bri);
		if(*p_bri ==  0  ){
			light_power_set( 0 );
		}else{
			light_power_set( 1 );
		}

		uart_cmd_send(UART_CMD_BRI, (void *)tmp);
	}else 	if( p_power  ){
		uint8_t bri = (*p_power == 0) ? 0:0xff;
#if 1
		tmp[0] = *p_power;
		memcpy(&tmp[1], &old_fade, sizeof(uint32_t) );
		uart_cmd_send(UART_CMD_STATUS, (void *)tmp);
#endif
#if 0
		tmp[0] = bri;
		memcpy(&tmp[1], &old_fade, sizeof(uint32_t) );
		light_bri_set(bri);
		uart_cmd_send(UART_CMD_BRI, (void *)tmp);
#endif
		light_power_set( *p_power );
	}
		// set dimmer
	if(p_dimmer_dir){
		tmp[0] = *p_dimmer_dir;
		old_fade = _FADE_DIMMER;
		memcpy(&tmp[1], &old_fade, sizeof(uint32_t) );
		uart_cmd_send( UART_CMD_DIMMER, (void *)tmp);
//	check current TapSchedule for Hue, send to hue_loop{} on Start, end the hue_loop upon Stop
	}



#else
	// set power
	if(p_power){
		light_power_set(*p_power );
	}

	if(p_fade){
		light_fade_set(*p_fade );
		}
		// set bri
	if(p_bri){
		light_bri_set(*p_bri );
		if( *p_bri == 0 && 0 != light_power_get( ) ){
			light_power_set( 0 );
		}else if( *p_bri != 0  && 0 == light_power_get( )){
			light_power_set( 1 );
			}
		}
#endif

	return MDF_OK;
}
// bri ? ?????, fade flaot ??
mdf_err_t light_change_user(int power_s, int bri_s, float fade_s, int dimmer_s){

	uint8_t  power = 0, bri = 0,dimmer = 0;
	uint32_t fade = 0, *p_fade = NULL;
	uint8_t *p_power = NULL,  *p_bri = NULL;
	uint8_t *p_dimmer_dir = NULL;


	if(power_s >= 0 ){
		power = (uint8_t)power_s;
		p_power = &power;
		}
	if(bri_s >= 0 ){
		int min_bri = light_min_bri_get();
		float f_bri = ( ( (100.0 - min_bri) * bri_s) / 100.0  + min_bri ) * ( (255.0 )/100.0);
		//bri =( (  (int)(f_bri *10) % 10) > 5 ?((uint8_t)f_bri + 1): (uint8_t)f_bri );
		bri = (uint8_t)f_bri + .5;
		p_bri = &bri;
		MDF_LOGD("bri_s = %d mim bri %d bri %u \n", bri_s, (255 *  min_bri / 100), *p_bri);

		if(p_power == NULL && bri_s == 0 ){
			power = 0;
			p_power = &power;
			}
		}
	if( fade_s >= 0 ){
		fade =(uint32_t)( fade_s * 1000.0);
		p_fade = &fade;
	}
	if( dimmer_s >= 0 ){
		dimmer = (uint8_t) dimmer_s;
		p_dimmer_dir = &dimmer;
		}

	return light_change_raw(p_power, p_bri, p_fade, p_dimmer_dir);
}

/***
*** 1. ?? json ???????
*** ??json ??:
 {"power":1,"timer":"00:00:00","name":"demo","type":"",
  "brightness":50,"fade":1.02,"inclvacation":1,"remote_id":"02-xx","learn":true}

*****/
mdf_err_t light_change_by_json(const char *p_src){
	mdf_err_t ret = MDF_OK;
	int power = -1, bri = -1, dimmer = -1, inclvacation = 0, commssion = 0, min_bri = 27, ledmode = 0;
	float fade = -1;
	char *p_name = NULL, *p_type_str = NULL;
	char time_zone[64] = {0};
	uint8_t p_mac[6] = {0};
	// power.
	ret = mlink_json_parse( p_src, "power", &power);
	if(ret == MDF_OK){
		MDF_LOGD("Set power to %d\n", power);
	}

	// get fade
	ret = mlink_json_parse( p_src, "fade", &fade);
	if(ret == MDF_OK){
		MDF_LOGD("Set fade to %f\n", fade);
	}
	// type
	ret = mlink_json_parse(p_src, "type", &p_type_str);
	if(ret == MDF_OK && p_type_str){
		int i =0;
		for(i=0;i<DEVTYPE_MAX;i++){
			if(!memcmp(l_dev_type_str[i], p_type_str,  MIN(strlen(l_dev_type_str[i]), strlen(p_type_str) ) ) ){
				Evt_mesh_t evt = {0};
				evt.cmd = EVT_SYS_TYPE_SET;
				evt.p_data = MDF_MALLOC(1);
				if(evt.p_data){
					evt.data_len = 1;
					evt.p_data[0] = i;

					MDF_LOGW("Device type is %d\n", i);
					mevt_send(&evt,  10/portTICK_RATE_MS);
				}
				break;
			}
		}
	}
	MDF_FREE(p_type_str);
	// bri.
	ret = mlink_json_parse( p_src, "brightness", &bri);
	if(ret == MDF_OK){
		MDF_LOGD("Set brightness to %d\n", bri);
	}

	ret = mlink_json_parse( p_src, "dimmer", &dimmer);
	if(ret == MDF_OK){
		MDF_LOGD("Set dimmer to %d\n", dimmer);
	}

	ret = mlink_json_parse( p_src, "inclvacation", &inclvacation);
	if(ret == MDF_OK){
		light_inclvacation_set((uint8_t) inclvacation);
		MDF_LOGD("Set inclvacation to %d\n", inclvacation);
	}
	ret = mlink_json_parse( p_src, "ledmode", &ledmode);
	if(ret == MDF_OK){
		light_ledmode_set((uint8_t) ledmode);
		MDF_LOGD("Set ledmode to %d\n", ledmode);
	}
	ret = mlink_json_parse( p_src, "minbrightness", &min_bri);
	if(ret == MDF_OK){
		light_min_bri_set( min_bri);
		MDF_LOGD("Set min bri to %d\n", min_bri);
	}

	light_change_user(power, bri, fade, dimmer);

	// commission
	ret = mlink_json_parse( p_src, "commssion", &commssion);
	if( commssion ){
		esp_wifi_get_mac(ESP_IF_WIFI_STA, p_mac);
		event_make_commission(p_mac);
		MDF_LOGD("make commssion\n");
	}
	// name.
	ret = mlink_json_parse( p_src, "name", &p_name);
	if( p_name ){
		mlink_device_set_name( p_name);
		MDF_LOGD("Set name to %s\n", p_name);
		MDF_FREE( p_name);
		p_name = NULL;
	}

	memset(time_zone, 0, 64);
	if( ESP_OK == mlink_json_parse( p_src, "timezone", &time_zone) && strlen(time_zone) > 0 ){

		MDF_LOGD("timezone: %s", time_zone);
		mdf_info_save( "timezone", time_zone, 64);
		setenv("TZ", time_zone, 1);
		tzset();
	}

	return ret;
}
/***
*** 1. ?? ????? json ??
*** ??json ??:
 {"power":1,"timer":"00:00:00","name":"demo","type":"",
  "brightness":50,"fade":1.02,"inclvacation":1,"remote_id":"02-xx","learn":true}

*****/
mdf_err_t light_status_alloc(char **pp_dst){
	char *p_json = NULL, p_tmp[128] = {0};
    uint8_t self_mac[6]          = {0};
    char tmp_str[64]             = {0}, p_timezone[64] = {0};
	int bri = 0, timezone_len = 0;
	double  fade = light_fade_get() / 1000.0;
	int64_t cur_time = 0;
	float tmp = 0;
	// power.
	//mlink_json_pack(&p_json, "power", (light_power_get() == 0?"false":"true")  );
	mlink_json_pack(&p_json, "power", (int)light_power_get());

	// bri
	tmp = (100.0 * light_bri_get() ) / 255.0 +.5  ;
	tmp = ( (int)tmp >= light_min_bri_get() )?( 100 * ( tmp - light_min_bri_get() )  /( 100 - light_min_bri_get() ) ) : 0;
//	tmp = light_bri_user_get();
	bri = (int) tmp;
	mlink_json_pack( &p_json, "brightness", (int) bri );
	mlink_json_pack( &p_json, "bri_user", light_bri_user_get());
	mlink_json_pack( &p_json, "bri_raw", light_bri_get());

	// fade
	mlink_json_pack_double( &p_json, "fade",  fade );
	// name
	mlink_json_pack( &p_json, "name", mlink_device_get_name());
	// time zone
	 mdf_info_load("timezone", p_timezone, 64);
	if( strlen(p_timezone) > 0 ){
		mlink_json_pack( &p_json, "timezone", p_timezone);
	}

    esp_wifi_get_mac( ESP_IF_WIFI_STA, self_mac);
	mlink_json_pack(&p_json, "_mac", mlink_mac_hex2str(self_mac, tmp_str) );
    mlink_json_pack(&p_json, "_version",  mlink_device_get_version());
    mlink_json_pack(&p_json, "_rssi", mwifi_get_parent_rssi());
    mlink_json_pack(&p_json, "_mesh_layer", esp_mesh_get_layer());

    mlink_json_pack(&p_json, "timer", "todo");
	mlink_json_pack(&p_json, "inclvacation", light_inclvacation_get());
	mlink_json_pack(&p_json, "ledmode", light_ledmode_get());

	mlink_json_pack(&p_json, "minbrightness", light_min_bri_get());

	memset(p_tmp, 0, 128);
	schedule_upate_time_get(p_tmp, 128, _SCH_CMD_ALAM);
	if(strlen( p_tmp) > 0){
		mlink_json_pack(&p_json, "alarmLastUpdated", p_tmp);
	}

	memset(p_tmp, 0, 128);
	schedule_upate_time_get(p_tmp, 128, _SCH_CMD_TAP);
	if(strlen( p_tmp) > 0){
		mlink_json_pack(&p_json, "tapScheduleLastUpdated", p_tmp);
	}

	//mlink_json_pack(&p_json, "learntimezone", "todo");
	memset(p_tmp, 0, 128);
	unix_time2string( ( utils_get_current_time_ms()/ 1000 ), p_tmp, 128);
	if(strlen(p_tmp) > 0){
		mlink_json_pack(&p_json, "current_time", p_tmp);
	}

	cur_time = utils_get_current_time_ms();
	memset(p_tmp, 0, 128);
	sprintf(p_tmp, "%llu", cur_time);
	mlink_json_pack(&p_json, "current_utime", p_tmp);

	// get local ip
	tcpip_adapter_ip_info_t ipInfo;

	// IP address.
	memset(tmp_str, 0, 64);
	tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ipInfo);
	sprintf(tmp_str, IPSTR, IP2STR(&ipInfo.ip));
	//printf("My IP: " IPSTR "\n", IP2STR(&ipInfo.ip));

	mlink_json_pack( &p_json, "local_ip", tmp_str);

	if(l_dev_type_str[light_type_get()])
    	mlink_json_pack(&p_json, "type", l_dev_type_str[light_type_get()]);

	*pp_dst = p_json;

	return MDF_OK;
}

/**
 * @brief   1.Get Mwifi initialization configuration information and Mwifi AP configuration information
 *      through the blufi or network configuration chain.
 */
mdf_err_t _get_network_config(mwifi_init_config_t *init_config, mwifi_config_t *ap_config, uint16_t tid, char *name_prefix)
{
#define DEVICE_MASTER_NETWORK_CONFIG_DURATION_MS    (60000)

    MDF_PARAM_CHECK(init_config);
    MDF_PARAM_CHECK(ap_config);
    MDF_PARAM_CHECK(tid);
    MDF_PARAM_CHECK(name_prefix);

    mconfig_data_t *mconfig_data        = NULL;
    mconfig_blufi_config_t blufi_config = {
        .company_id = MCOMMON_ESPRESSIF_ID, /**< Espressif Incorporated */
        .tid        = tid,
    };

    /**
     * @brief Network configuration chain slave initialization for obtaining network configuration information from master.
     */
    MDF_ERROR_ASSERT(mconfig_chain_slave_init());

    sprintf(blufi_config.name, "%s", name_prefix);
    MDF_LOGI("BLE name: %s", blufi_config.name);

    /**
     * @brief Initialize Bluetooth network configuration
     */
    MDF_ERROR_ASSERT(mconfig_blufi_init(&blufi_config));

    /**
     * @brief Get Network configuration information from blufi or network configuration chain.
     *      When blufi or network configuration chain complete, will send configuration information to config_queue.
     */
    MDF_ERROR_ASSERT(mconfig_queue_read(&mconfig_data, portMAX_DELAY));

    /**
     * @brief Deinitialize Bluetooth network configuration and Network configuration chain.
     */
    MDF_ERROR_ASSERT(mconfig_chain_slave_deinit());
    MDF_ERROR_ASSERT(mconfig_blufi_deinit());

    memcpy(ap_config, &mconfig_data->config, sizeof(mwifi_config_t));
    memcpy(init_config, &mconfig_data->init_config, sizeof(mwifi_init_config_t));

    /**
     * @brief Switch to network configuration chain master mode to configure the network for other devices(slave), according to the white list.
     */
    if (mconfig_data->whitelist_size > 0) {
        for (int i = 0; i < mconfig_data->whitelist_size / sizeof(mconfig_whitelist_t); ++i) {
            MDF_LOGD("count: %d, data: " MACSTR,
                     i, MAC2STR((uint8_t *)mconfig_data->whitelist_data + i * sizeof(mconfig_whitelist_t)));
        }

        MDF_ERROR_ASSERT(mconfig_chain_master(mconfig_data, DEVICE_MASTER_NETWORK_CONFIG_DURATION_MS / portTICK_RATE_MS));
    }

    MDF_FREE(mconfig_data);

    return MDF_OK;
}

/**
**  ?????????? ????? wifi_init ? wifi ?????.
 * @brief	1.Get Mwifi initialization configuration information and Mwifi AP configuration information from nvs flash.
 *			2.If there is no network configuration information in the nvs flash,
 *				obtain the network configuration information through the blufi or mconfig chain.
 *			3.Indicate the status of the device by means of a light
 */
mdf_err_t light_get_wifi_config(){

    mwifi_init_config_t init_config   = MWIFI_INIT_CONFIG_DEFAULT();

	mwifi_config_t ap_config     = {
        //.router_ssid     = "showhome_office",//"showdow",//CONFIG_ROUTER_SSID,
       	//.router_password = "showhome",//"!@#sky141516",//CONFIG_ROUTER_PASSWORD,
        .mesh_id         = CONFIG_MESH_ID,
        .mesh_password   = CONFIG_MESH_PASSWORD,
    };

#if 1
	if ( mdf_info_load("ap_config",  &ap_config, sizeof(mwifi_config_t)) != MDF_OK ) {
		    uint8_t self_mac[6]          = {0};
			uint8_t p_name[64]              = {0};
			esp_wifi_get_mac(ESP_IF_WIFI_STA, self_mac);
			sprintf((char *)p_name, _PY_ID_FORMAT, PR_MAC2STR(self_mac) );
			light_status_set( LSYS_STATUS_CONNECTING );

			MDF_ERROR_ASSERT( _get_network_config( &init_config, &ap_config, MWIFI_ID, (char *)p_name)  );

			MDF_LOGI("mconfig, ssid: %s, password: %s, mesh_id: " MACSTR,
					 ap_config.router_ssid, ap_config.router_password,
					 MAC2STR( ap_config.mesh_id ));

			MDF_LOGD("Star Ble, please open mesh-app\n");
			/**
			   * @brief Save configuration information to nvs flash.
			   */


			mdf_info_save("ap_config", &ap_config, sizeof(mwifi_config_t));
		}
#endif
	memset(ap_config.mesh_id, 0, 6);
	memset(ap_config.mesh_password, 0, 64);

	memcpy(ap_config.mesh_id, CONFIG_MESH_ID, sizeof(CONFIG_MESH_ID));
	memcpy(ap_config.mesh_password, CONFIG_MESH_PASSWORD, sizeof(CONFIG_MESH_PASSWORD));

	MDF_LOGI(MACSTR"|[%s] password: %s, mesh_id: " MACSTR,
				 MAC2STR(CONFIG_MESH_ID), CONFIG_MESH_PASSWORD,ap_config.mesh_password, MAC2STR( ap_config.mesh_id ));

    /**
     * @brief Note that once BT controller memory is released, the process cannot be reversed.
     *        It means you can not use the bluetooth mode which you have released by this function.
     *        it can release the .bss, .data and other section to heap
     */
     esp_bt_mem_release(ESP_BT_MODE_CLASSIC_BT);

	 MDF_ERROR_ASSERT( mwifi_init(&init_config));
	 MDF_ERROR_ASSERT( mwifi_set_config(&ap_config));
	 MDF_ERROR_ASSERT( mwifi_start() );

	return MDF_OK;
}

/*******
** Will report device's time after 700ms
*******************/
static mdf_err_t _light_report_countdown(int64_t ctime){
	mdf_err_t rc = 0;

	int64_t r_tm = _light_report_tm_get();
	if( r_tm > 0 && DIFF( r_tm, ctime) > 700 ){

		Evt_mesh_t evt = {0};

		evt.cmd = MEVT_TC_INFO_REPORT;
		memcpy(evt.p_devid , light.p_dev_id, TC_ID_LENGTH );

		MDF_LOGD("Try to report device's status\n");

		rc = mevt_send(&evt, 100/portTICK_RATE_MS);
		if( MDF_OK != rc)
			MDF_LOGW("Failt to send event MEVT_TC_INFO_REPORT \n");
		_light_report_tm_set(0);
	}

	return rc;
}

static mdf_err_t _light_store_load(void){

	mdf_err_t rc = 0;
	int type_len = 0, ver_len = 0;
	char *p_type_str = NULL, *p_ver = NULL;

	char *p_s_ver = (char *)mlink_device_get_version();

	// ????????? ??? ???? wifi ???
	p_ver = utlis_info_load(S_KEY_VERSION, &ver_len);
	if( NULL == p_ver){
		_sys_facotry_reset();
		if(p_s_ver)
			mdf_info_save(S_KEY_VERSION, p_s_ver, strlen( p_s_ver ) );

	}

	MDF_FREE(p_ver);

	rc = mdf_info_load(S_KEY_DEVID, light.p_dev_id, TC_ID_LENGTH);
	p_type_str = (char *)utlis_info_load(S_KEY_DEVTYPE, &type_len);
	if( p_type_str ){
		int i =0;
		for(i=0;i<DEVTYPE_MAX;i++){
			if(!memcmp(l_dev_type_str[i], p_type_str,  MIN(strlen(l_dev_type_str[i]), strlen(p_type_str) ) ) ){
				light.type = i;
				MDF_LOGD("Device type is %d\n", light.type);
			}
		}
	}else{
		mdf_info_save( S_KEY_DEVTYPE,  l_dev_type_str[DEVTYPE_Dimmable], strlen( l_dev_type_str[DEVTYPE_Dimmable]) );
		light.type = DEVTYPE_Dimmable;
	}
	_light_type_init(light.type);
	// inclvacation

	rc = mdf_info_load( S_KEY_VAMODE, &light.inclvacation, sizeof(uint8_t));
	if( ESP_OK != rc){
		light_inclvacation_set(0);
	}

	// ledmode

	rc = mdf_info_load( S_KEY_LEDMODE, &light.ledmode, sizeof(uint8_t));
	if( ESP_OK != rc){
		light_ledmode_set(0);
	}

	// set min brigness
	rc = mdf_info_load( S_KEY_MINBRI, &light.min_bri, sizeof(int));
	if( ESP_OK != rc){
		light_min_bri_set(27);
	}
	MDF_LOGD("Device type is %d\n", light.type);
	return rc;
}

mdf_err_t event_device_info_update(void){
	Evt_mesh_t evt = {0};

	evt.cmd = MEVT_TC_INFO_REPORT;
	memcpy(evt.p_devid , light.p_dev_id, TC_ID_LENGTH );

	MDF_LOGD("Try to report device's status\n");

	return  mevt_send(&evt, 100/portTICK_RATE_MS);
}
/**** device event handle start ****************/
static mdf_err_t _event_handle_sys_wifi_rest(Evt_mesh_t *p_evt)
{
	MDF_LOGW("Restart wifi\n");
	light_led_indicator();
	mdf_info_erase("ap_config");
	return MDF_OK;
}
static mdf_err_t _event_handle_sys_factory_rest(Evt_mesh_t *p_evt)
{
	MDF_LOGW("_event_handle_sys_factory_rest \n");
	_sys_facotry_reset();
	esp_restart();

	return MDF_OK;
}
static mdf_err_t _event_handle_button_tu(Evt_mesh_t *p_evt)
{

	// todo SET DEFAULT FADE HERE if no tap schedule set
	//light_change_user(1, 100, 1.2, -1);
	if(-1 == tap_event_active(_SCH_TAP_TU, -1) )
		light_change_user(1, 100, 0, -1);
		//light_change_user(1, 100, 1.2, -1);
	MDF_LOGW("_event_handle_button_tu\n");
	return MDF_OK;
}
static mdf_err_t _event_handle_button_td(Evt_mesh_t *p_evt)
{

	//light_change_user(0, 0, 1.2, -1);
	MDF_LOGW("_event_handle_button_td \n");

	if( -1 == tap_event_active(_SCH_TAP_TD, -1) )
		light_change_user(0, 0, 0, -1);
		//light_change_user(0, 5, 1.2, -1);
	return MDF_OK;
}
static mdf_err_t _event_handle_button_dtu(Evt_mesh_t *p_evt)
{
	// todo
	//light_change_user(1, 100, 0, -1);

	if( -1 ==  tap_event_active(_SCH_TAP_DTU, -1))
		light_change_user(1, 100, 0, -1);
	MDF_LOGW(" _event_handle_button_dtu \n");
	return MDF_OK;
}
static mdf_err_t _event_handle_button_dtd(Evt_mesh_t *p_evt)
{

	//light_change_user(0, 0, 0, -1);
	if( -1 == tap_event_active(_SCH_TAP_DTD, -1) )
		light_change_user(0, 0, 0, -1);
	MDF_LOGW("_event_handle_button_dtd \n");
	return MDF_OK;
}

char *p_url= NULL, *p_url_n = NULL;
int briVal=0,z = 0;

/*****

Function To Get Current Brightness Value

*****/

void Get_Bri_value()
{
	char *response = NULL, *url_2 = NULL, *p_json = NULL;
	int briChange = (((254/( _FADE_DIMMER /HUE_LOOP_PERIOD))*1) + .5);
	int ctChange = ((((500-153)/( _FADE_DIMMER /HUE_LOOP_PERIOD))*1) + .5);
	int transitionTime = (((( _FADE_DIMMER / HUE_LOOP_PERIOD)*3)/10) + .5);	

	if(z == 0)
	{
		int url_len = strlen(p_url) + strlen(p_hub_conn); 
		url_2 = MDF_MALLOC( url_len + 1);
		if(url_2){
		bzero(url_2, url_len + 1);
		sprintf(url_2,"%s%s",p_hub_conn, p_url);
		// MDF_LOGW("url %s \n", url);
	}
	}
	else
	{
		int url_len = strlen(p_url_n) + strlen(p_hub_conn);
		url_2 = MDF_MALLOC( url_len + 1);
		if(url_2){
		bzero(url_2, url_len + 1);
		sprintf(url_2,"%s%s",p_hub_conn, p_url_n);
		// MDF_LOGW("url %s \n", url_2);
	}
	}
	
	if (x==1){    																//x=1 for Press Up x=2 for Press Down  
		mlink_json_pack(&p_json, "on", true);
		mlink_json_pack(&p_json, "bri_inc", briChange); 
		mlink_json_pack(&p_json, "transitiontime", transitionTime);
	}
	else
	{
		mlink_json_pack(&p_json, "bri_inc", -briChange); 
		mlink_json_pack(&p_json, "transitiontime", transitionTime);
	}

	if(esp_mesh_is_root())
	{
		// MDF_LOGI("This device is root, perfoming http_perform_as_stream_reader \n");
		if( 0 == http_perform_as_stream_reader(url_2, 2, p_json, &response)){ 
		MDF_LOGW("hue send http rq failt !!\n");
		}
		MDF_LOGE("recv %s \n", response);
	}
	else
	{
		response ="[]";
	}

	char *ret;
	if(strlen(response) > 0)
	{
		ret = strstr(response, "rror");              //If Response Contains Error , Start With Brightness=0
	    if (ret)
	    {
	    	briVal = 0;
	    }
	    else
	    {
			int a;
			
			//Extract Brightness From Response
			if(x == 1)
			{
				sscanf(response,"%*[^0123456789]%d%*[^0123456789]%d%*[^0123456789]%d%*[^0123456789]%d%*[^0123456789]%d",&a,&a,&a,&a,&briVal);
			}
			else
			{
				sscanf(response,"%*[^0123456789]%d%*[^0123456789]%d%*[^0123456789]%d%*[^0123456789]%d",&a,&a,&a,&briVal);
			}
			MDF_LOGI("Bri %d \n",briVal);
	    }
	}
	else
	{
		briVal = 0;
	}
	
	
	MDF_FREE(url_2);
	url_2 = NULL;
	MDF_FREE(p_json);
	p_json = NULL;
}




char *p_json=NULL, *url=NULL;
int y = 0;
int Running = 0, loop_exit = 0;

void BodyPackJson(int x,int A,int B, int tt)
{
	if(x == 1)
	{
		mlink_json_pack(&p_json, "on", A);
		mlink_json_pack(&p_json, "bri", B); 
		mlink_json_pack(&p_json, "ct", 500);
		mlink_json_pack(&p_json, "transitiontime", tt);
	}
	else if(x == 2)
	{
		mlink_json_pack(&p_json, "bri_inc", A);
		mlink_json_pack(&p_json, "ct_inc", B);
		mlink_json_pack(&p_json, "transitiontime", tt);
	}
	//return p_json;
	
}
/*****

Hue Loop

*****/

void hue_loop_task(void *arg)
{
	int k = 0;
	int briChange,ctChange,transitionTime,brightness=0;
	int loopExclHueResponseTime = 1;
	if(esp_mesh_is_root())
	{
		if (HUE_LOOP_PERIOD - 300 >= 1)
		{
		loopExclHueResponseTime = (HUE_LOOP_PERIOD-300);
		}
	}
	else
	{
		loopExclHueResponseTime = (HUE_LOOP_PERIOD);
	}
	
	const TickType_t xDelay = loopExclHueResponseTime / portTICK_PERIOD_MS;
	while(1)
	{
		
		if(y == 0)															
		{
			briChange = (((254/( _FADE_DIMMER /HUE_LOOP_PERIOD))*1) + .5);
			ctChange = ((((500-153)/( _FADE_DIMMER /HUE_LOOP_PERIOD))*1) + .5);
			transitionTime = (((( _FADE_DIMMER / HUE_LOOP_PERIOD)*3)/10) + .5);	
			brightness = briVal;
			if(z == 0)
			{
				int url_len = strlen(p_url) + strlen(p_hub_conn);
				url = MDF_MALLOC( url_len + 1);
				if(url)
				{
				bzero(url, url_len + 1);
				sprintf(url,"%s%s",p_hub_conn, p_url);
				// MDF_LOGW("url %s \n", url);
				}
			}
			else
			{
				int url_len = strlen(p_url_n) + strlen(p_hub_conn);
				url = MDF_MALLOC( url_len + 1);
				if(url)
				{
				bzero(url, url_len + 1);
				sprintf(url,"%s%s",p_hub_conn, p_url_n);
				// MDF_LOGW("url %s \n", url);
				}
			}
			
			y=1;
		}

		if(x == 1)																//x=1 for Press Up x=2 for Press Down
		{
			// MDF_LOGI("Brightness %d",brightness);
			brightness = brightness + briChange;
			
			// MDF_LOGI("Brightness after increment %d",brightness);
			if(y == 1)
			{
				int bri = ((brightness*100)/254);         
				// mlink_json_pack(&p_json, "on", true);
				// mlink_json_pack(&p_json, "bri", bri); 
				// mlink_json_pack(&p_json, "ct", 500)
				// mlink_json_pack(&p_json, "transitiontime", transitionTime);
				BodyPackJson(1,1,bri,transitionTime);
				
				y=2;
			}
			else if(y == 2)
			{
				// mlink_json_pack(&p_json, "bri_inc", briChange);
				// mlink_json_pack(&p_json, "ct_inc", -ctChange);
				// mlink_json_pack(&p_json, "transitiontime", transitionTime);
				BodyPackJson(2,briChange,-ctChange,transitionTime);
			}
			if(brightness >= 254)
			{
				brightness = 254;
			}
		}
		else if(x == 2)
		{
			// MDF_LOGI("Brightness %d",brightness);
			if(briVal > 0)
			{
			brightness = brightness - briChange;
			// MDF_LOGI("Brightness after decrement %d",brightness);

			if(brightness <= 0)
			{
				if(k < 3) 									   //Gives User Time to Stay on Minimum Brightness
				{
					// mlink_json_pack(&p_json, "on", true);
					// mlink_json_pack(&p_json, "bri", 1);        //min bri = 1
					// mlink_json_pack(&p_json, "ct", 500);	   //Ct will be calculated according to bri in change hue
					// mlink_json_pack(&p_json, "transitiontime", transitionTime);
					BodyPackJson(1,1,1,transitionTime);
					brightness = 1;
					k++;
				}
				else
				{
					// mlink_json_pack(&p_json, "on", false);
					// mlink_json_pack(&p_json, "bri", 0);        //off
					// mlink_json_pack(&p_json, "ct", 500);	   //Ct will be calculated according to bri in change hue
					// mlink_json_pack(&p_json, "transitiontime", transitionTime);
					BodyPackJson(1,0,0,transitionTime);
					brightness = 0;
				}
				
			}
			else
			{
				if(y == 1)
				{
					int bri = ((brightness*100)/254);         
					// mlink_json_pack(&p_json, "bri", bri); 
					// mlink_json_pack(&p_json, "ct", 500);	  //Ct will be calculated according to bri in change hue	
					// mlink_json_pack(&p_json, "transitiontime", transitionTime);
					BodyPackJson(1,1,bri,transitionTime);
					y=2;
				}
				else if(y == 2)
				{
					// mlink_json_pack(&p_json, "bri_inc", -briChange);
					// mlink_json_pack(&p_json, "ct_inc", ctChange);
					// mlink_json_pack(&p_json, "transitiontime", transitionTime);
					BodyPackJson(2,-briChange,ctChange,transitionTime);
				}		
			}
			}
			else
			{
				// mlink_json_pack(&p_json, "on", false);
				// mlink_json_pack(&p_json, "bri", 0);          //off
				// mlink_json_pack(&p_json, "ct", 500);		 //Ct will be calculated according to bri in change hue
				// mlink_json_pack(&p_json, "transitiontime", transitionTime);
				BodyPackJson(1,0,0,transitionTime);
				brightness = 0;
			}


		}
		
		//performing Http Request
		http_perform_as_stream_reader(url,2,p_json,NULL);
		
		if((brightness == 0) || (brightness == 254) || (loop_exit == 1))      //Hue Loop Exit, if Bightness Minimum, Maximum Or Manual Release of button
		{
			// int ExitReason=0;
			// if(brightness==0)
			// {
				// ExitReason = 1;
			// }
			// else if(brightness == 254)
			// {
				// ExitReason = 2;
			// }
			// else if(loop_exit == 1)
			// {
				// ExitReason = 3;
			// }
			 MDF_LOGW("Exit From Hue Loop \n");
			// MDF_LOGW("Reason : %d  \n",ExitReason);
			loop_exit = 0;
			Running = 0;
			k = 0;
			if(z == 1)
			{
				MDF_FREE(p_url_n);
				p_url_n = NULL;
			}
			MDF_FREE(p_json);
			p_json =NULL;
			MDF_FREE(url);
			url =NULL;
			vTaskSuspend(NULL);
		}

		MDF_FREE(p_json);
		p_json =NULL;
		vTaskDelay(xDelay);
	}

}


/*****

Function To Start Hue Loop with Press Up (Requested From Node)

*****/
void Hue_Loop_Tap_up(char * Url)
{
	int url_len = strlen(Url);
	p_url_n = MDF_MALLOC( url_len + 1);
	strcpy(p_url_n,Url);
	x = 1;
	y = 0;
	z = 1;
	Get_Bri_value();
	if(Running == 0)
	{
		Running = 1;
		vTaskResume(hue_loop_handle);
	}
}

/***************************************************************

Function To Start Hue Loop with Press Down (Requested From Node)

****************************************************************/

void Hue_Loop_Tap_down(char * Url)
{
	int url_len = strlen(Url);
	p_url_n = MDF_MALLOC( url_len + 1);
	strcpy(p_url_n,Url);
	x = 2;
	y = 0;
	z = 1;
	Get_Bri_value();
	if(Running == 0)
	{
		Running = 1;
		vTaskResume(hue_loop_handle);
	}
	
}
/**********************************************

Function To Stop Hue Loop (Requested From Node)

***********************************************/
void Hue_Loop_exit()
{
	if(Running == 1)
	{
		loop_exit = 1;
	}
	MDF_LOGW("hold stop uart  send \n");
	
	
}

static mdf_err_t _event_handle_button_hold_start_up(Evt_mesh_t *p_evt)
{
	if(p_evt->p_data && p_evt->data_len > 0){
		if(0 == hue_loop_get_data(_SCH_TAP_TU, &p_url))					//Proceed Only If HueUrl != NULL
		{
			MDF_LOGE("p_url  %s\n",p_url); 								
			x = 1;														//x=1 for Press Up x=2 for Press Down
			y = 0;
			MDF_LOGW("hold START uart  send PRESS UP \n");
			if(esp_mesh_is_root())										//Hue Loop Can Only Run on Root
			{
				z = 0;
				Get_Bri_value();
				if(Running == 0)										//Check if Hue Loop Running already	
				{
					Running = 1;										//Flag to indicate Hue Loop  is Running or Not
					vTaskResume(hue_loop_handle);
				}
			}
			else
			{
				send_node_press("start","up",p_url);   					//Send Press Up to Root if Current Device is Node
			}
		}
		else
		{
			MDF_LOGE("Error getting p_url");
		}
	light_change_raw(NULL, NULL, NULL, p_evt->p_data);
	}
	return MDF_OK;
}
static mdf_err_t _event_handle_button_hold_start_down(Evt_mesh_t *p_evt)
{

	if(p_evt->p_data && p_evt->data_len > 0){
	if(0 == hue_loop_get_data(_SCH_TAP_TU, &p_url))						//Proceed Only If HueUrl != NULL
	{
		MDF_LOGE("p_url  %s\n",p_url);
		x = 2;      													//x=1 for Press Up x=2 for Press Down
		y = 0;
		MDF_LOGW("hold START uart  send PRESS DOWN \n");
		if(esp_mesh_is_root())											//Hue Loop Can Only Run on Root
		{
			z = 0;
			Get_Bri_value();
			if(Running == 0)											//Check if Hue Loop Running already	
			{
				Running = 1;        									//Flag to indicate Hue Loop  is Running or Not
				vTaskResume(hue_loop_handle);
			}
		}
		else
		{
			send_node_press("start","down",p_url);     					//Send Press Down to Root if Current Device is Node
		}
	}
	else
	{
		MDF_LOGE("Error getting p_url");
	}

	light_change_raw(NULL, NULL, NULL, p_evt->p_data);
	}
	return MDF_OK;
}

static mdf_err_t _event_handle_button_hold_stop(Evt_mesh_t *p_evt)
{
	if( p_evt->p_data && p_evt->data_len > 0)
		light_change_raw(NULL, NULL, NULL, p_evt->p_data);
	
	MDF_LOGW("hold stop uart  send \n");

	if(esp_mesh_is_root())                                 //Check If Device is Root, as Hue Loop Runs Only On Root
	{
		if(Running == 1)
		{
			loop_exit = 1;
		}
	}
	else
	{
		send_node_press("stop","release","");              //Send Command to Stop The Hue Loop From Node To ROOT
	}
	
	return MDF_OK;
}
static mdf_err_t _event_dev_type_set(Evt_mesh_t *p_evt)
{

	if(p_evt->p_data && p_evt->data_len == 1){
		Dev_type_t stype =  p_evt->p_data[0];

		MDF_LOGW("set device type %d \n", p_evt->p_data[0]);
		light_type_set( (Dev_type_t) p_evt->p_data[0] );
	}
	return MDF_OK;
}

static mdf_err_t _event_handle_uart_cmd(Evt_mesh_t *p_evt)
{
	mdf_err_t rc = 0;
	int need_report =0;
	Uart_cmd rec_cmd = UART_CMD_MAX;

	MDF_PARAM_CHECK(p_evt->p_data);
	MDF_PARAM_CHECK(p_evt->data_len > 0);

	rec_cmd = p_evt->p_data[0];

	if(rec_cmd < UART_CMD_MAX){

		need_report = 1;

		switch(rec_cmd){

			case UART_CMD_BRI:
				light_bri_set( p_evt->p_data[1]);
				#if 0
				if( p_evt->data_len >= 5 ){
					uint32_t fade = 0;
					memcpy(&fade, &p_evt->p_data[2], 4);
					light_fade_set( fade);
				}
				#endif
				light_led_indicator();
				break;

			case UART_CMD_STATUS:
				light_power_set( p_evt->p_data[1] );
				light_led_indicator();
				#if 0
				if( p_evt->data_len >= 5 ){
					uint32_t fade = 0;
					memcpy(&fade, &p_evt->p_data[2], 4);
					light_fade_set( fade);
				}
				#endif
				break;

			case UART_CMD_FADE:
				if( p_evt->data_len >= 4 ){
				uint32_t fade = 0;
				uint32_t *p_fade = &fade;
				p_fade[0] = p_evt->p_data[1];
				p_fade[1] = p_evt->p_data[2];
				p_fade[2] = p_evt->p_data[3];
				p_fade[3] = p_evt->p_data[4];

				MDF_LOGD("get fade %u \n", fade);
				light_fade_set(fade );
			}
				break;

			case UART_CMD_3WAY:
				light_3ways_set(p_evt->p_data[1]);
				break;
			default:

				need_report = 0;

				break;
		}
		if( need_report > 0)
			_light_report_tm_set( utils_get_current_time_ms() );
	}

	MDF_LOGW("handle uart cmd \n");
	return MDF_OK;
}

void _device_event_register(void){

 	mevt_handle_func_register( _event_handle_sys_wifi_rest, EVT_SYS_WIFI_RESET );
	mevt_handle_func_register( _event_handle_sys_factory_rest, EVT_SYS_FACTORY_REST );
	mevt_handle_func_register( _event_handle_button_tu, EVT_BUTTON_TU );
	mevt_handle_func_register( _event_handle_button_td, EVT_BUTTON_TD );
	mevt_handle_func_register( _event_handle_button_dtu, EVT_BUTTON_DTU );
	mevt_handle_func_register( _event_handle_button_dtd, EVT_BUTTON_DTD );
	mevt_handle_func_register( _event_handle_button_hold_start_up, EVT_BUTTON_HOLD_START_UP );
	mevt_handle_func_register( _event_handle_button_hold_start_down, EVT_BUTTON_HOLD_START_DOWN );
	mevt_handle_func_register( _event_handle_button_hold_start_up, EVT_BUTTON_HOLD_START_DUP );
	mevt_handle_func_register( _event_handle_button_hold_start_down, EVT_BUTTON_HOLD_START_DDOWN );
	mevt_handle_func_register( _event_handle_button_hold_stop, EVT_BUTTON_HOLD_STOP );

	mevt_handle_func_register( _event_dev_type_set, EVT_SYS_TYPE_SET );

	mevt_handle_func_register( _event_handle_uart_cmd, EVT_UART_CMD );

}
/**** device event handle end****************/
void device_loop(void){
	int64_t ctime = utils_get_current_time_ms();

	button_loop(ctime);

	uart_recv_handle();
	_light_report_countdown(ctime);
}

void device_init(){

    esp_log_level_set(TAG, ESP_LOG_INFO);
	_uart_init();
	_light_store_load();
	button_init();
	_device_event_register();
	light_status_set( LSYS_STATUS_LOST_CNN );


	mdf_info_load(FLASH_KEY_HUB, p_hub_conn,sizeof(p_hub_conn));

	xTaskCreatePinnedToCore(hue_loop_task, "hue_loop", 6 * 1024,NULL, CONFIG_MDF_TASK_DEFAULT_PRIOTY, &hue_loop_handle, 1);
	vTaskSuspend(hue_loop_handle);

	MDF_LOGD("Device id : %s\n", light.p_dev_id);
	MDF_LOGE("IN device_init");

}






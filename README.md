<!--
 * @Author: Micheal
 * @Date: 2020-02-22 15:40:21
 * @LastEditTime: 2021-02-04 21:26:07
 * @LastEditors: Please set LastEditors
 * @Description: In User Settings Edit
 * @FilePath: \mqtt_example\README.md
-->
[[source code ]](./main)

# Dimmer Sdk

## Hardware requirements

Nodemcu(esp32) board or Dimmer PCB and USB to TTL Serial Converter Adapter Module.

## Build the compilation environment
 As we use ESP-IDF of version v3.2.2, so please read [ESP-IDF guides](https://docs.espressif.com/projects/esp-idf/en/v3.2.2/get-started/index.html#) and [ESP-MDF](https://docs.espressif.com/projects/esp-mdf/en/latest/get-started/)  to build you compilation environment.
 
    notice: ESP-IDF is the Freertos for esp32 and ESP-MDF is the mesh sdk for esp32.

## source file overview
```
 -components
    |--- button       ; Key processing, including long press and short press 
    |--- light_device ; All the Dimmer function incloud factoroy rest and command respond from the mesh.
    |--- thincloud    ; Communication with tc cloud
    |--- utilis       ; Functions for small functions like byte print and malloc.
    |--- mesh_mqtt_handle ; Esp mqtt, But we use Aws mqtt, so we haven't use any of this components.
 - mqtt_example.c     ; The main function. 

```
## Compile and debug step by step 

###  set you serial port 
In the sdk_path/sdkconfig file and change this to you fash and debug port.
```
CONFIG_ESPTOOLPY_PORT="COM4"
```
### Buring and monitor 
```shell
cd  sdk_path
make flash monitor 
```
    "make flash monitor" will flash the firmware to the esp32 and open serial port to print the log.

### Source code framework description

1) Funtion `light_device_init` will init all the device's function after that will start `app_loop` task which will handle all the event like button press/schedule update/command handle and respond from Tc.

2) Once the mesh is build `event_loop_cb` function will handle wifi and mesh event. When we get `MDF_EVENT_MWIFI_ROOT_GOT_IP` event, it mean esp32 have connect to router and get ip, we will call `tc_client_creat` function to connnect TC.  More infomation please [refer to esp doc](https://docs.espressif.com/projects/esp-mdf/en/latest/api-reference/mwifi/index.html)
3) In file `event_queue.c`, we support a queue. we can use function `mevt_handle_func_register` to register an function to handle it. Like file `light_device.c` function `_device_event_register`, have register event wifi reset, TU/TD/DTU/DTD. When button detected  `TU`, just trigger `EVT_BUTTON_TU`. `app_loop` function will call `_event_handle_button_tu` to handle `TU`.

```c
void _device_event_register(void){

 	mevt_handle_func_register( _event_handle_sys_wifi_rest, EVT_SYS_WIFI_RESET );	
	mevt_handle_func_register( _event_handle_sys_factory_rest, EVT_SYS_FACTORY_REST );	
	mevt_handle_func_register( _event_handle_button_tu, EVT_BUTTON_TU );	
	mevt_handle_func_register( _event_handle_button_td, EVT_BUTTON_TD );	
	mevt_handle_func_register( _event_handle_button_dtu, EVT_BUTTON_DTU );
	mevt_handle_func_register( _event_handle_button_dtd, EVT_BUTTON_DTD );
	mevt_handle_func_register( _event_handle_button_hold_start, EVT_BUTTON_HOLD_START );
	mevt_handle_func_register( _event_handle_button_hold_stop, EVT_BUTTON_HOLD_STOP );
	
	mevt_handle_func_register( _event_dev_type_set, EVT_SYS_TYPE_SET );	
	
	mevt_handle_func_register( _event_handle_uart_cmd, EVT_UART_CMD );	

}
```

### MQTT package receive and respond
1) In file `mesh_thincloud.c`: function `tc_sub_device_command` will subscribe all the topic from tc and register an callback function 

```c
rc =  aws_iot_mqtt_subscribe(   tc.p_mclient, p_tab->p_topic_sub_cmd, strlen(p_tab->p_topic_sub_cmd),\
										QOS0,  _tc_sub_dev_cmd_cb, NULL);
```
All the notify from Tc will receive and handle in function `_tc_sub_dev_cmd_cb` which will check the list( mesh list that incloud all the node's mac ) and send a json to mesh in functiton `_handle_event_command` like 

```json
	{
	  "request":"update_state",//  tc command
	  "deviceId":"xxx",    
	  "cid": "98d67f00-ae7d-4830-9e09-8a2767760cad",
	  "data": {"power":1,"timer":"00:00:00","name":"demo","type":"","brightness":50,"fade":1.02,"vacationmode":1,"remote_id":"02-xx","learn":true}
	}
```

   In file `light_handle.c`, `_mlink_handle_function_register` function have register a function `_mlink_update_state` to handle mesh package `"request":"update_state"`, so `_mlink_update_state` will handle the Tc command  `update_state` and creat an respond package to the root which will relay to the TC.

```c 
static void _mlink_handle_function_register(void){
    ...
  MDF_ERROR_ASSERT(mlink_set_handle("update_state", _mlink_update_state));
  ...
}
```

#### schedule

File `light_schedule.c` contains all the functions of schedule and alarm.
Schedule and Alarm was conver to `Sch_t` and save in flash.   
Device will return an schedule id, which was creat by the effective time, so if there are two schedule have the same active time, device will return the same id, and the new schedule will cover the old schedule.
Function `_sch_loop_detect` will constantly judge whether to switch to a new schedule and trigger an alarm. Array `next_secend` always stores the time of the next schedule and alarm. `_sch_active_` function will active the new schedle  once the time is come and read flash to update array `next_secend` to the next time, so that we only need to read the flash once, which was very time consuming.

```c
  typedef struct SCHEDULE_HAD_T{
	uint64_t time_id; // 0123456786400 =  1234567(week) + 86400(time in seconds) + 01 (00  alarm)，11(tu) 12(td)13(dtu) ..
	uint8_t bri;
	uint8_t self;
	uint16_t fade;
	uint16_t coutdown_tm;
	uint8_t p_id[16]; 
}Schedule_had_t;

  typedef struct SCHEDULE_T{
	Schedule_had_t had;
	uint16_t data_len;
	uint8_t *p_data;
}Sch_t;
  
```

### Sample: config dimmer fade
Dimmer fade( Hold the buttom to dimmer the light ) was Macro definition in file `light_device.c` which value cann't be change.
If we want to change the dimmer fade in `update_state` command, we can follow the steps below to modify.
In file `light_device.c`:
```c
#define _FADE_DIMMER	(10000)	// dimmer fade
change to 
uint8_t _FADE_DIMMER = 10000;

```
Function `light_change_by_json` change to :
```c
mdf_err_t light_change_by_json(const char *p_src){
	mdf_err_t ret = MDF_OK;
	int power = -1, bri = -1, dimmer = -1, vacationmode = 0, commssion = 0, min_bri = 27;
	float fade = -1;
	char *p_name = NULL, *p_type_str = NULL;
	char time_zone[64] = {0};
	uint8_t p_mac[6] = {0};
	// power.
	ret = mlink_json_parse( p_src, "power", &power);
	if(ret == MDF_OK){
		MDF_LOGD("Set power to %d\n", power);
	}
    // dimmer_fade
    int dimmer_fade
    ret = mlink_json_parse( p_src, "dimmer_fade", &dimmer_fade);
	if(ret == MDF_OK){
		MDF_LOGD("Set dimmer_fade to %f\n", dimmer_fade);
        _FADE_DIMMER = dimmer_fade;
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
	
	ret = mlink_json_parse( p_src, "vacationmode", &vacationmode);
	if(ret == MDF_OK){
		light_vacationmode_set((uint8_t) vacationmode);
		MDF_LOGD("Set vacationmode to %d\n", vacationmode);
	}
	ret = mlink_json_parse( p_src, "minbrightness", &min_bri);
	if(ret == MDF_OK){
		light_min_bri_set( min_bri);
		MDF_LOGD("Set min bri to %d\n", min_bri);
	}

	light_change_user(power,  bri, fade, dimmer);

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
```
After that we can use Postman to change dimmer fade to 20s:

```json
{
  "name": "update_state",
  "data": {
    "dimmer_fade": 20000
  }
}
```
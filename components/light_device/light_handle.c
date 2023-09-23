/*
 * @Author: sky
 * @Date: 2020-03-09 18:34:28
 * @LastEditTime: 2021-02-18 11:15:24
 * @LastEditors: Please set LastEditors
 * @Description: 设备对外的 api 接口实现
 * @FilePath: \mqtt_example\components\light_device\light_handle.c
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <time.h>

#include "esp_wifi.h"
#include "mdf_err.h"
#include "mdf_common.h"
#include "mwifi.h"
#include "mlink.h"


#include "hue.h"
#include "event_queue.h"
#include "utlis.h"
#include "mesh_dev_table.h"
#include "mesh_event.h"
#include "mesh_thincloud.h"
#include "light_device.h"
#include "light_handle.h"
#include "light_ota.h"
#include "light_schedule.h"
#include "sunriset.h"

#define _STORE_KEY_DEV_NAME	"s_dev_name"
#define _STORE_KEY_DEV_ID	"s_dev_id"

#define _STORE_LAT_KEY	"f_Lat_info"
#define _STORE_LON_KEY	"f_Lon_info"


static const char *TAG          = "light_handle"; 

extern const char *mlink_device_get_version();
mdf_err_t mlink_set_value(uint16_t cid, void *arg)
{
    int value = *((int *)arg);

    MDF_LOGD("cid: %d, value: %d", cid, value);
    return MDF_OK;
}

mdf_err_t mlink_get_value(uint16_t cid, void *arg)
{
    int *value = (int *)arg;

    MDF_LOGV("cid: %d, value: %d", cid, *value);

    return MDF_OK;
}

static mdf_err_t _mlink_set_status(mlink_handle_data_t *handle_data)
{
    
	
	MDF_LOGD("receive set status %s\n",  handle_data->req_data);
	
    mlink_json_pack(&handle_data->resp_data, "statusCode",  200);
    handle_data->resp_size = strlen((char *)handle_data->resp_data);

    return MDF_OK;
}
static mdf_err_t _mlink_set_devid(mlink_handle_data_t *handle_data){
	// 保存 deviceid.
	// 上报 设备状态.
	char *p_devid = NULL;
	mdf_err_t ret =  MDF_FAIL;

	
	MDF_LOGD("receive set status %s\n",  handle_data->req_data);

	ret = mlink_json_parse(handle_data->req_data, "deviceId", &p_devid);
	MDF_ERROR_CHECK( MDF_OK != ret, ret, "Can't find deviceId!!");

	if(p_devid){
		Evt_mesh_t evt = {0};
		light_devid_set((uint8_t *) p_devid);
		// report device status event. MEVT_TC_INFO_REPORT

		memcpy(evt.p_devid, p_devid,  strlen(p_devid));
		
		MDF_LOGD("src deviceId %s \n", p_devid);
		MDF_LOGD("deviceId %s \n", evt.p_devid);
		evt.cmd = MEVT_TC_INFO_REPORT;
	
		ret = mevt_send(&evt, 100/portTICK_RATE_MS);
		if( MDF_OK != ret)
			MDF_LOGW("Failt to send event MEVT_TC_INFO_REPORT \n");
		MDF_FREE(p_devid);
	}else{
		MDF_LOGW("  p_devid == NULL \n");
	}

	return MDF_OK;
}

static mdf_err_t _mlink_get_devinfo_report(mlink_handle_data_t *handle_data){

	// 上报 设备状态.
	uint8_t *p_devid = NULL;
	mdf_err_t ret =  MDF_FAIL;

	Evt_mesh_t evt = {0};
	p_devid =  light_devid_get();
	if( p_devid == NULL){
		MDF_LOGI("No device id start to get device id \n");
		
		esp_wifi_get_mac(ESP_IF_WIFI_STA, evt.p_mac);
		evt.cmd = MEVT_TC_COMMISSION_REQUEST;
	}else{
		evt.cmd = MEVT_TC_INFO_REPORT;
		memcpy(evt.p_devid, p_devid, TC_ID_LENGTH);
	}
		//MDF_ERROR_GOTO( NULL == p_devid , End, "Failt: No device id.\n");
	// report device status event. MEVT_TC_INFO_REPORT


	MDF_LOGD("Receive tc_get_dev_info from mesh and send out MEVT_TC_INFO_REPORT command \n");
	
	ret = mevt_send(&evt, 100/portTICK_RATE_MS);
	if( MDF_OK != ret)
		MDF_LOGW("Failt to send event MEVT_TC_INFO_REPORT \n");


	return ret;
}
/** 1. 更新设备的状态。
*** 2.通过 mesh 回应最新的状态。
**  3. 再次通过 publish 的方式更新服务器上，目前设备的最新状态.
** 通过 mesh 接收到
** 	{
	  "request":"update_state",
	  "deviceId":"xxx",
	  "cid": "98d67f00-ae7d-4830-9e09-8a2767760cad",
	  "data": {"power":1,"timer":"00:00:00","name":"demo","type":"","brightness":50,"fade":1.02,"vacationmode":1,"remote_id":"02-xx","learn":true}
	}
**
****/

static mdf_err_t _mlink_update_state(mlink_handle_data_t *handle_data){

	mdf_err_t ret =  MDF_FAIL;
	Evt_mesh_t evt = {0}, r_evt = {0};
	char *p_data = NULL, *p_r_data = NULL;

	// 修改设备状态
	MDF_PARAM_CHECK( NULL != handle_data->req_data);
	MDF_LOGE("============================================\n");
	MDF_LOGE("Start Evt_mesh_t  Free heap %u\n", esp_get_free_heap_size( ) );
	MDF_LOGE("============================================\n");
	mlink_json_parse( handle_data->req_data, "data", &p_data);
	mlink_json_parse(handle_data->req_data, "cid", &evt.p_cid);
	
	if(p_data){
		light_change_by_json(p_data);
		MDF_FREE(p_data);
		p_data = NULL;
	}

	// 发出 回应 command 事件
	evt.cmd = MEVT_TC_COMMAND_RESPOND;
	mlink_json_parse(handle_data->req_data, "deviceId",  evt.p_devid);

	light_status_alloc((char **)&evt.p_data);
	evt.status_code = 200;

	if( evt.p_data ){
		handle_data->resp_data = (char *)malloc_copy_str((char *)evt.p_data);
		if(handle_data->resp_data)
			MDF_LOGD("Respond: %s\n", handle_data->resp_data);
			handle_data->resp_fromat = MLINK_HTTPD_FORMAT_JSON;
	}
	#if 0
	if(p_r_data){
		ret = command_response_json_alloc((char **) &evt.p_data, (char *)evt.p_cid, 200, p_r_data);
		MDF_ERROR_GOTO( MDF_OK != ret, Error_End, "Failt: to creat respond json \n");
	
	}else{
		ret = command_response_json_alloc((char **)&evt.p_data, (char *)evt.p_cid, 300, "Failt to get device's state!");
	}
	if( NULL == evt.p_data){
		MDF_LOGW("Failt: to get light status json!!\n");
	}
	#endif
#if 1
	ret = mevt_send(&evt, 100/portTICK_RATE_MS);
	if( MDF_OK != ret){
		MDF_LOGW("Failt to send event MEVT_TC_COMMAND_RESPOND \n");
		mevt_clean(&evt);
	}
#endif
#if 1
	// 发出设备上报事件.
	r_evt.cmd = MEVT_TC_INFO_REPORT;
	mlink_json_parse(handle_data->req_data, "deviceId",  r_evt.p_devid);

	ret = mevt_send(&r_evt, 100/portTICK_RATE_MS);
	if( MDF_OK != ret)
		MDF_LOGW("Failt to send event MEVT_TC_INFO_REPORT \n");
	else 
		mevt_clean(&r_evt);
	
#endif

	MDF_FREE(p_r_data);
	return ret;
//Error_End:
	
	MDF_FREE(p_r_data);
	mevt_clean(&evt);
	mevt_clean(&r_evt);
	
	return ret;
}
/***
** {
	  "request":"delta_state",
	  "deviceId":"xxx",
	  "cid": "98d67f00-ae7d-4830-9e09-8a2767760cad",
	  "data": {
		 "brightness":5
	   }
	}
******************/
static mdf_err_t _mlink_delta_state(mlink_handle_data_t *handle_data){

	mdf_err_t ret =  MDF_FAIL;
	Evt_mesh_t evt = {0}, r_evt = {0};
	char *p_data = NULL, *p_r_data = NULL;

	// 修改设备状态
	MDF_PARAM_CHECK( NULL != handle_data->req_data);
	
	MDF_LOGV("Start Evt_mesh_t  Free heap %u\n", esp_get_free_heap_size( ) );
	mlink_json_parse( handle_data->req_data, "data", &p_data);
	mlink_json_parse(handle_data->req_data, "cid", &evt.p_cid);
	
	if(p_data){
		
		int bri = 0;
		ret = mlink_json_parse( p_data, "brightness", &bri);
		if(ret == MDF_OK){
			bri = bri + (int)light_bri_user_get();
			//MDF_LOGE("old bri %d add bri %d \n", (int)light_bri_user_get(), bri);
			
			bri = (bri <=0)?0:bri;
			light_change_user(-1, bri, -1, -1);
		}else{
			// todo report error.
		}
		
		MDF_FREE(p_data);
		p_data = NULL;
	}

	// 发出 回应 command 事件
	evt.cmd = MEVT_TC_COMMAND_RESPOND;
	mlink_json_parse(handle_data->req_data, "deviceId",  evt.p_devid);

	light_status_alloc((char **)&evt.p_data);
	evt.status_code = 200;
#if 1
	ret = mevt_send(&evt, 100/portTICK_RATE_MS);
	if( MDF_OK != ret){
		MDF_LOGW("Failt to send event MEVT_TC_COMMAND_RESPOND \n");
		mevt_clean(&evt);
	}
#endif
#if 1
	// 发出设备上报事件.
	r_evt.cmd = MEVT_TC_INFO_REPORT;
	mlink_json_parse(handle_data->req_data, "deviceId",  r_evt.p_devid);

	ret = mevt_send(&r_evt, 100/portTICK_RATE_MS);
	if( MDF_OK != ret)
		MDF_LOGW("Failt to send event MEVT_TC_INFO_REPORT \n");
	else 
		mevt_clean(&r_evt);
	
#endif

	MDF_FREE(p_r_data);
	return ret;
//Error_End:
	
	MDF_FREE(p_r_data);
	mevt_clean(&evt);
	mevt_clean(&r_evt);
	MDF_FREE(p_data);
	
	return ret;
}

/***
***1.通过mesh 上报设备状态
{
"deviceId": "xxx-xxx-xx",
"cid":"xxx-xxx",
"request": "command_respond",
 "data":{
  "id": "0f4f6971-ccc3-4a44-8b5d-2379912e39e0",
  "result": {
    "statusCode": 200,
    "body": {
        “brightn”:12,
        "xx":xx
    }
  },
}
}

********/
static mdf_err_t _event_handle_tc_command_repond(Evt_mesh_t *p_evt)
{
	// get device id 
	mdf_err_t rc = MDF_OK;
	char *p_json = NULL, *p_data = NULL;
	char p_request_id[16] = {0};
	uint8_t *p_devid = light_devid_get();

	MDF_ERROR_CHECK(NULL == p_evt->p_cid, MDF_FAIL, "Failt: no request id\n");
	MDF_ERROR_CHECK(NULL == p_devid, MDF_FAIL, "Failt: get device id\n");

	
	MDF_LOGV("Start mesh event   Free heap %u\n", esp_get_free_heap_size( ) );
	memcpy(p_evt->p_devid, (char *)p_devid, TC_ID_LENGTH );

	mlink_json_pack( &p_json, "deviceId",  p_devid );

	
	mlink_json_pack( &p_json, "cid",  p_evt->p_cid );
	mlink_json_pack(&p_json, "request", "command_respond");
	

	// build tc json 
	command_response_json_alloc((char **) &p_data, (char *)p_evt->p_cid, p_evt->status_code, (char *)p_evt->p_data);

	if( p_data ){
		mlink_json_pack( &p_json, "data", p_data);
		MDF_FREE(p_data);
		p_data = NULL;
	}
	
	random_string_creat(p_request_id, 16);
	
	MDF_LOGD("Respond json %s \n", p_json);
	rc = mesh_send_2root_with_id((uint8_t *)p_request_id, &p_json);

	MDF_FREE(p_json);
	
	MDF_LOGV("End mesh event   Free heap %u\n", esp_get_free_heap_size( ) );
	return rc;
}
/***
***1. 发送到 tc 服务器，只能 root 接受响应.
接收到：
{
"deviceId": "xxx-xxx-xx",
"cid":"xxx-xxx",
"request": "command_respond",
 "data":{
  "id": "0f4f6971-ccc3-4a44-8b5d-2379912e39e0",
  "result": {
    "statusCode": 200,
    "body": {
        “brightn”:12,
        "xx":xx
    }
  },
}
}

通过 mqtt  向 "thincloud/devices/{deviceId}/command/{commandId}/response"  发送
 {
  "id": "0f4f6971-ccc3-4a44-8b5d-2379912e39e0",
  "result": {
    "statusCode": 200,
    "body": {
        “brightn”:12,
        "xx":xx
    }
  },
}

********/

static mdf_err_t _mlink_get_command_respond(mlink_handle_data_t *handle_data)
{
	mdf_err_t rc =  0;
	uint8_t *p_devid = NULL, *p_cid = NULL, *p_data = NULL;

	MDF_PARAM_CHECK(NULL != handle_data);
	MDF_PARAM_CHECK(NULL != handle_data->req_data);
	
	MDF_LOGV("Start mqtt event   Free heap %u\n", esp_get_free_heap_size( ) );
	// 获取 deviceid
	rc = mlink_json_parse(handle_data->req_data, "deviceId",  &p_devid);
	MDF_ERROR_GOTO( (MDF_OK != rc), End, "Failt: not deviceid \n");
	
	// 获取 cid
	rc = mlink_json_parse(handle_data->req_data, "cid", &p_cid);
	MDF_ERROR_GOTO( (MDF_OK != rc), End, "Failt: not cid \n");
	// 获取要响应的 data
	rc = mlink_json_parse(handle_data->req_data, "data", &p_data);
	MDF_ERROR_GOTO( (MDF_OK != rc), End, "Failt: not deviceid");
	
	rc = tc_send_comamnd_respond(p_devid, p_cid, (char *)p_data);
	
End:

	MDF_FREE( p_devid);
	MDF_FREE( p_cid);
	MDF_FREE( p_data);
	
	return rc;
	
}
/***
先推送到    root 节点，root 节点再把外层json 去掉，推送到 对应的 topic.
推送到 Thindcloud 服务器数据的格式如下: 
"topic":"thincloud/devices/%s/requests",

{
	"request": "tc_publish",
	"mac":"xxx",
	"deviceId":"xxx",
	"cid":"xx",
	"data":{
	  "id": "0f4f6971-ccc3-4a44-8b5d-2379912e39e0",
	  "method": "PUT",
	  "params": [
	    {
	      "brightness": 1
	    }
	  ]
	}
}
*******/
static mdf_err_t _event_handle_info_report(Evt_mesh_t *p_evt){
	// 收集设备信息.
	// 发送到 root 节点.
	mdf_err_t ret =  MDF_OK;
	char *p_json = NULL, *p_s_json = NULL, *p_ss_json = NULL,*p_custom = NULL, *p_array = NULL;
	char p_request_id[16] = {0};
    uint8_t self_mac[6] = {0}, tmp_str[32] = {0};



	light_status_alloc( &p_ss_json);
	MDF_ERROR_GOTO( NULL == p_ss_json, Exit, "Fait: to get device status json\n");
	
	mlink_json_pack(&p_custom, "custom", p_ss_json);
	
	MDF_FREE(p_ss_json);
	p_ss_json = NULL;
	// creat 
	// build "params":[ { "brightness": 1} ]
	p_array = MDF_MALLOC( strlen( p_custom ) + 8 );
	
	MDF_ERROR_GOTO( ( NULL == p_array  ), Exit, "Failt to Alloc !!\n");
	sprintf(p_array, "[%s]", p_custom);
	// todo requestid !!!
	MDF_FREE(p_custom);
	p_custom = NULL;
	
	
	random_string_creat(p_request_id, 16);
	ret = service_request(&p_s_json, p_request_id, "put", p_array);
	MDF_ERROR_GOTO( ( MDF_OK != ret || NULL == p_s_json ), Exit, "creat server request failt !!\n");
	
	// build  {"request":"xx", "topic":"xx","params":[ { "brightness": 1} ] }
	//service_request_topic(p_topic, (const char *)p_evt->p_devid);
	mlink_json_pack(&p_json, "request", "tc_publish");
	//mlink_json_pack(&p_json, "topic", p_topic);
	mlink_json_pack(&p_json, "data",  p_s_json);

    esp_wifi_get_mac(ESP_IF_WIFI_STA, self_mac);
	mlink_mac_hex2str(self_mac, (char *)tmp_str);

	mlink_json_pack(&p_json, "deviceId", (char *)p_evt->p_devid);
	mlink_json_pack(&p_json, "mac", (char *)tmp_str);

	MDF_ERROR_GOTO( (  NULL == p_json ), Exit, "Build top json failt!!\n");
	
	ret = mesh_send_2root_with_id((uint8_t *)tmp_str, &p_json);
	
	if( MDF_OK !=ret )
		MDF_LOGW("Failt to report device info. !!\n");
	else
		MDF_LOGD("Successfully report info %s\n", p_json);

Exit:

	MDF_FREE(p_ss_json);
	MDF_FREE(p_custom);
	MDF_FREE(p_array);
	MDF_FREE(p_s_json);
	MDF_FREE(p_json);
	
	return ret;
}

// 1. 只是转发
/***
接收到: 
"topic":"thincloud/devices/%s/requests",

{
	"request": "tc_publish",
	"mac":"xxx",
	"deviceId":"xxx",
	"cid":"xx",
	"data":{
	  "id": "0f4f6971-ccc3-4a44-8b5d-2379912e39e0",
	  "method": "PUT",
	  "params": [
	    {
	      "brightness": 1
	    }
	  ]
	}
}
*******/

static mdf_err_t _mlink_tc_publish(mlink_handle_data_t *handle_data){

	mdf_err_t ret = MDF_OK;
	Evt_mesh_t evt = {0};
	char *p_str_mac = {0};
	int len = strlen(handle_data->req_data);
	
	MDF_LOGD("mlink get %s\n", handle_data->req_data);
	
	MDF_ERROR_GOTO( 0 >= len , ERR_Exit, "No json data!!\n");
	
	ret = mlink_json_parse(handle_data->req_data , "deviceId", evt.p_devid);
	MDF_ERROR_GOTO( MDF_OK != ret, ERR_Exit, "Json No deviceId!!\n");


	ret = mlink_json_parse(handle_data->req_data , "mac", &p_str_mac);
	if( p_str_mac){
		
		mlink_mac_str2hex(p_str_mac,  evt.p_mac);
		MDF_FREE(p_str_mac);
		p_str_mac = NULL;
	}
	
	MDF_ERROR_GOTO( MDF_OK != ret, ERR_Exit, "Json No mac!!\n");

	evt.p_data = MDF_MALLOC(  len + 1);	
	MDF_ERROR_GOTO( NULL == evt.p_data, ERR_Exit, "No Memory!!\n");

	memset(evt.p_data, 0, len + 1);
	memcpy(evt.p_data, handle_data->req_data, len);

	evt.cmd = MEVT_TC_PUBLISH_TO_SERVER;

	ret = mevt_send(&evt, 0);	
	MDF_ERROR_GOTO( MDF_OK != ret, ERR_Exit, "Send event failt!!\n");

	return ret;
	
ERR_Exit:
	
	MDF_FREE(evt.p_data);

	return ret;
}
// 该函数只有 root 才能触发
// 1. 检查 tab，不存在则更新。   todo 
// 2. 检查是否订阅 command 和 request, 若无订阅则直接订阅. todo 
// 3. publish 到 Thincloud.
/***
推送到 Thindcloud 服务器数据的格式如下: 
{
  "id": "0f4f6971-ccc3-4a44-8b5d-2379912e39e0",
  "method": "PUT",
  "params": [
    {
      "deviceAttribute": "new-value"
    }
  ]
}

*****/
static mdf_err_t _event_handle_tc_publish(Evt_mesh_t *p_msg){
	//3. publish 到 Thincloud.
	mdf_err_t ret = MDF_OK;

	
	if(esp_mesh_is_root() )
		ret = tc_send_publish(p_msg->p_mac, p_msg->p_devid, (char *)p_msg->p_data);
	else
		MDF_LOGW("Need tobe root to handle this event\n");
	
	return ret;
}


/***
** 向 root 发送：
{
	"request": "commission_request",
	"mac":"xxx"
}

***/
static mdf_err_t _event_handle_tc_commission_request(Evt_mesh_t *p_evt)
{
	mdf_err_t rc =  0;
	char *p_json = NULL, p_mac_str[32] = {0};
	// mac to string
	mlink_mac_hex2str(p_evt->p_mac, p_mac_str);

	mlink_json_pack(&p_json, "request", "commission_request");
	mlink_json_pack(&p_json, "mac", p_mac_str);

	rc =  mesh_send_2root_with_id(NULL, &p_json);
	if(rc != MDF_OK){
		MDF_LOGW("Failt to send commission_request to root\n");
	}

	MDF_FREE(p_json);
	return rc;
}
/***
** 向 tc 发送：
{
"data":{
  "id": "0f4f6971-ccc3-4a44-8b5d-2379912e39e0",
  "method": "DELETE /storage/childDeviceList",
  "params": []
  }
}


***/
static mdf_err_t _event_del_online_storage(void){

	mdf_err_t ret = MDF_OK;
	Evt_mesh_t evt = {0};
	char *p_str_mac = {0}, *p_s_json = NULL;
	char p_request_id[16] = {0};

	
    MDF_ERROR_CHECK( !esp_mesh_is_root(), ret, "Device not root!");
    MDF_ERROR_CHECK( !tc_is_connect(), ret, "Nee TC connect!");



	random_string_creat(p_request_id, 16);
	ret = service_request(&p_s_json, p_request_id, "DELETE /storage/childDeviceList", NULL);
	
	esp_wifi_get_mac( ESP_IF_WIFI_STA, evt.p_mac);
	MDF_ERROR_GOTO( MDF_OK != ret, ERR_Exit, "Json No mac!!\n");
	if(p_s_json){
		char *p_data = NULL;
		
		mlink_json_pack( &p_data, "data", p_s_json);
		MDF_FREE(p_s_json);
		p_s_json = NULL;
		
		MDF_LOGD("Delete storage %s\n", p_data);

		memcpy(evt.p_devid, (char *)light_devid_get(), TC_ID_LENGTH);
		
			
		evt.data_len = strlen(p_data);
		evt.p_data =(uint8_t *) p_data;	
		evt.cmd = MEVT_TC_PUBLISH_TO_SERVER;
		ret = mevt_send(&evt, 0);	
		MDF_ERROR_GOTO( MDF_OK != ret, ERR_Exit, "Send MEVT_TC_PUBLISH_TO_SERVER event failt!!\n");

	}else {
		MDF_FREE( p_s_json);
	}
	return ret;
	
ERR_Exit:
	
	MDF_FREE(evt.p_data);

	return ret;
}
static mdf_err_t _event_update_online_storage(void){
	mdf_err_t ret = MDF_OK;
	Evt_mesh_t evt = {0};
	char *p_str_mac = {0}, *p_json = NULL, *p_arr = NULL, *p_s_arr = NULL, *p_ss_arr = NULL;
	char p_request_id[16] = {0};
	int len  = 0;
	
	MDF_ERROR_CHECK( !esp_mesh_is_root(), ret, "Device not root!");
    MDF_ERROR_CHECK( !tc_is_connect(), ret, "Nee TC connect!");
	
	random_string_creat(p_request_id, 16);
	p_ss_arr = dev_tap_deviceid_json_get();
	MDF_ERROR_CHECK(  p_ss_arr == NULL, -1, "Failt to get device id tap array!");

	MDF_LOGD("device id array list %s",  p_ss_arr);
	mlink_json_pack(&p_s_arr, "childDeviceList", p_ss_arr);


	MDF_ERROR_GOTO( p_s_arr == NULL, ERR_Exit, "Failt to build tap array!");
	MDF_FREE(p_ss_arr);
	p_ss_arr = NULL;
	
	p_arr = utlis_malloc( 2 );
	MDF_ERROR_GOTO( p_arr == NULL, ERR_Exit, "Failt to build tap array!");
	p_arr[ 0 ] = '[';
	len  = 2 + strlen( p_s_arr );
	p_arr = MDF_REALLOC(p_arr, len);
	MDF_ERROR_GOTO( p_arr == NULL, ERR_Exit, "Failt to build tap array!");

	strcat( p_arr, p_s_arr );
	MDF_FREE(p_s_arr);
	p_s_arr = NULL;
	len = strlen( p_arr) + 2;
	p_arr = MDF_REALLOC(p_arr, len);
	MDF_ERROR_GOTO( p_arr == NULL, ERR_Exit, "Failt to build tap array!");
	p_arr[ len - 2 ] = ']';
	p_arr[len - 1] = '\0';
	
	
	ret = service_request(&p_json, p_request_id, "POST /storage/childDeviceList", p_arr);
	MDF_ERROR_GOTO( MDF_OK != ret, ERR_Exit, "Failt to Build json!!\n");	
	esp_wifi_get_mac( ESP_IF_WIFI_STA, evt.p_mac);

	if(p_json){
		char *p_data = NULL;
		mlink_json_pack( &p_data, "data", p_json);
		MDF_FREE(p_json);
		p_json = NULL;

		MDF_LOGD("Post storage %s\n", p_data);
		
		memcpy(evt.p_devid, light_devid_get(), TC_ID_LENGTH);
		evt.data_len = strlen(p_data);
		evt.p_data =(uint8_t *) p_data;	
		evt.cmd = MEVT_TC_PUBLISH_TO_SERVER;
		ret = mevt_send(&evt, 0);	
		MDF_ERROR_GOTO( MDF_OK != ret, ERR_Exit, "Send MEVT_TC_PUBLISH_TO_SERVER event failt!!\n");

	}else {
		goto ERR_Exit;
	}
	
	return ret;
	
ERR_Exit:
	MDF_FREE(p_arr);
	p_arr = NULL;
	MDF_FREE(p_s_arr);
	p_s_arr = NULL;
	MDF_FREE(p_ss_arr);
	p_ss_arr = NULL;

	MDF_FREE(p_json);
	p_json = NULL;

	MDF_FREE(evt.p_data);

	return ret;

}

static uint64_t _time_runing_ms = 0;

void light_online_device_update_set( uint64_t t_ms){
	_time_runing_ms = t_ms + utils_get_current_time_ms();

}

void light_online_update_loop(void){

	if( light_devid_get() == NULL){
		light_online_device_update_set( 1500);
	} else if(_time_runing_ms > 0 && utils_get_current_time_ms()  >= _time_runing_ms){

			
				_event_del_online_storage();
				_event_update_online_storage();
				_time_runing_ms = -1;
			}
}

static mdf_err_t _mlink_commission_request(mlink_handle_data_t *handle_data)
{
	char *p_mac_str = NULL;
	uint8_t p_mac[6] = {0};
	mdf_err_t rc = MDF_OK;
	
	MDF_PARAM_CHECK(handle_data);
	MDF_PARAM_CHECK(handle_data->req_data);

	// add table
	mlink_json_parse(handle_data->req_data, "mac", &p_mac_str);
	MDF_ERROR_CHECK(NULL == p_mac_str, MDF_FAIL, "Failt find mac \n");

	mlink_mac_str2hex(p_mac_str, p_mac);

	rc = tc_mac_commission(p_mac);

	MDF_FREE(p_mac_str);

	return rc;
}
static mdf_err_t _event_handle_sys_rest(Evt_mesh_t *p_evt)
{
	MDF_LOGW("Restart PRO and APP CPUs");
	esp_restart();

	return MDF_OK;
}
extern int ca_save(const char *p_root, const char *p_crt, const char *p_key);
static mdf_err_t _mlink_ca_config(mlink_handle_data_t *handle_data){
	mdf_err_t rc = 0;
	char *p_crt = NULL, *p_pri = NULL, *p_data = NULL;
	int status_code = 300;

	mlink_json_parse(handle_data->req_data, "data", &p_data);
	if(p_data){
		rc =  mlink_json_parse(p_data, "certificate_crt", &p_crt);
		MDF_LOGW("rc = %d \n", rc );
		//rc = mlink_json_parse(p_data, "private_key", &p_pri);
		//MDF_LOGW("rc = %d \n", rc );
		MDF_FREE(p_data);
		p_data = NULL;
	}
	
	if(ESP_OK ==  ca_save(NULL, p_crt, p_pri) ){
		MDF_LOGD("successfully save certificate_crt and private_key \n" );
		status_code = 200;
	}
	
	MDF_FREE(p_crt);
	MDF_FREE(p_pri);

	return mevt_command_respond_creat( (char *)handle_data->req_data, status_code, NULL);
}


/*****
Send Press Up/Down Command From Node Through Root
{
	  "request":"node_press",
	  "url": "light/3/state",
	  "body":"start",
	  "type":"up"
}
******/
mdf_err_t send_node_press(const char *p_buff , const char *type, const char *p_url){ 
 	char *p_json = NULL;
	int ret = 0;
	MDF_PARAM_CHECK( !esp_mesh_is_root());
	mlink_json_pack(&p_json, "request", "node_press");
	mlink_json_pack(&p_json, "body", p_buff); //start or stop
	mlink_json_pack(&p_json, "type", type);   //tap type( up or down)
	mlink_json_pack(&p_json, "url", p_url);	  //Url
	if( p_json ){
		MDF_LOGE("Data From Press For Sending to root after packing into json %s\n", p_json);
		uint8_t dst_addr[6]= MWIFI_ADDR_ROOT;
		ret =  mesh_data_send(dst_addr, p_json, strlen( p_json ));
		MDF_FREE(p_json);
		p_json = NULL;
		MDF_LOGE("Press Data Sent");

	}else{
		MDF_LOGE("Failt to pack json\n");
	} 
	
	return ret;
}





/*****
Send Hue Command From Node Through Root
{
	  "request":"hue_2root",
	  "url": "light/3/state",
	  "body":
	  {
		  "on":true,
		  "bri":100,
		  "ct":500
	  },
	  "method":2
	}
******/
mdf_err_t send_hue_2root(const char *p_url, int meth_index, const char *p_buff){ 
 	char *p_json = NULL;
	int ret = 0;
	MDF_PARAM_CHECK( !esp_mesh_is_root());
	mlink_json_pack(&p_json, "request", "hue_2root");
	mlink_json_pack(&p_json, "url", p_url);
	mlink_json_pack(&p_json, "body", p_buff);
	mlink_json_pack(&p_json, "method", meth_index);
	if( p_json ){
		MDF_LOGE("Data From Hue active For Sending to root after packing into json %s\n", p_json);
		uint8_t dst_addr[6]= MWIFI_ADDR_ROOT;
		ret =  mesh_data_send(dst_addr, p_json, strlen( p_json ));
		MDF_FREE(p_json);
		p_json = NULL;
		MDF_LOGE("Data Sent");

	}else{
		MDF_LOGE("Failt to pack json\n");
	} 
	
	return ret;
}
/***
** 
receive:
{
	  "request":"time_update_rq",
	  "mac": "98d67f00-ae7d-4830-9e09-8a2767760cad",
	}
respond:
{
	  "request":"time_update_ack",
	  "mac": "98d67f00-ae7d-4830-9e09-8a2767760cad",
	  "current_time": uint32_t
	}

******************/
mdf_err_t make_time_update_rq_2root(void){
	char *p_json = NULL;
	int ret = 0;
	
	MDF_PARAM_CHECK( !esp_mesh_is_root());
	mlink_json_pack(&p_json, "request", "time_update_rq");
	if( p_json ){
		uint8_t self_mac[6] = {0};
		char p_mac_str[64] = {0};
		uint8_t dst_addr[6]= MWIFI_ADDR_ROOT;
		esp_wifi_get_mac(ESP_IF_WIFI_STA, self_mac);
		sprintf(p_mac_str, _MAC_STR_FORMAT, PR_MAC2STR(self_mac) );	
		mlink_json_pack(&p_json, "mac", p_mac_str);
		ret =  mesh_data_send(dst_addr, p_json, strlen( p_json ));
		MDF_FREE(p_json);
		p_json = NULL;
	}else{
		MDF_LOGE("Failt to pack json\n");
	}
	return ret;
}
/***

Function To Perform Http Requests From Root

****/
esp_err_t perform_http_request(const char *p_url, int method, const char *p_buff, char **pp_http_recv)
{
	int ret = -1;
	if( 0 == http_perform_as_stream_reader(p_url, method, p_buff, pp_http_recv))
	{
		MDF_LOGE("hue send http rq failt !!\n");
	}
	if(pp_http_recv && *pp_http_recv)
	MDF_LOGE("recv %s \n", *pp_http_recv);
   ret = 0;

return ret;
}

/***

Function To Receive Request "hue_2root" sent From Node

****/

static mdf_err_t _mlink_update_hue(mlink_handle_data_t *handle_data){	//k
	MDF_LOGE("Hue Data Received by ROOT");

	mdf_err_t ret =  MDF_OK;
	char *p_url = NULL, *p_buff = NULL, *meth_index = NULL, *p_recv = NULL;
	MDF_PARAM_CHECK( NULL != handle_data->req_data );
	
	MDF_PARAM_CHECK( esp_mesh_is_root());
	mlink_json_parse( handle_data->req_data, "url", &p_url);
	mlink_json_parse( handle_data->req_data, "body", &p_buff);
	mlink_json_parse( handle_data->req_data, "method", &meth_index);
	MDF_LOGE("Data From Node Hue active to perform http_perform_as_stream_reader");
	MDF_LOGE("p_url	%s\n", p_url);
	MDF_LOGE("p_buff %s\n", p_buff);
	
	//Perform Http Request Sent By Node 
	if( (p_url != NULL))
	{
		if( 0 == perform_http_request(p_url, meth_index, p_buff, & p_recv) )
		{
			MDF_LOGE("Sent to HUE");
			if(p_recv != NULL)
			{
			MDF_LOGE("http receive %s\n", p_recv);
			}
			else
			{
			MDF_LOGE("Response Not Received");	
			}
		}		
	}else
	{
		MDF_LOGW("No hue in update hue \n");
	}
	MDF_FREE(p_url );
	MDF_FREE(p_buff);
	p_url = NULL;
	p_buff = NULL;
	return ret;
}

/***

Function To Set Longitude Latitude in Flash

****/
static mdf_err_t _mlink_latlon_set(mlink_handle_data_t *handle_data){
	MDF_LOGW("Lat Lon picked up by Root \n");
	mdf_err_t ret =  MDF_OK;
	char *p_Data= NULL;
	MDF_PARAM_CHECK( NULL != handle_data->req_data );
	// MDF_LOGI("%s \n",handle_data->req_data);   
	mlink_json_parse( handle_data->req_data, "data", &p_Data);
	MDF_LOGI("%s \n",p_Data); 
	
	char Lat_1[12]={0};
	char Lon_1[12]={0};
	mlink_json_parse( p_Data, "lat",Lat_1);
	mlink_json_parse( p_Data, "lon",Lon_1);
	
	MDF_LOGI("Lat %s\n", Lat_1);
	MDF_LOGI("Lon %s\n", Lon_1);  
	
	mdf_info_save(_STORE_LAT_KEY, Lat_1,sizeof(Lat_1)+1); 
	mdf_info_save(_STORE_LON_KEY, Lon_1,sizeof(Lon_1)+1);
	
	MDF_FREE(p_Data);
	p_Data = NULL;
	return ret;

}
/***

Function To Receive Request "node_press" sent From Node

****/

static mdf_err_t _mlink_node_press(mlink_handle_data_t *handle_data){
	char *p_buff = NULL, *type = NULL,*p_url=NULL;
	mdf_err_t ret =  MDF_OK;
	MDF_LOGE("Press Data Received by ROOT");
	MDF_PARAM_CHECK( NULL != handle_data->req_data );
	
	MDF_PARAM_CHECK( esp_mesh_is_root());
	mlink_json_parse( handle_data->req_data, "body", &p_buff);
	mlink_json_parse( handle_data->req_data, "type", &type);
	mlink_json_parse( handle_data->req_data, "url", &p_url);
	
	if (strcmp(p_buff, "start") == 0)   		//Check if The body contains "start"
	{
		if (strcmp(type, "up") == 0)			//Check if The body contains "up"
		{
			Hue_Loop_Tap_up(p_url);				//Start Hue Loop with Press Up
		}
		else if(strcmp(type, "down") == 0)		//Check if The body contains "down"
		{
			Hue_Loop_Tap_down(p_url);			//Start Hue Loop with Press Down
		}
	}
	else if(strcmp(p_buff, "stop") == 0)		//Check if The body contains "stop"
	{
		Hue_Loop_exit();						//Stop Hue Loop
		MDF_LOGE("Sent to Hue_Loop_exit()");
	}
	
	MDF_FREE(type );
	MDF_FREE(p_buff);
	MDF_FREE(p_url);
	p_url = NULL;
	type = NULL;
	
	p_buff = NULL;
	return ret;
}



/******************
receive :
{
	  "request":"time_update_ack",
	  "mac": "98d67f00-ae7d-4830-9e09-8a2767760cad",
	  "current_time": "Wed Sep 23 17:22:21 2020"
	}

*******************/
static mdf_err_t _mlink_time_update_rq(mlink_handle_data_t *handle_data){

	mdf_err_t ret =  MDF_OK;
	char *p_mac = NULL;

	MDF_PARAM_CHECK( NULL != handle_data->req_data );
	
	MDF_PARAM_CHECK( esp_mesh_is_root());
	mlink_json_parse( handle_data->req_data, "mac", &p_mac);
	if( p_mac ){
		char *p_respond = NULL, ctime_str[64] = {0};
		unix_time2string(utils_get_current_time_ms()/ 1000, ctime_str, 64);
		mlink_json_pack(&p_respond, "current_time", ctime_str);
		if(p_respond){
			uint8_t dst_mac[12] = {0};
			mlink_json_pack(&p_respond, "mac", p_mac);
			mlink_json_pack(&p_respond, "request", "time_update_ack");
			MAC_STR_2_BYTE(dst_mac, p_mac);
			ret =  mesh_data_send(dst_mac, p_respond, strlen( p_respond ));
			MDF_FREE(p_respond);
			p_respond = NULL;
		}
		
		MDF_FREE(p_mac);
		p_mac = NULL;
	}else{
		MDF_LOGW("No mac in handle time_update_rq \n");
	}

	return ret;
}
/******************
receive :
{
	  "request":"time_update_ack",
	  "mac": "98d67f00-ae7d-4830-9e09-8a2767760cad",
	  "current_time": "Wed Sep 23 17:22:21 2020"
	}

*******************/
static mdf_err_t _mlink_time_update_ack(mlink_handle_data_t *handle_data){

	mdf_err_t ret =  MDF_FAIL;
	char *p_ctime = NULL;

	MDF_PARAM_CHECK( NULL != handle_data->req_data );
	
	ret = mlink_json_parse( handle_data->req_data, "current_time", &p_ctime);
	if( ESP_OK == ret &&  p_ctime && strlen( p_ctime ) > 0 ){
		
		struct tm tm = {0};
		struct timeval tv={0};
		struct timezone tz = {0};

		strptime(p_ctime, "%a %b %d %H:%M:%S %Y ", &tm);
		tv.tv_sec = mktime(&tm);
		tz.tz_minuteswest=0;
		tz.tz_dsttime=1;
		settimeofday(&tv, &tz);
		local_time_printf();
		MDF_FREE(p_ctime);
		p_ctime = NULL;
	}else{
		MDF_LOGW("No current_time in handle time_update_ack \n");
	}

	return ret;
}


static void _mesh_handle_event_register(void){
	
 	mevt_handle_func_register( _event_handle_sys_rest, EVT_SYS_REST);	
 	mevt_handle_func_register( _event_handle_info_report, MEVT_TC_INFO_REPORT);	
 	mevt_handle_func_register( _event_handle_tc_publish,  MEVT_TC_PUBLISH_TO_SERVER);
 	mevt_handle_func_register( _event_handle_tc_command_repond,  MEVT_TC_COMMAND_RESPOND);
 	mevt_handle_func_register( _event_handle_tc_commission_request,  MEVT_TC_COMMISSION_REQUEST);

}
static void _mlink_handle_function_register(void){
    MDF_ERROR_ASSERT(mlink_set_handle("tc_set_status", _mlink_set_status) );
    MDF_ERROR_ASSERT(mlink_set_handle("tc_set_devid", _mlink_set_devid) );
    MDF_ERROR_ASSERT(mlink_set_handle("tc_publish", _mlink_tc_publish) );
    MDF_ERROR_ASSERT(mlink_set_handle("tc_get_dev_info", _mlink_get_devinfo_report) );

	// tc command.
    MDF_ERROR_ASSERT(mlink_set_handle("delta_state", _mlink_delta_state));
    MDF_ERROR_ASSERT(mlink_set_handle("update_state", _mlink_update_state));
    MDF_ERROR_ASSERT(mlink_set_handle("_update", _mlink_update_state));
    MDF_ERROR_ASSERT(mlink_set_handle("hue_2root", _mlink_update_hue));
    MDF_ERROR_ASSERT(mlink_set_handle("node_press", _mlink_node_press));
    MDF_ERROR_ASSERT(mlink_set_handle("latlon_set", _mlink_latlon_set));
	// todo remove 
    MDF_ERROR_ASSERT(mlink_set_handle("ca_config", _mlink_ca_config) );
    MDF_ERROR_ASSERT(mlink_set_handle("time_update_rq", _mlink_time_update_rq) );
    MDF_ERROR_ASSERT(mlink_set_handle("time_update_ack", _mlink_time_update_ack) );


	MDF_ERROR_ASSERT(mlink_set_handle("command_respond", _mlink_get_command_respond));
    MDF_ERROR_ASSERT(mlink_set_handle("commission_request", _mlink_commission_request));

}

mdf_err_t light_device_init(){

    uint8_t self_mac[6]          = {0};
	uint8_t p_name[64]              = {0};

	
    esp_log_level_set(TAG, ESP_LOG_INFO);
	// todo tid modify tid
	//  add default name
    esp_wifi_get_mac(ESP_IF_WIFI_STA, self_mac);
	sprintf((char *)p_name, _MAC_STR_FORMAT, PR_MAC2STR(self_mac) );
	// todo set CONFIG_LIGHT_VERSIONP
	// todo  
    MDF_ERROR_ASSERT( mlink_add_device(MWIFI_ID, (const char *)p_name, "V0.7.08.49" ));

	// device init
	device_init();
	// todo.
	schedule_init();
	// add device characteristic
    MDF_ERROR_ASSERT( mlink_add_characteristic(LIGHT_CID_POWER, "power", CHARACTERISTIC_FORMAT_INT, CHARACTERISTIC_PERMS_RWT, 0, 1, 1));
    MDF_ERROR_ASSERT( mlink_add_characteristic(LIGHT_CID_BRI, "brightness", CHARACTERISTIC_FORMAT_INT, CHARACTERISTIC_PERMS_RWT, 0, 100, 1));
    MDF_ERROR_ASSERT( mlink_add_characteristic(LIGHT_CID_FADE, "fade", CHARACTERISTIC_FORMAT_DOUBLE, CHARACTERISTIC_PERMS_RWT, 0, 10,  0));
	MDF_ERROR_ASSERT( mlink_add_characteristic(LIGHT_CID_SHUTDOWN_TIME, "shutdown_time_ms", CHARACTERISTIC_FORMAT_INT, CHARACTERISTIC_PERMS_RWT, 0, 3600, 1));
	
	// 初始化 queue 事件
	mevt_init(); // event init 
	mesh_event_init(); //  event register

	  /**
     * @brief Add a request handler, handling request for devices on the LAN.
     */
    _mesh_handle_event_register();
	_mlink_handle_function_register();
	oat_init();
	hue_init();
	// start event queue 
	//xTaskCreatePinnedToCore(evt_loop_task, "event_queue_task", 2 * 1024,
    //                        NULL, CONFIG_MDF_TASK_DEFAULT_PRIOTY, NULL, 0);
	//xTaskCreate(evt_loop_task, "event_queue_task", 3 * 1024,
    //            NULL, CONFIG_MDF_TASK_DEFAULT_PRIOTY, NULL);
	return MDF_OK;
}

/*
 * @Author: sky
 * @Date: 2020-02-25 17:56:10
 * @LastEditTime: 2021-02-04 17:13:33
 * @LastEditors: Please set LastEditors
 * @Description: 对 aws-mqtt 进行封装以适应 mesh 事件调用
 ****   1. aws-mqtt 不支持回调，需要为 mqtt 连接分配线程。
 ****   2. 通过销毁当前 mqtt 连接，和重建 mqtt 连接实现断线重连。
 * @FilePath: \esp\mqtt_example\components\thincloud\mesh_thincloud.c
 **/
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "aws_iot_log.h"
#include "aws_iot_error.h"
#include "aws_iot_mqtt_client_interface.h"

#include "mlink.h"
#include "mwifi.h"
#include "esp_wifi.h"

#include "light_device_config.h"
#include "mesh_event.h"
#include "mesh_thincloud.h"
#include "mesh_dev_table.h"
#include "mdf_err.h"
#include "utlis.h"
#include "utlist.h"
#include "tc_event.h"
#include "light_device.h"
#include "lwip/apps/sntp.h"

#include "light_handle.h"
enum tc_evt{
    TC_EVT_NONE,
    TC_EVT_CONNT,
    TC_EVT_DISCNN,
    TC_EVT_MAX
};
typedef  struct  TC_T{
    /* data */
    SemaphoreHandle_t cnn_lock;
    AWS_IoT_Client *p_mclient;

    xQueueHandle evt_cmd; // tobe remove.
    xQueueHandle evt_cnn;
	bool connet_tc;		// connect to tc flag
}Tc_t;



static TaskHandle_t g_tc_mqtt_task_handle  = NULL;

static const char *TAG          = "thincloud_ward";
static Tc_t tc;

mdf_err_t _unsub_device(Dev_tab_t *p_tab);

#define _TRY_LOCK_CNN(t)  (xSemaphoreTake( tc.cnn_lock, ( TickType_t ) t ) == pdTRUE)
#define _UNLOCK_CNN()		xSemaphoreGive( tc.cnn_lock )


void tc_msg_destory(Tc_evt_msg_t *p_msg);


// 处理了 mis 回调
static void _tc_cmis_cb(
    AWS_IoT_Client *client,
    char *topicName,
    uint16_t topicNameLen,
    IoT_Publish_Message_Params *params,
    void *data){

	uint16_t statusCode = 0;
	
	Tc_evt_msg_t msg = {0};

	msg.cmd = TC_EVT_TC_CMIS;
	MDF_LOGD("in commissioning_response  \n");
	if(params->payload)
		MDF_LOGI("recv %s \n", (char *)params->payload);

	commissioning_response((char **) &msg.p_devid, &statusCode, (char **)&msg.p_cmdid, params->payload, params->payloadLen);
	if( msg.p_devid)
    	MDF_LOGD("receive device id  %s\n", msg.p_devid);
	if( msg.p_cmdid){	
		msg.p_mac = MDF_MALLOC( 6 );
		mlink_mac_str2hex((const char *)msg.p_cmdid, msg.p_mac);
	}
	MDF_LOGD("Status code %d \n", statusCode);
	if(statusCode == 200  && tc.evt_cmd){ //  发出接收到  cmis 回应消息
    	xQueueSend(tc.evt_cmd, &msg, 100/portTICK_RATE_MS );
	}
	else  tc_evt_msg_clean(&msg);
	
}

static void _tc_respond_cb(
	AWS_IoT_Client *client,
    char *topicName,
    uint16_t topicNameLen,
    IoT_Publish_Message_Params *params,
    void *data){

	// todo. 
	// 回应应该又所处理.
	if(topicName)
		MDF_LOGI("From Topic %s\n", topicName);

	if(params->payload)
		MDF_LOGI("receive command %s\n",  (char *)params->payload);

}

// 1. 提取 deviceid, topic, mac
// 2. 封装 成事件，通过 mesh 发送到对应的 mac node.
static void _tc_sub_dev_cmd_cb(
	AWS_IoT_Client *client,
    char *topicName,
    uint16_t topicNameLen,
    IoT_Publish_Message_Params *params,
    void *data){

	int len_devid  = 0;
	char *p_sub =  NULL, *p_next = NULL;

	Tc_evt_msg_t msg = {0};
	IoT_Error_t rc = 0;
	Dev_tab_t *p_tab = NULL;

	MDF_LOGD("receive command %s\n",  (char *)params->payload);

	MDF_ERROR_GOTO( strlen("thincloud/devices/") >= topicNameLen, Error_EXIT,
                   "publish topic too small !!\n");

	// get devid 
	p_sub = topicName + strlen("thincloud/devices/");
	MDF_ERROR_GOTO( NULL == p_sub, Error_EXIT,"Illigel topic !!\n");
	
	p_next = strstr(p_sub, "/command");
	len_devid = p_next - p_sub;
	MDF_ERROR_GOTO( ( NULL == p_next || 0 >= len_devid ), Error_EXIT, "Illigel topic !!\n");

	MDF_LOGD("receive topic %s  \n", topicName);

	// get devid.
	msg.p_devid = malloc_copy((uint8_t *) p_sub, len_devid);
	MDF_ERROR_GOTO( NULL == msg.p_devid, Error_EXIT,"Failt: Alloc device id\n");
	// todo 
	// get mac

	p_tab = dev_tab_find(NULL, msg.p_devid);
	
	if(p_tab ){
		msg.p_mac = malloc_copy(p_tab->p_mac, 6);
		MDF_ERROR_GOTO(NULL == msg.p_mac, Error_EXIT, "Failt: Alloc mac \n");
	}else{
		MDF_LOGW("Can't find tab list \n");
	}
	
	rc = command_request((char **)&msg.p_cmdid, (char **)&msg.p_method, (char **)&msg.p_data, params->payload, params->payloadLen);

	MDF_LOGD("data is %s", msg.p_data);
	MDF_ERROR_GOTO( rc != SUCCESS, Error_EXIT, "Failed to proccess command request: rc = %d", rc);
	if(msg.p_data )
		MDF_LOGD( "cmdid %s, method %s data %s \n", msg.p_cmdid, msg.p_method, msg.p_data );
	// send event 
	if( tc.evt_cmd){
		msg.cmd = TC_EVT_TC_CMD;
    	xQueueSend(tc.evt_cmd, &msg, 100/portTICK_RATE_MS);
	} else
		tc_evt_msg_clean(&msg);
	

	return ;

Error_EXIT:
	
	tc_evt_msg_clean(&msg);
	//MDF_FREE()
	return ;
}
// sub device command 
mdf_err_t tc_sub_device_command( Dev_tab_t *p_tab){

		IoT_Error_t  rc = 0;

		MDF_PARAM_CHECK( tc.p_mclient );
		MDF_PARAM_CHECK( NULL != p_tab);

		MDF_ERROR_GOTO( 0 == strlen((char *)p_tab->p_devid), Exit, "Failt to subscribe device id for there's no deviceid \n");
		
		// todo topice is null ??
		MDF_LOGW("try to subscribe device  %s command topic \n", p_tab->p_devid);
		MDF_FREE(p_tab->p_topic_sub_cmd);
		rc = topic_command_alloc( &p_tab->p_topic_sub_cmd, (char *)p_tab->p_devid);
		MDF_ERROR_GOTO(rc, Exit, "Failt to Creat Topic\n");

		MDF_FREE(p_tab->p_topic_sub_respond);
		rc = topic_respond_alloc( &p_tab->p_topic_sub_respond,(char *) p_tab->p_devid);
		MDF_ERROR_GOTO(rc, Exit, "Failt to Creat Topic\n");

		if( _TRY_LOCK_CNN( 200 ) ){
				
			MDF_LOGD("mqtt sub to %s \n", p_tab->p_topic_sub_cmd);
			// update online node.
			light_online_device_update_set(1500);
			rc =  aws_iot_mqtt_subscribe(   tc.p_mclient, p_tab->p_topic_sub_cmd, strlen(p_tab->p_topic_sub_cmd),\
										QOS0,  _tc_sub_dev_cmd_cb, NULL);
			if(rc == SUCCESS){

				p_tab->flag_sub_cmd = 1;
				MDF_LOGD("Successfully sub to %s \n", p_tab->p_topic_sub_cmd);

			}else{
				
				p_tab->flag_sub_cmd = 0;
 				MDF_LOGE("Failed to subscribe to service command topic: rc = %d \n", rc);
			}

			if( !p_tab->flag_sub_respond ){
				
				MDF_LOGD("mqtt sub to %s \n", p_tab->p_topic_sub_respond);
				rc = aws_iot_mqtt_subscribe( tc.p_mclient, p_tab->p_topic_sub_respond, strlen(p_tab->p_topic_sub_respond), \
										 QOS0, _tc_respond_cb, NULL);
					if (rc != SUCCESS){
							p_tab->flag_sub_respond = 0;
							MDF_LOGE("Failed to subscribe to service response topic: rc = %d \n", rc);
			 		}else{
			 			p_tab->flag_sub_respond = 1;
						MDF_LOGD("Successfully sub to %s \n", p_tab->p_topic_sub_respond);
					}
				}
			_UNLOCK_CNN();
		}
Exit:
		return rc;
}
// 1. 订阅 commission respond .
// 2. 发出 commission request.
mdf_err_t tc_make_commission(Dev_tab_t *p_tab){

	char rq_id[ 32] = {0};
	char *p_topic_cmis = NULL;
	mdf_err_t rc = 0;
	static int64_t o_request_tm = 0;
	int64_t ctime = utils_get_current_time_ms();

	MDF_PARAM_CHECK( NULL != p_tab);
	
	// todo remove in mesh.
	if( 1000  > DIFF(o_request_tm, ctime ) ){
		MDF_LOGI("In commissioning, Make commission too often!!\n");
		return MDF_OK;
	}
	o_request_tm = ctime;
	
	sprintf(rq_id, _MAC_STR_FORMAT, PR_MAC2STR(p_tab->p_mac) );


	MDF_PARAM_CHECK(tc.p_mclient);
	MDF_LOGW("commissioning device %s  \n", rq_id);

	MDF_FREE(p_tab->p_topic_sub_cmis);

	
	rc = topic_commission_respond_alloc( &p_tab->p_topic_sub_cmis, (char *)p_tab->p_mac, rq_id);
	MDF_ERROR_GOTO(rc, Exit, "Failt to Creat Topic\n");

	rc = topic_commission_alloc( &p_topic_cmis, (char *)p_tab->p_mac);
	MDF_ERROR_GOTO( rc ||p_topic_cmis == NULL , Exit, "Failt to Creat Topic\n");

	if( _TRY_LOCK_CNN( 200 ) ){
		rc = aws_iot_mqtt_subscribe(tc.p_mclient, p_tab->p_topic_sub_cmis, strlen(p_tab->p_topic_sub_cmis), QOS0, _tc_cmis_cb, NULL);
		MDF_LOGD("subscribe_to_commissioning_response ret = %d \n", rc);
	
		rc = send_commissioning_request( tc.p_mclient, p_topic_cmis, rq_id, (char *)p_tab->p_mac);

		MDF_LOGD("send_commissioning_request ret = %d \n", rc);
		_UNLOCK_CNN();
	}
	
	MDF_LOGD("commission dev %s is done", rq_id);

Exit:
	
	MDF_FREE(p_topic_cmis);
    return MDF_OK;
}
mdf_err_t tc_mac_commission(uint8_t *p_mac)
{
	mdf_err_t rc = MDF_OK;
	Dev_tab_t *p_tab = NULL;

	MDF_PARAM_CHECK(NULL != p_mac);

	p_tab = dev_tab_add(p_mac, NULL);
	if(p_tab){
		rc = tc_make_commission( p_tab);
	}else{
		MDF_LOGW("Failt to add tab when commission\n");
	}
	
	return rc ;
}
static mdf_err_t _subscribe_device(Dev_tab_t **pp_tab,uint8_t *p_mac, uint8_t *p_devid){
	mdf_err_t rc = MDF_OK;

	
	MDF_PARAM_CHECK( tc.p_mclient );
	MDF_PARAM_CHECK( NULL != p_mac || NULL != p_devid);
	
	Dev_tab_t *p_tab = dev_tab_add(p_mac, p_devid);
	MDF_PARAM_CHECK( NULL != p_tab );

	if( strlen((char *)p_tab->p_devid) > 0 && ( !p_tab->flag_sub_cmd || !p_tab->flag_sub_respond ) ){
		
		rc = tc_sub_device_command(p_tab);
	}

	*pp_tab = p_tab;
	
	return rc;
}
	
mdf_err_t _unsub_device(Dev_tab_t *p_tab){
	IoT_Error_t  rc = 0;

	MDF_PARAM_CHECK( tc.p_mclient );
	MDF_PARAM_CHECK( NULL != p_tab );
	// todo topice is null 
	MDF_LOGW("Try to unsubscribe to device %s topic \n", p_tab->p_devid);
	
	if( p_tab->p_topic_sub_cmd && _TRY_LOCK_CNN( 200 ) ){
		
		rc =  aws_iot_mqtt_unsubscribe(tc.p_mclient, p_tab->p_topic_sub_cmd, strlen(p_tab->p_topic_sub_cmd));
		if (rc != SUCCESS){
			MDF_LOGE("Failed to unsubscribe topic: %s rc = %d \n", p_tab->p_topic_sub_cmd, rc);
		}

		
		light_online_device_update_set(1500);
		rc =  aws_iot_mqtt_unsubscribe(tc.p_mclient, p_tab->p_topic_sub_respond, strlen(p_tab->p_topic_sub_respond));
		if (rc != SUCCESS){
			MDF_LOGE("Failed unsubscribe topic: %s rc = %d \n", p_tab->p_topic_sub_respond, rc);
		}else{
			
			MDF_LOGD("Successfully unsubscribe topic: %s \n", p_tab->p_topic_sub_respond);
		}
		_UNLOCK_CNN();
		p_tab->flag_sub_cmd = 0;
		p_tab->flag_sub_respond = 0;

		MDF_FREE(p_tab->p_topic_sub_cmd);
		p_tab->p_topic_sub_cmd = NULL;

		MDF_FREE(p_tab->p_topic_sub_respond);
		p_tab->p_topic_sub_respond = NULL;
		// todo unsubscribe to response.
	}
	
	dev_tab_del(p_tab);
	
	return rc;
}

static mdf_err_t _handle_event_commission( Tc_evt_msg_t *p_event){
	// 把 mac-deviceid 添加到 tables 中
	// 把订阅 deviceid command。
	// 通过 mac 向 mesh 下发 deviceid。
	mdf_err_t rc = MDF_OK;
	char *p_json = NULL;

	Dev_tab_t *p_tab = NULL;

	rc = _subscribe_device(&p_tab, p_event->p_mac, p_event->p_devid);

	MDF_ERROR_GOTO( NULL == p_tab || MDF_OK != rc , Exit, "Failt: subscrib device!! \n");
	
	
	MDF_LOGD("p_topic_sub_cmis %p\n", p_tab->p_topic_sub_cmis);
	if(p_tab->p_topic_sub_cmis){
		
		MDF_LOGD("subscrib topic %s \n", p_tab->p_topic_sub_cmis);
		rc = aws_iot_mqtt_unsubscribe(tc.p_mclient, p_tab->p_topic_sub_cmis, strlen( p_tab->p_topic_sub_cmis ));
		if(rc != MDF_OK)
			MDF_LOGW("Failt to unsubscribe commission respond %s \n", p_tab->p_topic_sub_cmis);
	} 
	
	mlink_json_pack(&p_json, "deviceId", p_event->p_devid);
	mlink_json_pack(&p_json, "request", "tc_set_devid");
	
	mesh_send_with_id(p_event->p_mac, p_event->p_cmdid, &p_json);

Exit:
	MDF_FREE(p_json);
	p_json = NULL;

	return rc;
}
/***
****
1. 把 msg 转换成  mesh json 的方式发送出去.
msg:
p_devid: xxx
cmdid：98d67f00-ae7d-4830-9e09-8a2767760cad 
method： update_state 
data： {"data":{"power":1,"timer":"00:00:00","name":"demo","type":"","brightness":50,"fade":1.02,"vacationmode":1,"remote_id":"02-xx","learn":true}}
目标json:
	{
	  "request":"update_state",// or other 
	  "deviceId":"xxx",
	  "cid": "98d67f00-ae7d-4830-9e09-8a2767760cad",
	  "data": {"power":1,"timer":"00:00:00","name":"demo","type":"","brightness":50,"fade":1.02,"vacationmode":1,"remote_id":"02-xx","learn":true}
	}
****/
static mdf_err_t _handle_event_command(Tc_evt_msg_t *p_evt){
	mdf_err_t rc = 0;
	char *p_json = NULL;

	MDF_PARAM_CHECK(p_evt->p_method);
	
	MDF_LOGV("Star mesh  Free heap %u\n", esp_get_free_heap_size( ) );
	MDF_LOGD("handle command event \n");
	MDF_PARAM_CHECK(p_evt->p_mac != NULL);
	MDF_PARAM_CHECK(p_evt->p_devid != NULL);
	MDF_PARAM_CHECK(p_evt->p_method != NULL);
	
	rc = mlink_json_pack( &p_json, "deviceId", p_evt->p_devid);
	MDF_ERROR_GOTO( ( NULL == p_json ), End, "Failt: json add deviceid !!\n");
	
	MDF_LOGD("json %s \n", p_json);
	
	// 处理  batch_command 命令 进行分支
	if( 0 == memcmp(p_evt->p_method, "batch_command", strlen("batch_command")) ){
		char *p_m = NULL;

		MDF_ERROR_GOTO( NULL == p_evt->p_data, End, "Failt in batch_command no payload\n");

		rc = mlink_json_parse((char *)p_evt->p_data, "method", &p_m);
		MDF_ERROR_GOTO( NULL == p_m, End, "Failt in batch_command no method\n");

		// todo 
		if( strlen(p_m) >= strlen("ota_begin")  && 0 == memcmp(p_m, "ota_begin", strlen("ota_begin")) ){
			uint8_t self_mac[6]          = {0};
			esp_wifi_get_mac(ESP_IF_WIFI_STA, p_evt->p_mac);
		}
		rc = mlink_json_pack( &p_json, "request", p_m);
		MDF_FREE(p_m);
		p_m = NULL;
	}else 
		rc = mlink_json_pack( &p_json, "request", p_evt->p_method);

	if(p_evt->p_data){		
		uint8_t *p_custom = NULL;
		//rc = mlink_json_pack( &p_json, "data", &p_data);
		rc = mlink_json_parse((char *)p_evt->p_data, "custom", &p_custom);
		if(p_custom ){
			rc = mlink_json_pack( &p_json, "data", p_custom);
			MDF_FREE(p_custom);
			p_custom = NULL;
		}else{
			rc = mlink_json_pack( &p_json, "data",  (char *)p_evt->p_data);
		}
	}

	rc = mesh_send_with_id(p_evt->p_mac, p_evt->p_cmdid, &p_json);
	if(rc != MDF_OK){
		MDF_LOGW("Failt: mesh send ret = %d\n", rc);
	}
End:

	MDF_FREE(p_json);
	MDF_LOGV("End mesh  Free heap %u\n", esp_get_free_heap_size( ) );
	return rc;
}

#if 0
void _tc_sub_dev_cmd_repd_cb(
	AWS_IoT_Client *client,
    char *topicName,
    uint16_t topicNameLen,
    IoT_Publish_Message_Params *params,
    void *data){
    
	int len_devid  = 0;
	char *p_sub =  NULL, *p_next = NULL;
	
	Tc_evt_msg_t *p_msg = MDF_MALLOC( sizeof(Tc_evt_msg_t) );

	memset(p_msg, 0, sizeof(Tc_evt_msg_t) );

	p_msg->cmd = TC_EVT_TC_CMD;
	MDF_ERROR_GOTO( topicNameLen >= strlen("thincloud/devices/"), EXIT,
                   "publish topic too small !!\n");
	// get devid 
	p_sub = topicName + strlen("thincloud/devices/");
	MDF_ERROR_GOTO( NULL == p_sub, EXIT,"Illigel topic !!\n");
	
	p_next = strstr(p_sub, "/command");
	len_devid = p_next - p_sub;
	MDF_ERROR_GOTO( ( NULL == p_next || 0 >= len_devid ), EXIT, "Illigel topic !!\n");
	
	p_msg->p_devid = MDF_MALLOC(len_devid);
	memset(p_msg->p_devid, 0, len_devid);
	memcpy(p_msg->p_devid, p_sub, len_devid);

	MDF_LOGD("get devid %s\n", p_msg->p_devid);
	MDF_LOGD("receive topic %s \n", topicName);
    command_request((char **)&p_msg->p_cmdid, (char **)&p_msg->p_method, NULL, params->payload, params->payloadLen);


EXIT:
	tc_msg_destory(p_msg);
}

#endif

// 该函数只有 root 才能触发
// 1. 检查 tab，不存在则更新。   todo 
// 2. 检查是否订阅 command 和 request, 若无订阅则直接订阅. todo 
// 3. publish 到 Thincloud.
mdf_err_t tc_send_publish(uint8_t *p_mac, uint8_t *p_devid, char *p_json){
	mdf_err_t rc = 0;
		Dev_tab_t *p_tab = NULL;

	MDF_PARAM_CHECK( tc.p_mclient );
	
	rc = _subscribe_device(&p_tab,  p_mac, p_devid);
	
	if(_TRY_LOCK_CNN( 200 )){
		rc = send_publish(tc.p_mclient, (char *)p_devid, (char *)p_json);
		_UNLOCK_CNN();
	}

	return rc;
}
// 构造 respond topic, 并把 json 发送出去.
mdf_err_t tc_send_comamnd_respond(uint8_t *p_devid, uint8_t *p_cid, char *p_json)
{

	char *p_topic = NULL;
	mdf_err_t rc = 0;
	
	IoT_Publish_Message_Params params;

	MDF_PARAM_CHECK( esp_mesh_is_root() );
	MDF_PARAM_CHECK( p_json );

	MDF_PARAM_CHECK(tc.p_mclient);

	topic_command_respond_alloc(&p_topic, (char *)p_devid, (char *)p_cid);
	MDF_ERROR_CHECK(NULL == p_topic, MDF_FAIL, "Failt: get topic\n");

	memset( &params, 0, sizeof(IoT_Publish_Message_Params) );
    params.qos = QOS0;
    params.isRetained = false;
    params.payload = (void *)p_json;
    params.payloadLen = strlen(p_json);
	

	MDF_LOGW("respond %s to topic %s \n", p_json, p_topic);
	
	if( _TRY_LOCK_CNN( 200 ) ){
    	rc =  aws_iot_mqtt_publish(tc.p_mclient, p_topic, strlen(p_topic), &params);
		_UNLOCK_CNN();
		if(rc >= 0){
			MDF_LOGD("Successfully -->send: %s\n", p_json);
			MDF_LOGD("To topic: %s \n", p_topic);
			rc = MDF_OK;
		}else{
			MDF_LOGW("Failt to publish %d\n", rc);
		}
	}else{
		MDF_LOGW("Failt: Mqtt client state is not idle when respond is being made\n");
		rc = -30;
	}

	MDF_FREE(p_topic);
	p_topic = NULL;

	return rc;
}

mdf_err_t tc_unsub_dev(uint8_t *p_mac, uint8_t *p_devid){

	mdf_err_t rc = MDF_OK;
	Dev_tab_t *p_find = dev_tab_find(p_mac, p_devid);

	if(p_find ){
		rc = _unsub_device( p_find);
	}else{
		MDF_LOGW("Failt to unsub dev. device not in root list \n");
		rc = MDF_FAIL;
	}
	return rc;
}

#if 0
// sub device command 
mdf_err_t tc_sub_device_response( uint8_t *p_devid, uint8_t *p_rq_id){

		IoT_Error_t  rc = 0;
		char p_topic_buf[MAX_TOPIC_LENGTH] = {0};

		MDF_PARAM_CHECK( tc.p_mclient );
		// todo topice is null ??
		MDF_LOGW("try to subscribe device  %s command topic \n", p_devid);
		
		if(_TRY_LOCK_CNN( 200 )){
			rc = subscribe_to_service_dev_response( tc.p_mclient, (char *)p_devid, (const char *)p_rq_id, _tc_sub_dev_cmd_cb, NULL);
			_UNLOCK_CNN();
			if (rc != SUCCESS){
 				IOT_ERROR("Failed to subscribe to service response topic: rc = %d \n", rc);
		 	}
		 	// todo subscribe to response.
		}
		return rc;
}
#endif
mdf_err_t tc_subscrib_one_device(uint8_t *p_mac, uint8_t *p_devid){
	static Dev_tab_t tab;
	dev_tab_add(p_mac, p_devid);

	memcpy(tab.p_mac, p_mac, 6);
	memcpy(tab.p_devid, p_devid, TC_ID_LENGTH);
	
	return tc_sub_device_command(&tab);
}
mdf_err_t tc_dev_add_subdev( TC_MQTT_CMD type, uint8_t *mac_list, size_t subdev_num)
{
    MDF_PARAM_CHECK(mac_list);
    MDF_PARAM_CHECK(tc.p_mclient);

    uint8_t p_mac[ MWIFI_ADDR_LEN ] = {0};
	Dev_tab_t *p_tab = NULL;

    for (int i = 0; i < subdev_num; ++i) {

		// get mac 
		memcpy(p_mac,  ( mac_list +  i * MWIFI_ADDR_LEN ), MWIFI_ADDR_LEN);

		// add to the list
		//p_tab = dev_tab_add(p_mac, NULL);

		if( p_tab  ){
			// creat get device's info event 
			Tc_evt_msg_t msg = {0};

			if( tc.evt_cmd){
				
				msg.cmd = TC_EVT_GET_DEV_INFO;
				
				msg.p_mac = malloc_copy( p_tab->p_mac, 6);
				MDF_PARAM_CHECK( NULL != msg.p_mac);
		    	xQueueSend(tc.evt_cmd, &msg, 100/portTICK_RATE_MS);
			} else
				tc_evt_msg_clean(&msg);
			
		}
    }
    return MDF_OK;
}
static mdf_err_t _handle_event_get_dev_info(Tc_evt_msg_t *p_evt){
	mdf_err_t rc = 0;

	MDF_LOGW("_handle_event_get_dev_info\n");

	return rc;
}

void tc_dev_remove(uint8_t *mac_list, size_t rm_dev_num){
    CHECK_ERR_NO_RETURN(mac_list);
    CHECK_ERR_NO_RETURN(tc.p_mclient);
	
    uint8_t p_mac[ MWIFI_ADDR_LEN ] = {0};
	Dev_tab_t *p_del = NULL;

    for( int i = 0; i < rm_dev_num; ++i) {
		
		// get mac 
		memcpy(p_mac, ( mac_list +  i * MWIFI_ADDR_LEN ), MWIFI_ADDR_LEN);
		p_del = dev_tab_find( p_mac, NULL);
		if( p_del && p_del->flag_sub_cmd && strlen( (const char*)p_del->p_devid ) > 0 ){
			_unsub_device(p_del);
		}
    }
}
#if 0
void tc_msg_publish( uint8_t *p_deviceid, uint8_t *p_data)
{
	char p_rq_id[16+1] = {0};
	random_string_creat(p_rq_id, 16);
	int rc = 0;
	
   	if( _TRY_LOCK_CNN(200) ){
        
	   /* 到这里我们获取到信号量，现在可以访问共享资源了 */
		//unsubscribe_to_service_response( tc.p_mclient, p_deviceid, p_rq_id);
		///IoT_Error_t   rc =subscribe_to_service_response( tc.p_mclient, p_deviceid, p_rq_id, _tc_sub_dev_cmd_repd_cb,NULL);
		//if (rc != SUCCESS){
	    //    IOT_ERROR("Failed to subscribe to service response topic: rc = %d", rc);
	    //}

		MDF_LOGI("Device %s publish data to  %s tc \n", p_deviceid, p_data);

		rc = send_service_request( tc.p_mclient, (char *)p_rq_id, (char *)p_deviceid,"put", (char *)p_data);

		if ( rc ){
	        IOT_ERROR("Failed to send service request : rc = %d", rc);
  	    }
		
		_UNLOCK_CNN();
   }
}
#endif

static void _tc_mqtt_disconnect(void){
	// unsub 
	Dev_tab_t *p_had = NULL, *p_el = NULL, *p_tmp = NULL;

	MDF_LOGD("Destory tc event \n");
	tc_event_deint( tc.evt_cmd );
	
	MDF_LOGD("unsubscrib all topic...\n");
	p_had = dev_tab_had_get();
    if(p_had){
		LL_FOREACH_SAFE(p_had, p_el, p_tmp){
			_unsub_device( p_el);
		}
	}

	vQueueDelete(tc.evt_cnn);
    if(tc.cnn_lock)
        vSemaphoreDelete(tc.cnn_lock);

	MDF_LOGD("Disconnect MQTT \n");
    aws_iot_mqtt_disconnect( tc.p_mclient );
	
	// todo 
	//MDF_FREE(tc.p_mclient);
	tc.cnn_lock = NULL;
    tc.evt_cnn = NULL;
    tc.p_mclient = NULL;
}
// 发送停止 loop 线程信号
mdf_err_t tc_client_destory(void){

    int evt = TC_EVT_DISCNN;

	MDF_PARAM_CHECK(tc.p_mclient);
	
    if( esp_mesh_get_layer() != MESH_ROOT )
        return MDF_OK;

	MDF_LOGW("Try to destory mqtt connect \n");
    xQueueSend(tc.evt_cnn, &evt, portMAX_DELAY);
    return MDF_OK;
}


static void tc_sntp_init(void)
{
   // ESP_LOGI(TAG, "Initializing SNTP");
    //sntp_setoperatingmode(SNTP_OPMODE_POLL);
	time_t now = 0;
	struct tm timeinfo = { 0 };
	int retry = 0, timezone_len = 0;
	const int retry_count = 10;
	char zone[64]={0};
		

    sntp_stop();
    sntp_setservername(0,	"us.pool.ntp.org");
	sntp_setservername(1,	"cn.ntp.org.cn");	//	set	server	0	by	domain	name
    sntp_setservername(2,	"ntp.sjtu.edu.cn");	//	set	server	1	by	domain	name
    sntp_init();

	// wait for time to be set
	while( timeinfo.tm_year < (2019 - 1900) && ++retry < retry_count) {
		MDF_LOGI( "Waiting for system time to be set... (%d/%d)", retry, retry_count);
		vTaskDelay( 2000 / portTICK_PERIOD_MS);
		time(&now);
		localtime_r(&now, &timeinfo );
	}
	
	if( timeinfo.tm_year >= (2019 - 1900)){
		//MDF_LOGE("sntp_stop");
		sntp_stop();
		}

	mdf_info_load("timezone", zone, 64);
	//utlis_info_load("timezone",  &timezone_len);

	if( strlen(zone) > 0)
		setenv("TZ", zone, 1);

	else{
		setenv("TZ", CNF_ZONE_, 1);
		memcpy(zone, CNF_ZONE_, strlen(CNF_ZONE_));
		mdf_info_save("timezone", zone, 64 );
	}
	
	tzset();
	local_time_printf();

}

void _tc_event_handle_init(void){
	
	tc.evt_cmd = tc_event_init();
	tc_event_function_register(TC_EVT_TC_CMIS, _handle_event_commission);
	tc_event_function_register(TC_EVT_TC_CMD, _handle_event_command);
	tc_event_function_register(TC_EVT_GET_DEV_INFO, _handle_event_get_dev_info);
}
bool tc_is_connect(void){
	return tc.connet_tc;
}

void _tc_loop_(void *p_arg){
    unsigned char recnn = 5;
    int evt = 0, yeil_error = 0;
    IoT_Error_t rc = 20;
	uint64_t old_ctime = 0, ctime = 0;
	uint8_t p_mac[6] = {0};
	
	tc.connet_tc = false;
	
    do{
			
		esp_wifi_get_mac( ESP_IF_WIFI_STA, p_mac);
        rc = client_connect( &tc.p_mclient, (char *)p_mac);
		vTaskDelay( 2000 / portTICK_RATE_MS );
		MDF_LOGI("Building mqtt connection  rc = %d \n", rc);
		recnn--;
        if( 0 == recnn )
			esp_restart();
    }while( rc != SUCCESS );
	
	light_status_set( LSYS_STATUS_ONLINE );
	MDF_LOGD("Creat mqtt queue !!\n");
    tc.evt_cnn =  xQueueCreate(3, sizeof(int));

	_tc_event_handle_init();
	
	// build lock
    if( NULL == tc.cnn_lock)
		tc.cnn_lock = xSemaphoreCreateMutex();

	tc.connet_tc = true;
	mesh_event_table_update();
	
	vTaskDelay( 100 / portTICK_RATE_MS );
	//light_online_device_update_set(3000);
    while( yeil_error < 4){
		
		ctime = utils_get_current_time_ms();

		if(DIFF( old_ctime,  ctime) > 2000 && _TRY_LOCK_CNN(200) ){
        	rc = aws_iot_mqtt_yield( tc.p_mclient, 100);
			
			if(rc < 0 ){
				MDF_LOGE("MQTT Failt %d \n", rc);
				yeil_error++;
			}
			
			_UNLOCK_CNN();
			
			old_ctime = ctime;
		}
		
		if( xQueueReceive( tc.evt_cnn, &evt, ( TickType_t )10) == pdPASS ){
            if( evt == TC_EVT_DISCNN){
                break;
            	}
        }
		//vTaskDelay( 50/ portTICK_RATE_MS );
		vTaskDelay( 20 / portTICK_RATE_MS);
		handle_tc_event(tc.evt_cmd);
		//vTaskDelay( 50/ portTICK_RATE_MS );
		//handle_tc_event(tc.evt_cmd);
    }
    g_tc_mqtt_task_handle = NULL;
	_tc_mqtt_disconnect();	
}
void _tc_loop(void *p_arg){

	tc_sntp_init();
	/****** network init ***************/
	//tc_certs_init();

	while( esp_mesh_is_root() /*** && light_status_get() >= LSYS_STATUS_CNN **/ ){
	//if(esp_mesh_is_root()){
		_tc_loop_(p_arg);
		vTaskDelay( 4000 / portTICK_RATE_MS );
	}
	//tc_certs_deinit();
	vTaskDelete(NULL);
}

// 建立 mqtt 连接
// 并建立一个loop 线程
mdf_err_t tc_client_creat(void){

    esp_log_level_set(TAG, ESP_LOG_DEBUG);

	if(!g_tc_mqtt_task_handle){
		if( esp_mesh_get_layer() != MESH_ROOT )
				return MDF_OK;
			
			MDF_LOGD("Try to Creat tc client !!\n");
		
			if(tc.evt_cnn){
				tc_client_destory(); 
				do{
					vTaskDelay( 100/ portTICK_RATE_MS );
				}while(tc.evt_cnn || tc.p_mclient); 	
			}
		
			// build thread 
			//xTaskCreatePinnedToCore()
			xTaskCreatePinnedToCore(_tc_loop, "aws_tc_task", 9 * 1024,NULL, CONFIG_MDF_TASK_DEFAULT_PRIOTY + 1, &g_tc_mqtt_task_handle, 1);
			//xTaskCreate(_tc_loop, "aws_tc_task", 10 * 1024,NULL, 2 | portPRIVILEGE_BIT, NULL);

	}
    
    return MDF_OK;
}


/*
 * Copyright 2019 Yonomi, Inc. or its affiliates. All Rights Reserved.
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "thincloud.h"

/*
 * Thincloud C Embedded SDK
 * 
 * Contains utilities to construct ThinCloud standard topics
 * and marshal and unmarshal request and responses.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>


#include "mdf_mem.h"

#include "mlink.h"
#include "utlis.h"

#include "aws_iot_log.h"
#include "aws_iot_error.h"
#include "aws_iot_mqtt_client_interface.h"
#include "utlis.h"
#include "light_device_config.h" 
#include "utlis_store.h"

static const char Factory_host[] = "a3fakvq39qvu3f-ats.iot.us-west-2.amazonaws.com";
//static const char Factory_host[] = "a2tiwp97uwkdze-ats.iot.us-east-1.amazonaws.com";
static const char *TAG                   = "thincloud";
static const char _format_topic_commission[]="thincloud/registration/%s_%s/requests";
static const char _format_topic_commission_respond[] = "thincloud/registration/%s_%s/requests/%s/response";

static const char _format_topic_command[]="thincloud/devices/%s/command";
static const char _format_topic_command_respond[]="thincloud/devices/%s/command/%s/response";

static const char _format_topic_respond[]="thincloud/devices/%s/requests/#";
static const char _format_topic_server_request[]="thincloud/devices/%s/requests";

static char *_phy_id_format_get(void){
	if( utils_info_len_get("certificate_crt") > 0 && utils_info_len_get("private_key") > 0){
		return _PY_ID_FORMAT;
	}else
		return _OLD_PY_ID_FORMAT;
}
/**
 * @brief Build a commission request topic
 * 
 * Constructs a commission request topic of the format
 *       "thincloud/registration/{deviceType}_{physicalId}/requests"
 *
 * @param[out]  pp_topic    pointer  Pointer to a string buffer to write to.
 * @param[in]   deviceType  Devices's device type.
 * @param[in]   physicalId  Device's physical ID.
 * 
 * @return Zero on success, negative value otherwise 
 */
IoT_Error_t topic_commission_alloc(char **pp_topic, const char *p_mac)
{
	char *p = NULL, p_physicalid[32] = {0};
	int len = 0;
	
	sprintf(p_physicalid, _PY_ID_FORMAT, PR_MAC2STR(p_mac) );
	MDF_PARAM_CHECK( NULL != pp_topic );

	len = strlen(_format_topic_commission) + strlen(CNF_YONOMI_DEV_TYPE) + strlen(p_physicalid) + 4;
	p =  MDF_MALLOC( len);

	MDF_ERROR_CHECK( NULL == p, MDF_ERR_NO_MEM, "Error: Malloc failt!!\n");
	memset(p, 0, len);
	
    sprintf(p,  _format_topic_commission, CNF_YONOMI_DEV_TYPE, p_physicalid);
	*pp_topic = p;
    FUNC_EXIT_RC(SUCCESS);
}
IoT_Error_t topic_commission_respond_alloc(char **pp_topic, const char *p_mac, const char *p_rq_id)
{
	char *p = NULL, p_physicalid[32] = {0};
	int len = 0;

	
	sprintf(p_physicalid, _PY_ID_FORMAT, PR_MAC2STR(p_mac) );
	MDF_PARAM_CHECK( NULL !=  pp_topic  );

	len = strlen(_format_topic_commission_respond) + strlen(CNF_YONOMI_DEV_TYPE) + strlen(p_physicalid) + 4;
	p =  MDF_MALLOC( len);

	MDF_ERROR_CHECK( NULL == p, MDF_ERR_NO_MEM, "Error: Malloc failt!!\n");
	memset(p, 0, len);
	
    sprintf(p,  _format_topic_commission_respond, CNF_YONOMI_DEV_TYPE, p_physicalid, p_rq_id);
	*pp_topic = p;
    FUNC_EXIT_RC(SUCCESS);
}

IoT_Error_t topic_command_alloc(char **pp_topic, const char *p_devid)
{
	char *p = NULL;
	int len = 0;
	

	len = strlen(_format_topic_command)  + strlen(p_devid) + 4;
	p =  MDF_MALLOC( len);

	MDF_ERROR_CHECK( NULL == p, MDF_ERR_NO_MEM, "Error: Malloc failt!!\n");
	memset(p, 0, len);
	
    sprintf(p,  _format_topic_command, p_devid);
	*pp_topic = p;
    FUNC_EXIT_RC(SUCCESS);
}
IoT_Error_t topic_command_respond_alloc(char **pp_topic, const char *p_devid, const char *p_cid)
{
	char *p = NULL;
	int len = 0;
	
	len = strlen(_format_topic_command_respond)  + strlen(p_devid) + strlen(p_cid) + 4;
	p =  MDF_MALLOC( len);

	MDF_ERROR_CHECK( NULL == p, MDF_ERR_NO_MEM, "Error: Malloc failt!!\n");
	memset(p, 0, len);
	
    sprintf(p,  _format_topic_command_respond, p_devid, p_cid);
	*pp_topic = p;
    FUNC_EXIT_RC(SUCCESS);
}

IoT_Error_t topic_respond_alloc(char **pp_topic, const char *p_devid)
{
	char *p = NULL;
	int len = 0;

	len = strlen(_format_topic_respond)  + strlen(p_devid) + 4;
	p =  MDF_MALLOC( len);

	MDF_ERROR_CHECK( NULL == p, MDF_ERR_NO_MEM, "Error: Malloc failt!!\n");
	memset(p, 0, len);
	
    sprintf(p,  _format_topic_respond, p_devid);
	*pp_topic = p;
    FUNC_EXIT_RC(SUCCESS);
}

IoT_Error_t topic_server_request_alloc(char **pp_topic, const char *p_devid)
{
	char *p = NULL;
	int len = 0;
	MDF_PARAM_CHECK( pp_topic && p_devid );

	len = strlen(_format_topic_server_request)  + strlen(p_devid) + 4;
	p =  MDF_MALLOC( len);

	MDF_ERROR_CHECK( NULL == p, MDF_ERR_NO_MEM, "Error: Malloc failt!!\n");
	memset(p, 0, len);
	
    sprintf(p,  _format_topic_server_request, p_devid);
	*pp_topic = p;
    FUNC_EXIT_RC(SUCCESS);
}
/** 构造command 回应的json, 当 status code != 200 则构造携带 message 结构
 "thincloud/devices/{deviceId}/command/{commandId}/response"
正常回应:
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
存在问题，需要上报错误：
{
  "id": "0f4f6971-ccc3-4a44-8b5d-2379912e39e0",
  "result": {
    "statusCode": 200,
    "message": {
        "reason":"NO such device!!"
    }
  },
}

****/
IoT_Error_t command_response_json_alloc(char **pp_json, 
		const char *p_cid, uint16_t status_code, char *p_msg)
{
	char *p_ss_json = NULL;
	char *p_body_key = NULL;
	
	MDF_PARAM_CHECK(pp_json);
	MDF_PARAM_CHECK(p_cid);

	mlink_json_pack(&p_ss_json, "statusCode", status_code);
	if( 200 == status_code ){
		// 正常huiying
		p_body_key = "body";
	}else{
		// 回应错误提示
		p_body_key = "message";
	}
	
	if(p_msg)
		mlink_json_pack( &p_ss_json, p_body_key, p_msg);

	mlink_json_pack( pp_json, "id", p_cid);
	mlink_json_pack( pp_json, "result", p_ss_json);

	MDF_FREE(p_ss_json);
	return MDF_OK;
}
/**
 * @brief Build a commission request topic
 * 
 * Constructs a commission request topic of the format
 *       "thincloud/registration/{deviceType}_{physicalId}/requests"
 *
 * @param[out]  buffer      Pointer to a string buffer to write to.
 * @param[in]   deviceType  Devices's device type.
 * @param[in]   physicalId  Device's physical ID.
 * 
 * @return Zero on success, negative value otherwise 
 */
IoT_Error_t commission_request_topic(char *buffer, const char *deviceType, const char *physicalId)
{
    if (buffer == NULL)
    {
       return 0;
    }
    if (deviceType == NULL || physicalId == NULL)
    {
        FUNC_EXIT_RC(NULL_VALUE_ERROR);
    }
    sprintf(buffer, "thincloud/registration/%s_%s/requests", deviceType, physicalId);
    FUNC_EXIT_RC(SUCCESS);
}

/**
 * @brief Build a commission response topic
 * 
 * Constructs a commission response topic of the format
 *       "thincloud/registration/{deviceType}_{physicalId}/requests/{requestId}/response"
 * 
 * @param[out]  buffer      Pointer to a string buffer to write to.
 * @param[in]   deviceType  Devices's device type.
 * @param[in]   physicalId  Device's physical ID.
 * @param[in]   requestId   Unique ID for the request.
 * 
 * @return Zero on success, negative value otherwise 
 */
IoT_Error_t commission_response_topic(char *buffer, const char *deviceType, const char *physicalId, const char *requestId)
{
    if (buffer == NULL)
    {
        return 0;
    }

    if (deviceType == NULL || physicalId == NULL || requestId == NULL)
    {
        FUNC_EXIT_RC(NULL_VALUE_ERROR);
    }

    sprintf(buffer, "thincloud/registration/%s_%s/requests/%s/response", deviceType, physicalId, requestId);

    FUNC_EXIT_RC(SUCCESS);
}

/**
 * @brief Build a command request topic
 * 
 * Constructs a command request topic of the format
 *       "thincloud/devices/{deviceId}/command"
 * 
 * @param[out]  buffer    Pointer to a string buffer to write to.
 * @param[in]   deviceId  Device's ID.
 * 
 * @return Zero on success, negative value otherwise 
 */
IoT_Error_t command_request_topic(char *buffer, const char *deviceId)
{
    if (buffer == NULL)
    {
        return 0;
    }

    if (deviceId == NULL)
    {
        FUNC_EXIT_RC(NULL_VALUE_ERROR);
    }

    sprintf(buffer, "thincloud/devices/%s/command", deviceId);

    FUNC_EXIT_RC(SUCCESS);
}

/**
 * @brief Build a command response topic
 * 
 * Constructs a command response topic of the format
 *       "thincloud/devices/{deviceId}/command/{commandId}/response"
 * 
 * @param[out]  buffer    Pointer to a string buffer to write to.
 * @param[in]   deviceId  Devices's ID.
 * @param[in]   commandId  Command request's ID.
 * 
 * @return Zero on success, negative value otherwise 
 */
IoT_Error_t command_response_topic(char *buffer, const char *deviceId, const char *commandId)
{
    if (buffer == NULL)
    {
        return 0;
    }

    if (deviceId == NULL || commandId == NULL)
    {
        FUNC_EXIT_RC(NULL_VALUE_ERROR);
    }

    sprintf(buffer, "thincloud/devices/%s/command/%s/response", deviceId, commandId);

    FUNC_EXIT_RC(SUCCESS);
}

/**
 * @brief Build a service request topic
 * 
 * Constructs a service request topic of the format
 *       "thincloud/devices/{deviceId}/requests"
 * 
 * @param[out]  buffer    Pointer to a string buffer to write to.
 * @param[in]   deviceId  Devices's ID.
 * 
 * @return Zero on success, negative value otherwise 
 */
IoT_Error_t service_request_topic(char *buffer, const char *deviceId)
{
    if (buffer == NULL)
    {
        return 0;
    }

    if (deviceId == NULL)
    {
        FUNC_EXIT_RC(NULL_VALUE_ERROR);
    }

    sprintf(buffer, "thincloud/devices/%s/requests", deviceId);

    FUNC_EXIT_RC(SUCCESS);
}


/**
 * @brief Build a service response topic
 * 
 * Constructs a service response topic of the format
 *       "thincloud/devices/{deviceId}/requests/%s/response"
 * 
 * @param[out] buffer     Pointer to a string buffer to write to.
 * @param[in]  deviceId   Devices's ID.
 * @param[in]  requestId  Request's ID.
 * 
 * @return Zero on success, negative value otherwise 
 */
IoT_Error_t service_response_topic(char *buffer, const char *deviceId, const char *requestId)
{
    if (buffer == NULL)
    {
        return 0;
    }

    if (deviceId == NULL || requestId == NULL)
    {
        FUNC_EXIT_RC(NULL_VALUE_ERROR);
    }

    sprintf(buffer, "thincloud/devices/%s/requests/%s/response", deviceId, requestId);

    FUNC_EXIT_RC(SUCCESS);
}

/**
 * @brief Build a commissioning request
 * 
 * Construct a commissioning request.
 * 
 * @param[out] pp_buffer   Pointer to a string buffer to write to ( u have to free them after used) 
 * @param[in]  requestId   Unique ID for the request
 * @param[in]  deviceType  Requesting device's type
 * @param[in]  physicalId  Device's physical ID
 * 
 * @return Zero on success, negative value otherwise 
 */
IoT_Error_t commissioning_request(char **pp_buffer, const char *requestId, const char *deviceType, const char *physicalId)
{
    char *p_str_json = NULL, *p_data = NULL, *p_dataObj = NULL, *p_params = NULL;

    if (deviceType == NULL || physicalId == NULL){
        FUNC_EXIT_RC(NULL_VALUE_ERROR);
    }
    
    if (requestId != NULL){
        mlink_json_pack(&p_str_json, "id", requestId);
    }

    mlink_json_pack(&p_str_json, "method", "commission");

    if(deviceType)
        mlink_json_pack(&p_data, "deviceType", deviceType);
    
    if(physicalId)
        mlink_json_pack(&p_data, "physicalId", physicalId);
    
    if(p_data)
        mlink_json_pack(&p_dataObj, "data", p_data);

    MDF_FREE(p_data);
    p_data = NULL;

    p_params = MDF_MALLOC( 4 + strlen( p_dataObj ) );
    if( !p_params){
        MDF_LOGE("Malloc Failt !!\n");
        return FAILURE;
    }

    memset(p_params, 0, 4 + strlen( p_dataObj ) );
    p_params[0] = '[';
    memcpy( &p_params[1], p_dataObj, strlen(p_dataObj));
    p_params[1 + strlen( p_dataObj ) ] = ']';
    
    MDF_FREE(p_dataObj);
    p_dataObj = NULL;

    mlink_json_pack(&p_str_json, "params", p_params);
 
    MDF_FREE(p_params);
    p_params = NULL;

    *pp_buffer = p_str_json;

    FUNC_EXIT_RC(SUCCESS);
}

/**
 * @brief Unmarshall a commissioning response 
 * 
 * Unmarshall a commissioning response from a string stream.
 * 
 * @param[out]  deviceId    Assigned device ID.
 * @param[out]  statusCode  Commissioning status.
 * @param[out]  requestId   ID of the original request.
 * @param[in]   payload     Response payload.
 * @param[in]   payloadLen  Reponse payload's length.
 * 
 * @return Zero on success, a negative value otherwise 
 */
IoT_Error_t commissioning_response(char **pp_deviceId, uint16_t *statusCode, char **pp_requestId, char *payload, uint16_t payloadLen)
{
    char *p_res = NULL;
	int ret = 0, code;
	
    if (payload == NULL || payloadLen == 0)
    {
        FUNC_EXIT_RC(SUCCESS);
    }

	ret = mlink_json_parse(payload, "id", pp_requestId);
    MDF_ERROR_GOTO(ret < 0,  EXIT, "Get requestIdValue from  json formatted !!");

    ret = mlink_json_parse(payload, "result", &p_res);
    MDF_ERROR_GOTO(ret < 0,  EXIT, "Get result from  json !!");

	mlink_json_parse(p_res, "statusCode", &code);
	*statusCode =(uint16_t) code;
	ret = mlink_json_parse(p_res, "deviceId", pp_deviceId);

	MDF_FREE(p_res);
	MDF_ERROR_GOTO(ret < 0,  EXIT, "Get deviceId from  json formatted !!");


    FUNC_EXIT_RC(SUCCESS);

EXIT:
	
    FUNC_EXIT_RC(FAILURE);
}

/**
 * @brief Marshal a command response
 * 
 * Marshal a command response into a JSON string.
 * 
 * @param[out]  buffer           Pointer to a string buffer to write to.
 * @param[in]   requestId        Command request's ID.
 * @param[in]   statusCode       Command's response status.
 * @param[in]   isErrorResponse  Signals if a command responds with an error.
 * @param[in]   errorMessage     Response error message. Only used if isErrorResponse is set to true.
 * @param[in]   body             Command response body.
 * 
 * @return Zero on success, a negative value otherwise 
 */
IoT_Error_t command_response(char *buffer, const char *requestId, uint16_t statusCode, bool isErrorResponse, char *errorMessage, char *body)
{
	char *obj = NULL;
    char *statusCodeValue = NULL;

    if (requestId != NULL)
    {
       mlink_json_pack(&obj,"id", requestId);
    }

	mlink_json_pack(&statusCodeValue,"statusCode", statusCode);

    if (isErrorResponse)
    {
       
        if (errorMessage != NULL)
        {
           mlink_json_pack(&statusCodeValue,"message",errorMessage);
        }

    }
    else
    {

        if (body != NULL)
        {
            mlink_json_pack(&statusCodeValue, "body",body);
        }
			
    }
	mlink_json_pack(&obj,"result",statusCodeValue);
	MDF_FREE(statusCodeValue);

	if(strlen(obj) < MAX_BODY_LENGTH)
    	strcpy(buffer, obj);
	else
		MDF_LOGW( "too larg for len = %d",  strlen( obj ) );

	MDF_FREE(obj);
    FUNC_EXIT_RC(SUCCESS);
}

char *_command_response(const char *requestId, uint16_t statusCode, bool isErrorResponse, char *errorMessage, char *body)
{
    char *obj = NULL;
    char *statusCodeValue = NULL;

    if (requestId != NULL)
    {
       mlink_json_pack(&obj,"id", requestId);
    }
    
	mlink_json_pack(&statusCodeValue,"statusCode", statusCode);

    if (isErrorResponse)
    {
       
        if (errorMessage != NULL)
        {
           mlink_json_pack(&statusCodeValue,"message",errorMessage);
        }

    } else
    {

        if (body != NULL)
        {
            mlink_json_pack(&statusCodeValue, "body",body);
        }
			
    }
	
	mlink_json_pack(&obj,"result",statusCodeValue);
	MDF_FREE(statusCodeValue);
	return obj;
}

/**
 * @brief Unmarshall a command request payload 
 * 
 * Unmarshall a command request from a string payload. 
 * 
 * @param[out]  requestId   ID of the original request.
 * @param[out]  method      Command method.
 * @param[out]  params      Command request parameters.
 * @param[in]   payload     Response payload.
 * @param[in]   payloadLen  Reponse payload's length.
 * 
 * @return Zero on success, a negative value otherwise 
 */
IoT_Error_t command_request(char **pp_requestId, char **pp_method, char **pp_data, const char *payload, const unsigned int payloadLen)
{
    if ( payload == NULL || payloadLen == 0)
    {
        FUNC_EXIT_RC(SUCCESS);
    }

	mlink_json_parse(payload, "id", pp_requestId);
    mlink_json_parse(payload, "method", pp_method);
#if 1
    if ( pp_data != NULL){
		char *p_data = NULL;
		
        int rc = utlis_json_array_get_item((void **)&p_data, payload, "params", "data");
		if(p_data){
			mlink_json_parse(p_data, "data", pp_data);
			MDF_FREE(p_data);
			p_data = NULL;
		}
        if (rc != ESP_OK)
        {
        	
            FUNC_EXIT_RC(JSON_PARSE_ERROR);
        }
    }
#endif
    FUNC_EXIT_RC(SUCCESS);
}

/**
 * @brief Marshal a service request
 * 
 * Marshal a service request to a JSON string.
 * 
 * @param[out]  buffer     Pointer to a string buffer to write to.
 * @param[in]   requestId  Unique ID for the request.
 * @param[in]   method     Service method to request.
 * @param[in]   params     Service request parameters.
 * 
 * @return Zero on success, a negative value otherwise 
 */
IoT_Error_t service_request(char **pp_buffer, const char *requestId, const char *method, char *params)
{
    if(requestId != NULL )
    	mlink_json_pack(pp_buffer,"id", requestId);

	mlink_json_pack(pp_buffer, "method", method);

	if (params != NULL)
		mlink_json_pack(pp_buffer,"params", params);
	if(strlen(*pp_buffer) > MAX_BODY_LENGTH){
		MDF_LOGE("Sservice request too big > %d \n", MAX_BODY_LENGTH );
		MDF_FREE( *pp_buffer);
	    FUNC_EXIT_RC(FAILURE);
	}else
    FUNC_EXIT_RC(SUCCESS);
}
char *_service_request(const char *requestId, const char *method, char *params)
{
    char* str = NULL;
    if( requestId )
    	mlink_json_pack( &str, "id", requestId);

	if(method)
		mlink_json_pack(&str, "method", method);
	
    if( params )
		mlink_json_pack(&str,"params", params);

	return str;
}

/**
 * @brief Unmarshall a service response payload 
 * 
 * Unmarshall a service response from a string payload. 
 * 
 * @param[out]  requestId   Request ID of the original request.
 * @param[out]  statusCode  Request's status.
 * @param[out]  data        Response's data.
 * @param[in]   payload     Response payload.
 * @param[in]   payloadLen  Reponse payload's length.
 * 
 * @return Zero on success, a negative value otherwise 
 */
IoT_Error_t service_response(char *requestId, uint16_t *statusCode, char **pp_data, const char *payload, const unsigned int payloadLen)
{
    char *p_result = NULL;
    if (payload == NULL || payloadLen == 0)
    {
        FUNC_EXIT_RC(SUCCESS);
    }


    if ( requestId != NULL )
    {
        mlink_json_parse(payload, "id", requestId);
    }

    mlink_json_parse(payload, "result", &p_result);

    if (p_result != NULL)
    {
        int rc =  0;
        rc = mlink_json_parse(p_result, "statusCode", statusCode);
        rc = mlink_json_parse(p_result, "body",  pp_data);

        MDF_FREE(p_result);

        if (rc < 0)
        {
            FUNC_EXIT_RC(JSON_PARSE_ERROR);
        }
    }

    FUNC_EXIT_RC(SUCCESS);
}

/**
 * @brief Send a command response.
 * 
 * Publish a command response to MQTT.
 * 
 * @param[in]  client           AWS IoT MQTT Client instance.
 * @param[in]  deviceId         Device's ID.
 * @param[in]  commandId        ID of the requested command.
 * @param[in]  statusCode       Command's status code.
 * @param[in]  isErrorResponse  Signals if a command responds with an error.
 * @param[in]  errorMessage     Response error message. Only used if isErrorResponse is set to true.
 * @param[in]  body             Command response body.
 * 
 * @return Zero on success, negative value otherwise 
 */
IoT_Error_t send_command_response(AWS_IoT_Client *client, const char *deviceId, const char *commandId, uint16_t statusCode, bool isErrorResponse, char *errorMessage, char *body)
{
	int ret = SUCCESS;
    char topic[MAX_TOPIC_LENGTH] = {0};
	char *p_resp = NULL;
	memset(topic, 0, MAX_TOPIC_LENGTH);
	if( NULL == deviceId || NULL == client)
		return FAILURE;
		
	MDF_LOGD("device id %s \n",  deviceId);
	MDF_LOGD("commandId %s \n", commandId);
	IoT_Error_t rc = command_response_topic(topic, deviceId, commandId);
	MDF_LOGD("topic %s \n", topic);
    if (rc != SUCCESS){
        FUNC_EXIT_RC(rc);
    }
	
	MDF_LOGD(" command respond topic: %s \n", topic);

   if(body)
		MDF_LOGV("body : %s \n", body);

   //char *p_tmp = "{\"key1\":11,\"key2\":22}";
	p_resp = _command_response(commandId, statusCode, isErrorResponse, errorMessage, body);
   	if( NULL == p_resp){
		FUNC_EXIT_RC(SHADOW_JSON_ERROR);
	}
	if( strlen(p_resp) > MAX_BODY_LENGTH ){
		MDF_LOGE("package too lagre !!");
		FUNC_EXIT_RC(SHADOW_JSON_ERROR);
	}
	MDF_LOGD("command respond: %s\n", p_resp);
	
    IoT_Publish_Message_Params params;
	memset( &params, 0, sizeof(IoT_Publish_Message_Params) );
    params.qos = QOS0;
    params.isRetained = false;
    params.payload = (void *)p_resp;
    params.payloadLen = strlen(p_resp);
	
    MDF_LOGV("-->send: %s\n", p_resp);

    ret =  aws_iot_mqtt_publish(client, topic, strlen(topic), &params);
	MDF_LOGD("publish to aws result %d\n", (int)ret);

	//vTaskDelay( 20 / portTICK_PERIOD_MS );
	MDF_FREE( p_resp );
	return ret;
}
/**
 * @brief Send commissioning request
 * 
 * Publish a commissioning request to MQTT.
 * 
 * @param[in]  client      AWS IoT MQTT Client instance.
 * @param[in]  requestId   Unique ID for the request.
 * @param[in]  deviceType  Devices's device type.
 * @param[in]  physicalId  Device's physical ID.
 * 
 * @return Zero on success, negative value otherwise 
 */
IoT_Error_t send_commissioning_request(AWS_IoT_Client *client, const char *p_topic, const char *requestId, const char *p_mac)
{
    IoT_Publish_Message_Params params;
    char *p_payload = NULL;//MAX_JSON_TOKEN_EXPECTED
	char p_physicalid[64] = {0};
    IoT_Error_t rc = 0;
	
	MDF_PARAM_CHECK( p_topic);
	sprintf(p_physicalid, _PY_ID_FORMAT, PR_MAC2STR(p_mac) );

    rc = commissioning_request(&p_payload, requestId, CNF_YONOMI_DEV_TYPE, p_physicalid);

    if (rc != SUCCESS)
    {
        FUNC_EXIT_RC(rc);
    }
	
    params.qos = QOS0;
    params.isRetained = false;
    params.payload = (void *)p_payload;
    params.payloadLen = strlen( p_payload );
    rc = aws_iot_mqtt_publish(client, p_topic, strlen( p_topic ), &params);

	MDF_LOGD("commission %s to %s \n", p_payload, p_topic );
	if(rc != SUCCESS){
		MDF_LOGE( "Failt to commission request to %s\n", p_topic );
	}else
		MDF_LOGI( "Successfully to commission request to %s\n", p_topic );

	MDF_FREE(p_payload);
    
    return rc;
}


/**
 * @brief Send service request
 * 
 * Publish a service request to MQTT.
 * 
 * @param[in]  client     AWS IoT MQTT Client instance.
 * @param[in]  requestId  Unique ID for the request.
 * @param[in]  deviceId   Devices's ID.
 * @param[in]  method     Service method to request.
 * @param[in]  params     Service request parameters.
 * 
 * @return Zero on success, negative value otherwise 
 */
IoT_Error_t send_service_request(AWS_IoT_Client *client, char *requestId, char *deviceId, char *method, /*json_object *reqParams*/char *reqParams)
{
    char topic[MAX_TOPIC_LENGTH];
	IoT_Error_t ret;
	
    IoT_Error_t rc = service_request_topic(topic, deviceId);

    if (rc != SUCCESS)
    {
        FUNC_EXIT_RC(rc);
    }

   // char payload[MAX_JSON_TOKEN_EXPECTED];
	///char payload[MAX_BODY_LENGTH];
    //rc = service_request(payload, requestId, method, reqParams);
	char *p_head_data = _service_request(requestId, method, reqParams);
	
	if( NULL == p_head_data)
		FUNC_EXIT_RC(NULL_VALUE_ERROR);
	
	if( strlen( p_head_data ) > MAX_BODY_LENGTH || (reqParams && strlen(reqParams) + strlen(p_head_data) > MAX_BODY_LENGTH  ) ){
		MDF_FREE(p_head_data);
		FUNC_EXIT_RC(MAX_SIZE_ERROR);
	}

    IoT_Publish_Message_Params params;
    params.qos = QOS0;
    params.isRetained = false;
    params.payload = (void *)p_head_data;
    params.payloadLen = strlen(p_head_data);
	//printf("send data:%s\n",payload);
    ret =  aws_iot_mqtt_publish(client, topic, strlen(topic), &params);
	MDF_FREE( p_head_data );
	p_head_data = NULL;
	
	return ret;
}

/**
 * @brief Send service request
 * 
 * 直接 Publish a service request to MQTT.
 * 
 * @param[in]  client     AWS IoT MQTT Client instance.
 * @param[in]  p_json  包含 topic json data.
 {
	"request": "tc_publish",
	"topic":"thincloud/devices/%s/requests",
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
 * @return Zero on success, negative value otherwise 
 */
IoT_Error_t send_publish(AWS_IoT_Client *client, char *p_devid, char *p_json)
{
	IoT_Error_t rc = 0;
	char *p_data = NULL;
	char p_topic[128] = {0};
	IoT_Publish_Message_Params params;


	// get topic 
	// todo 
	service_request_topic(p_topic, (const char *)p_devid);
	MDF_ERROR_GOTO( strlen(p_topic) == 0, Err_Exit, "Json No topic!\n");
	
	// get data
	rc = mlink_json_parse(p_json, "data", &p_data);
	MDF_ERROR_GOTO(MDF_OK != rc, Err_Exit, "Json No data!\n");
		
    params.qos = QOS0;
    params.isRetained = false;
    params.payload = (void *)p_data;
    params.payloadLen = strlen(p_data);
	//printf("send data:%s\n",payload);
    rc =  aws_iot_mqtt_publish(client, p_topic, strlen(p_topic), &params);
	if(rc != MDF_OK ){
		MDF_LOGE("Failt to Publish to %s rc = %d !!\n", p_topic, rc);
	}else 
		MDF_LOGI("Successfully to Publish %s to %s \n", p_data, p_topic);
	
Err_Exit:

	MDF_FREE(p_data);
	p_data = NULL;
	
	return rc;
}

/**
 * @brief Subscribe to commissioning respones
 * 
 * @param[in]  client          AWS IoT MQTT Client instance.
 * @param[in]  requestId       Unique ID of a request.
 * @param[in]  deviceType      Devices's device type.
 * @param[in]  physicalId      Device's physical ID.
 * @param[in]  handler         Subscription response handler.
 * @param[in]  subscriberData  Data blob to be passed to the subscription handler on invoke.
 * 
 * @return Zero on success, negative value otherwise 
 */
IoT_Error_t subscribe_to_commissioning_response(AWS_IoT_Client *client, char *p_topic, const char *requestId, const char *deviceType, const char *physicalId, pApplicationHandler_t handler, void *subscribeData)
{
    IoT_Error_t rc = commission_response_topic(p_topic, deviceType, physicalId, requestId);

    if (rc != SUCCESS)
    {
        FUNC_EXIT_RC(rc);
    }
	printf("subscribe TOPIC:%s\r\n",p_topic);

    return aws_iot_mqtt_subscribe(client, p_topic, strlen(p_topic), QOS0, handler, subscribeData);
}

IoT_Error_t unsubscribe_to_commissioning_response(AWS_IoT_Client *client, char *p_topic, const char *requestId, const char *deviceType, const char *physicalId)
{
 
  
  IoT_Error_t rc = commission_response_topic(p_topic, deviceType, physicalId, requestId);
  
  if (rc != SUCCESS)
  {
	  FUNC_EXIT_RC(rc);
  }

  return   aws_iot_mqtt_unsubscribe(client,p_topic, strlen(p_topic));
}


/**
 * @brief Subscribe to command requests
 * 
 * @param[in]  client          AWS IoT MQTT Client instance.
 * @param[in]  deviceId        Device's ID.
 * @param[in]  handler         Subscription response handler.
 * @param[in]  subscriberData  Data blob to be passed to the subscription handler on invoke.
 * 
 * @return Zero on success, negative value otherwise 
 */
IoT_Error_t subscribe_to_command_request(AWS_IoT_Client *client, char *topic, const char *deviceId, pApplicationHandler_t handler, void *subscribeData)
{
	
	IoT_Error_t rc = command_request_topic(topic, deviceId);

    if (rc != SUCCESS)
    {
        FUNC_EXIT_RC(rc);
    }
	MDF_LOGD("Mqtt subscribe to %s \n", topic);
    return aws_iot_mqtt_subscribe(client, topic, strlen(topic), QOS0, handler, subscribeData);
}
// delay
IoT_Error_t unsubscribe_to_command_request(AWS_IoT_Client *client, char* topic, const char *deviceId){
	
	IoT_Error_t rc = command_request_topic(topic, deviceId);

    if (rc != SUCCESS)
    {
        FUNC_EXIT_RC(rc);
    }
	return aws_iot_mqtt_unsubscribe(client, topic, strlen(topic));
}
/**
** As a general rule, you can subscribe a device to its generic requests topic like that: 
** thincloud/devices/{deviceId}/requests/#
** All messages under that topic will come through that subscription, 
** including those responses. This is the recommended approach for a device to subscribe to requests. 
** You’ll just need to subscribe in the same manner you currently are, but to the generic # form of the URI.
***/
/**
 * @brief Build a service response topic
 * 
 * Constructs a service response topic of the format
 *       "thincloud/devices/{deviceId}/requests/#"
 * 
 * @param[out] buffer     Pointer to a string buffer to write to.
 * @param[in]  deviceId   Devices's ID.
 * @param[in]  requestId  Request's ID.
 * 
 * @return Zero on success, negative value otherwise 
 */
IoT_Error_t service_response_dev_topic(char *buffer, const char *deviceId)
{
    if (buffer == NULL)
    {
        return 0;
    }

    if (deviceId == NULL )
    {
        FUNC_EXIT_RC(NULL_VALUE_ERROR);
    }

    sprintf(buffer, "thincloud/devices/%s/requests/#", deviceId);

    FUNC_EXIT_RC(SUCCESS);
}
/**
 * @brief Subscribe to service responses
 *			All responses will publish form this topic.
 * 
 * @param[in]  client          AWS IoT MQTT Client instance.
 * @param[in]  deviceId        Device's ID.
 * @param[in]  requestId       Unique ID of a request.
 * @param[in]  handler         Subscription response handler.
 * @param[in]  subscriberData  Data blob to be passed to the subscription handler on invoke.
 * 
 * @return Zero on success, negative value otherwise 
 */
IoT_Error_t subscribe_to_service_dev_response(AWS_IoT_Client *client, char *p_topic, const char *deviceId,  pApplicationHandler_t handler, void *subscribeData)
{
    IoT_Error_t rc = service_response_dev_topic(p_topic, deviceId);

    if (rc != SUCCESS)
    {
        FUNC_EXIT_RC(rc);
    }

	MDF_LOGD("mqtt sub to %s \n", p_topic);
    return aws_iot_mqtt_subscribe(client, p_topic, strlen(p_topic), QOS0, handler, subscribeData);
}
IoT_Error_t unsubscribe_to_service_dev_response(AWS_IoT_Client *client, char *p_topic, const char *deviceId)
{
	IoT_Error_t rc = service_response_dev_topic(p_topic, deviceId);
	
		if (rc != SUCCESS)
		{
			FUNC_EXIT_RC(rc);
		}
  return aws_iot_mqtt_unsubscribe(client, p_topic, strlen(p_topic));
}

/**
 * @brief Subscribe to service responses
 * 
 * @param[in]  client          AWS IoT MQTT Client instance.
 * @param[in]  deviceId        Device's ID.
 * @param[in]  requestId       Unique ID of a request.
 * @param[in]  handler         Subscription response handler.
 * @param[in]  subscriberData  Data blob to be passed to the subscription handler on invoke.
 * 
 * @return Zero on success, negative value otherwise 
 */
IoT_Error_t subscribe_to_service_response(AWS_IoT_Client *client, char *p_topic, const char *deviceId, const char *requestId, pApplicationHandler_t handler, void *subscribeData)
{
    IoT_Error_t rc = service_response_topic(p_topic, deviceId, requestId);

    if (rc != SUCCESS)
    {
        FUNC_EXIT_RC(rc);
    }

    return aws_iot_mqtt_subscribe(client, p_topic, strlen(p_topic), QOS0, handler, subscribeData);
}

IoT_Error_t unsubscribe_to_service_response(AWS_IoT_Client *client, char *p_topic, const char *deviceId, const char *requestId)
{
	IoT_Error_t rc = service_response_topic(p_topic, deviceId, requestId);
	
		if (rc != SUCCESS)
		{
			FUNC_EXIT_RC(rc);
		}
  return aws_iot_mqtt_unsubscribe(client, p_topic, strlen(p_topic));
}

/**
 * @brief Initialize an AWS IoT Client 
 * 
 * Initialize an AWS IoT Client instance with ThinCloud specific parameters and defaults.
 * 
 * @param[in]  client          AWS IoT MQTT Client instance.
 * @param[in]  hostAddr        ThinCloud host address. 
 * @param[in]  rootCAPath      Path to a root CA.
 * @param[in]  clientCRTPath   Path to a client CRT.
 * @param[in]  clientKeyPath   Path to a client private key.
 * @param[in]  handler         Disconnect handler.
 * @param[in]  disconnectData  Data blob to be passed to the disconnect handler on invoke.
 * 
 * @return Zero on success, negative value otherwise 
 */
IoT_Error_t tc_init(AWS_IoT_Client *client, const char *hostAddr, const char *rootCAPath, const char *clientCRTPath, const char *clientKeyPath, iot_disconnect_handler handler, void *disconnectData)
{
    IoT_Client_Init_Params params = iotClientInitParamsDefault;

    params.enableAutoReconnect = false; // We enable this on connect
    params.pHostURL = (char *)hostAddr;
    params.port = 8883; // Default MQTT port443
    params.pRootCALocation = rootCAPath;
    params.pDeviceCertLocation = clientCRTPath;
    params.pDevicePrivateKeyLocation = clientKeyPath;
    params.mqttCommandTimeout_ms = 40000;
    params.tlsHandshakeTimeout_ms = 20000;
    params.isSSLHostnameVerify = true;
    params.disconnectHandler = handler;
    params.disconnectHandlerData = disconnectData;

    return aws_iot_mqtt_init(client, &params);
	
}
IoT_Error_t tc_attempt_reconnect(AWS_IoT_Client *pClient){
	return  aws_iot_mqtt_attempt_reconnect(pClient);
} 
void _tc_disconnectCallbackHandler(AWS_IoT_Client *pClient, void *data) {
    MDF_LOGE("MQTT Disconnect");
    IoT_Error_t rc = FAILURE;

	local_time_printf();

    if(NULL == pClient) {
        return;
    }else{
		MDF_LOGE("disconnect count %u \n", aws_iot_mqtt_get_network_disconnected_count(pClient));
	}

	#if 1
    if(aws_iot_is_autoreconnect_enabled(pClient)) {
        ESP_LOGI(TAG, "Auto Reconnect is enabled, Reconnecting attempt will start now");
    } else {
        ESP_LOGW(TAG, "Auto Reconnect not enabled. Starting manual reconnect...");
        rc = aws_iot_mqtt_attempt_reconnect(pClient);
        if(NETWORK_RECONNECTED == rc) {
            ESP_LOGW(TAG, "Manual Reconnect Successful");
        } else {
            ESP_LOGW(TAG, "Manual Reconnect Failed - %d", rc);
        }
    }

	#endif
}


/**
 * @brief Start MQTT connection to ThinCloud host
 * 
 * Initialize an MQTT conenction to a thincloud host.
 * 
 * @param[in]  client         AWS IoT MQTT Client instance.
 * @param[in]  clientId       An unique ID for the client instance.
 * @param[in]  autoReconnect  Signals if the MQTT client should attempt to auto-reconnect
 *                            after connection failures.
 * 
 * @return Zero on success, negative value otherwise 
 */
IoT_Error_t tc_connect(AWS_IoT_Client *client, char *clientId, bool autoReconnect)
{
	
    IoT_Client_Connect_Params params = iotClientConnectParamsDefault;

    params.keepAliveIntervalInSec = 2 * 60;
    params.isCleanSession = true;
    params.MQTTVersion = MQTT_3_1_1;
    params.pClientID = clientId;
    params.clientIDLen = (uint16_t)strlen(clientId);
    params.isWillMsgPresent = false;

    IoT_Error_t rc = aws_iot_mqtt_connect(client, &params);
    if (rc != SUCCESS)
    {
    	MDF_LOGE("aws_iot_mqtt_connect error %d \n", rc);
        FUNC_EXIT_RC( rc );
    }else{
		MDF_LOGE("Successfully build mqtt connections \n");
	}
    if (autoReconnect)
    {
        rc = aws_iot_mqtt_autoreconnect_set_status(client, true);
        if (rc != SUCCESS)
        {
        
			MDF_LOGE("aws_iot_mqtt_autoreconnect_set_status error %d \n", rc);
        	FUNC_EXIT_RC(rc);
        }
		
		aws_iot_mqtt_set_disconnect_handler(client, _tc_disconnectCallbackHandler, NULL);
	}
	
    FUNC_EXIT_RC( SUCCESS );
}

extern const uint8_t aws_root_ca_pem_start[] asm("_binary_aws_root_ca_pem_start");
extern const uint8_t aws_root_ca_pem_end[] asm("_binary_aws_root_ca_pem_end");

#if 0
extern const uint8_t aws_root_ca_pem_start[] asm("_binary_aws_root_ca_pem_start");
extern const uint8_t aws_root_ca_pem_end[] asm("_binary_aws_root_ca_pem_end");
extern const uint8_t certificate_pem_crt_start[] asm("_binary_certificate_pem_crt_start");
extern const uint8_t certificate_pem_crt_end[] asm("_binary_certificate_pem_crt_end");
extern const uint8_t private_pem_key_start[] asm("_binary_private_pem_key_start");
extern const uint8_t private_pem_key_end[] asm("_binary_private_pem_key_end"); 

#endif
AWS_IoT_Client t_client;
char *p_ca_crt_alloc = NULL, *p_ca_pri_key_alloc = NULL, *p_ca_root_alloc = NULL;

int ca_save(const char *p_root, const char *p_crt, const char *p_key){
	int ret  = 0, len  = 0;

	if(p_root){
		len = strlen(p_root);
		ret = utlis_store_save(US_SPA_SYS, "ca_root_len", &len, sizeof(int) );
	    MDF_ERROR_CHECK(ret != ESP_OK, ret, "Failt to save %s !\n", "ca_root_len");

		MDF_LOGW("Write %d [%s] \n", len, p_crt);
		ret = utlis_store_save(US_SPA_SYS, "ca_root", p_root, len);
	    MDF_ERROR_CHECK(ret != ESP_OK, ret, "Failt to save %s !\n", "ca_root");
		MDF_LOGE("save ca_root \n");
	}
	if(p_crt ){
		len = strlen(p_crt);
		ret = utlis_store_save(US_SPA_SYS, "ca_crt_len", &len, sizeof(int) );
		
		MDF_LOGW("Write %d [%s] \n", len, p_crt);
	    MDF_ERROR_CHECK(ret != ESP_OK, ret, "Failt to save %s !\n", "ca_crt_len");
		ret = utlis_store_save(US_SPA_SYS, "ca_crt", p_crt, len );
	    MDF_ERROR_CHECK(ret != ESP_OK, ret, "Failt to save %s !\n", "ca_crt");
		MDF_LOGE("save ca_crt \n");
		
	}
	if( p_key ){
		len = strlen(p_key);
		ret = utlis_store_save(US_SPA_SYS, "ca_key_len", &len, sizeof(int) );
		
		MDF_ERROR_CHECK(ret != ESP_OK, ret, "Failt to save %s !\n", "ca_key_len");
		
		MDF_LOGW("Write %d [%s] \n", len, p_key);
		ret = utlis_store_save(US_SPA_SYS, "ca_key", p_key, len );
		MDF_ERROR_CHECK(ret != ESP_OK, ret, "Failt to save %s !\n", "ca_key");
		
		MDF_LOGE("save ca_key \n");

	}
	return ret;
}
static int _tc_cert_get(const char *p_ca_string, const char *p_ca_len_string, char **pp_ca){
	int ret = -1, len = 0;
	char *p = NULL;
	
	ret = utlis_store_load(US_SPA_SYS, p_ca_len_string, &len, sizeof(int));
	if( ret != ESP_OK || len <= 0 ){
		MDF_LOGE("Failt to get %s len /n", p_ca_len_string);
		return ret;
	}
	
	MDF_LOGW("Read len = %d \n", len);
	p = utlis_malloc( len + 1);
	MDF_ERROR_CHECK(NULL == p, -1, "Failt to malloc\n");
	ret = utlis_store_load(US_SPA_SYS, p_ca_string, p, len);
	if( ret != ESP_OK ){
		MDF_LOGE("Failt to read %s \n", p_ca_string);
		MDF_FREE(p);
	}
	
	*pp_ca = p;

	return ret;
}
/**
* connnect to mqtt server.
****/
IoT_Error_t  client_connect(AWS_IoT_Client **pp_client, char *p_mac)
{
	char cli_id[64] = {0};
	size_t len = 0 ;
    //IoT_Error_t rc = tc_init(client,  Factory_host,(const char*)aws_root_ca_pem_start, (const char*)certificate_pem_crt_start, (const char*)private_pem_key_start, _tc_disconnectCallbackHandler, NULL);
	AWS_IoT_Client *p_client = &t_client;// malloc(sizeof(AWS_IoT_Client));

	if(!p_client)
		return -1;

	MDF_LOGD("Start to build mqtt connection to TC \n");
	memset(&t_client, 0, sizeof(AWS_IoT_Client) );
		
	if( p_ca_pri_key_alloc == NULL || p_ca_crt_alloc == NULL){
			return -1;
	}
	
	IoT_Error_t rc = tc_init(&t_client, Factory_host, (const char*)aws_root_ca_pem_start, (const char*)p_ca_crt_alloc, (const char*)p_ca_pri_key_alloc, NULL, NULL);
	if ( rc != SUCCESS ){
		aws_iot_mqtt_free(&t_client);
        MDF_LOGE("tc_init errro!\n");
		return rc;
    }else{
		MDF_LOGI("Successfully connection to TC \n");
    }
	sprintf(cli_id, _CLIENT_ID_FORMAT, PR_MAC2STR(p_mac) );
	MDF_LOGE("Client id is %s\n", cli_id);
    rc = tc_connect( &t_client, cli_id, true);

    if (rc != SUCCESS){
		aws_iot_mqtt_free(&t_client);
        MDF_LOGE("tc_connect error rc:%d!\n",rc);
		return rc;
    }else{
		*pp_client = p_client;
		MDF_LOGI("Connect to Thincloud successfully!!\n");
    	}
   return rc;
}
/***
* get  cert.
**/
void tc_certs_init(void){
	size_t len = 0;

    esp_log_level_set(TAG, ESP_LOG_INFO);

	//MDF_FREE( p_ca_root_alloc );
	//p_ca_root_alloc = NULL;
	MDF_FREE(p_ca_crt_alloc);
	p_ca_crt_alloc = NULL;
	MDF_FREE(p_ca_pri_key_alloc);
	p_ca_pri_key_alloc = NULL;
	//utlis_store_blob_get(US_SPA_SYS, "ca_root", &p_ca_root_alloc, &len);
	utlis_store_blob_get(US_SPA_SYS, "ca_crt", &p_ca_crt_alloc, &len);

	utlis_store_blob_get(US_SPA_SYS, "ca_key", &p_ca_pri_key_alloc, &len);

}
void tc_certs_deinit(void){
	//MDF_FREE( p_ca_root_alloc );
	//p_ca_root_alloc = NULL;
	MDF_FREE(p_ca_crt_alloc);
	p_ca_crt_alloc = NULL;
	MDF_FREE(p_ca_pri_key_alloc);
	p_ca_pri_key_alloc = NULL;

}


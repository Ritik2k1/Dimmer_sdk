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

#ifndef THINCLOUD_EMBEDDED_C_SDK_
#define THINCLOUD_EMBEDDED_C_SDK_

/* 1.0.1 */
#define TC_EMBEDDED_C_SDK_VERSION_MAJOR 1
#define TC_EMBEDDED_C_SDK_VERSION_MINOR 0
#define TC_EMBEDDED_C_SDK_VERSION_PATCH 3

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

// #include "json-c/json.h"
#include "mdf_mem.h"

#include "aws_iot_log.h"
#include "aws_iot_error.h"
#include "aws_iot_mqtt_client_interface.h"
#define  MYFREE(ptr)  MDF_FREE(ptr)
#define  AWSCHEK(a,b,c) do{ if(a) {printf("%s ret:%d\n",c,b); return b; } }while(0)
/**
 * UUID standard length plus null character
 */
#define TC_ID_LENGTH 37
#define CNF_YONOMI_DEV_TYPE	"invisihome-invr1"

/**
 * AWS IoT Max Topic Length plus null character
 */
#define MAX_TOPIC_LENGTH  257 //CNF_MAX_TOPIC_LENGTH
#define MAX_BODY_LENGTH  (1024 + 256)//CNF_MAX_BODY_LENGTH
#define REQUEST_METHOD_GET "GET"
#define REQUEST_METHOD_PUT "PUT"
#define REQUEST_METHOD_POST "POST"
#define REQUEST_METHOD_DELETE "DELETE"
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
IoT_Error_t commission_request_topic(char *buffer, const char *deviceType, const char *physicalId);
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
IoT_Error_t commission_response_topic(char *buffer, const char *deviceType, const char *physicalId, const char *requestId);

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
IoT_Error_t command_request_topic(char *buffer, const char *deviceId);

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
IoT_Error_t command_response_topic(char *buffer, const char *deviceId, const char *commandId);

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
IoT_Error_t service_request_topic(char *buffer, const char *deviceId);

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
IoT_Error_t service_response_topic(char *buffer, const char *deviceId, const char *requestId);
/**
 * @brief Build a commissioning request
 * 
 * Construct a commissioning request.
 * 
 * @param[out] buffer      Pointer to a string buffer to write to 
 * @param[in]  requestId   Unique ID for the request
 * @param[in]  deviceType  Requesting device's type
 * @param[in]  physicalId  Device's physical ID
 * 
 * @return Zero on success, negative value otherwise 
 */
IoT_Error_t commissioning_request(char **pp_buffer, const char *requestId, const char *deviceType, const char *physicalId);
IoT_Error_t unsubscribe_to_command_request(AWS_IoT_Client *client, char* topic, const char *deviceId);

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
IoT_Error_t commissioning_response(char **pp_deviceId, uint16_t *statusCode, char **pp_requestId, char *payload, uint16_t payloadLen);

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
IoT_Error_t command_response(char *buffer, const char *requestId, uint16_t statusCode, bool isErrorResponse, char *errorMessage, char *body);
char *_command_response(const char *requestId, uint16_t statusCode, bool isErrorResponse, char *errorMessage, char *body);
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
IoT_Error_t command_request(char **pp_requestId, char **pp_method, char **params, const char *payload, const unsigned int payloadLen);

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
IoT_Error_t service_request(char **pp_buffer, const char *requestId, const char *method, char *params);
char *_service_request(const char *requestId, const char *method, char *params);

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
IoT_Error_t service_response(char *requestId, uint16_t *statusCode, char **pp_data, const char *payload, const unsigned int payloadLen);

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
IoT_Error_t send_command_response(AWS_IoT_Client *client, const char *deviceId, const char *commandId, uint16_t statusCode, bool isErrorResponse, char *errorMessage, char *body);
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
IoT_Error_t send_commissioning_request(AWS_IoT_Client *client, const char *requestId, const char *deviceType, const char *p_mac);

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
IoT_Error_t send_service_request(AWS_IoT_Client *client, char *requestId, char *deviceId, char *method, /*json_object *reqParams*/char *reqParams);


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
IoT_Error_t subscribe_to_commissioning_response(AWS_IoT_Client *client,char *p_topic, const char *requestId, const char *deviceType, const char *physicalId, pApplicationHandler_t handler, void *subscribeData);

IoT_Error_t unsubscribe_to_commissioning_response(AWS_IoT_Client *client, char *p_topic, const char *requestId, const char *deviceType, const char *physicalId);


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
IoT_Error_t subscribe_to_command_request(AWS_IoT_Client *client, char *topic, const char *deviceId, pApplicationHandler_t handler, void *subscribeData);

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
IoT_Error_t subscribe_to_service_response(AWS_IoT_Client *client, char *p_topic, const char *deviceId, const char *requestId, pApplicationHandler_t handler, void *subscribeData);
IoT_Error_t unsubscribe_to_service_response(AWS_IoT_Client *client, char *p_topic, const char *deviceId, const char *requestId);

/**
** As a general rule, you can subscribe a device to its generic requests topic like that: 
** thincloud/devices/{deviceId}/requests/#
** All messages under that topic will come through that subscription, 
** including those responses. This is the recommended approach for a device to subscribe to requests. 
** You’ll just need to subscribe in the same manner you currently are, but to the generic # form of the URI.
***/
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
IoT_Error_t subscribe_to_service_dev_response(AWS_IoT_Client *client, char *p_topic, const char *deviceId,  pApplicationHandler_t handler, void *subscribeData);
IoT_Error_t unsubscribe_to_service_dev_response(AWS_IoT_Client *client, char *p_topic, const char *deviceId);
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
IoT_Error_t tc_init(AWS_IoT_Client *client, const char *hostAddr, const char *rootCAPath, const char *clientCRTPath, const char *clientKeyPath, iot_disconnect_handler handler, void *disconnectData);

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
IoT_Error_t tc_connect(AWS_IoT_Client *client, char *clientId, bool autoReconnect);
#if 0
extern const uint8_t aws_root_ca_pem_start[] asm("_binary_aws_root_ca_pem_start");
extern const uint8_t aws_root_ca_pem_end[] asm("_binary_aws_root_ca_pem_end");
extern const uint8_t certificate_pem_crt_start[] asm("_binary_certificate_pem_crt_start");
extern const uint8_t certificate_pem_crt_end[] asm("_binary_certificate_pem_crt_end");
extern const uint8_t private_pem_key_start[] asm("_binary_private_pem_key_start");
extern const uint8_t private_pem_key_end[] asm("_binary_private_pem_key_end");
#endif
IoT_Error_t  client_connect(AWS_IoT_Client **pp_client, char *p_mac);
IoT_Error_t tc_attempt_reconnect(AWS_IoT_Client *pClient);

/**
 * @brief Send service request
 * 
 * 直接 Publish a service request to MQTT.
 * 
 * @param[in]  client     AWS IoT MQTT Client instance.
 * @param[in]  p_json incloude topic json data.
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
IoT_Error_t send_publish(AWS_IoT_Client *client, char *p_devid, char *p_json);
IoT_Error_t topic_commission_alloc(char **pp_topic, const char *p_mac);
IoT_Error_t topic_commission_respond_alloc(char **pp_topic, const char *p_mac, const char *p_rq_id);
IoT_Error_t topic_command_alloc(char **pp_topic, const char *p_devid);
IoT_Error_t topic_respond_alloc(char **pp_topic, const char *p_devid);
IoT_Error_t topic_command_respond_alloc(char **pp_topic, const char *p_devid, const char *p_cid);
IoT_Error_t command_response_json_alloc(char **pp_json, const char *p_cid, uint16_t status_code, char *p_msg);

int ca_save(const char *p_root, const char *p_crt, const char *p_key);
void tc_certs_init(void);
void tc_certs_deinit(void);

#endif /* THINCLOUD_EMBEDDED_C_SDK_ */

/*
 * @Author: sky
 * @Date: 2020-03-09 18:34:28
 * @LastEditTime: 2021-02-04 17:01:25
 * @LastEditors: Please set LastEditors
 * @Description: ota
 * @FilePath: \mqtt_example\components\light_device\light_handle.c
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

#include "mwifi.h"
#include "mupgrade.h"
#include "mlink.h"

#include "utlis.h"
#include "event_queue.h"
#include "light_device.h"
static const char *TAG          = "Ota"; 
typedef enum{
	OTA_NONE,
	OTA_START,
	OTA_ING,
	OTA_DONE,
	OTA_ERR,
	
	OTA_MAX
}OTA_status_t;
const char *p_ota_msg[OTA_MAX] = {
	NULL,
	"Device starting updatie...\n",
	"Device updating...\n",
	"Device update successfully\n",
	"Device update Failt\n",
	NULL
};
typedef struct {

	OTA_status_t status;
}Ota_t;

static Ota_t ota = {0};

#if 0
static void _oat_time_check(int64_t ctime){
	if( DIFF(ctime, ota.last_time) >  ( 20* 1000) ){
		_ota_status_set(OTA_NONE, "No ota.");
	}
}
#endif
static mdf_err_t _ota_status_set(OTA_status_t status){

	MDF_PARAM_CHECK(status < OTA_MAX);
	
	ota.status = status;
	return MDF_OK;
}
static mdf_err_t _ota_status_code_get(uint8_t **pp_msg, uint16_t *p_code )
{
	if( p_ota_msg[ ota.status ] ){
		*pp_msg = malloc_copy((uint8_t *)p_ota_msg[ ota.status ], strlen(p_ota_msg[ ota.status ]));
	}
	*p_code = (ota.status != OTA_ERR)?200:300;

	return MDF_OK;
}
static void _handle_ota(const char *p_ota_url)
{
    mdf_err_t ret       = MDF_OK;
    uint8_t *data       = MDF_MALLOC(MWIFI_PAYLOAD_LEN);
    char name[32]       = {0x0};
    size_t total_size   = 0;
    int start_time      = 0;
    mupgrade_result_t upgrade_result = {0};
    mwifi_data_type_t data_type = {.communicate = MWIFI_COMMUNICATE_MULTICAST};

	Evt_mesh_t evt = {0};

    /**
     * @note If you need to upgrade all devices, pass MWIFI_ADDR_ANY;
     *       If you upgrade the incoming address list to the specified device
     */
    // uint8_t dest_addr[][MWIFI_ADDR_LEN] = {{0x1, 0x1, 0x1, 0x1, 0x1, 0x1}, {0x2, 0x2, 0x2, 0x2, 0x2, 0x2},};
    uint8_t dest_addr[][MWIFI_ADDR_LEN] = {MWIFI_ADDR_ANY};

    /**
     * @brief In order to allow more nodes to join the mesh network for firmware upgrade,
     *      in the example we will start the firmware upgrade after 30 seconds.
     */
    //vTaskDelay(10 * 1000 / portTICK_PERIOD_MS);

    esp_http_client_config_t config = {
        //.url            = CONFIG_FIRMWARE_UPGRADE_URL,
		.url	= p_ota_url,
		.transport_type = HTTP_TRANSPORT_UNKNOWN,
    };

	
	_ota_status_set(OTA_ING);
    /**
     * @brief 1. Connect to the server
     */
    esp_http_client_handle_t client = esp_http_client_init(&config);
    MDF_ERROR_GOTO(!client, EXIT, "Initialise HTTP connection");

    start_time = xTaskGetTickCount();

    MDF_LOGI("Open HTTP connection: %s", p_ota_url);

    /**
     * @brief First, the firmware is obtained from the http server and stored on the root node.
     */
    do {
        ret = esp_http_client_open(client, 0);

        if (ret != MDF_OK) {
            if (!esp_mesh_is_root()) {
                goto EXIT;
            }

            vTaskDelay(pdMS_TO_TICKS(1000));
            MDF_LOGW("<%s> Connection service failed", mdf_err_to_name(ret));
        }
    } while (ret != MDF_OK);

    total_size = esp_http_client_fetch_headers(client);
    sscanf(p_ota_url, "%*[^//]//%*[^/]/%[^.bin]", name);

    if (total_size <= 0) {
        MDF_LOGW("Please check the address of the server");
        ret = esp_http_client_read(client, (char *)data, MWIFI_PAYLOAD_LEN);
        MDF_ERROR_GOTO(ret < 0, EXIT, "<%s> Read data from http stream", mdf_err_to_name(ret));

        MDF_LOGW("Recv data: %.*s", ret, data);
        goto EXIT;
    }

    /**
     * @brief 2. Initialize the upgrade status and erase the upgrade partition.
     */
    ret = mupgrade_firmware_init(name, total_size);
    MDF_ERROR_GOTO(ret != MDF_OK, EXIT, "<%s> Initialize the upgrade status", mdf_err_to_name(ret));

    /**
     * @brief 3. Read firmware from the server and write it to the flash of the root node
     */
    for (ssize_t size = 0, recv_size = 0; recv_size < total_size; recv_size += size) {
        size = esp_http_client_read(client, (char *)data, MWIFI_PAYLOAD_LEN);
        MDF_ERROR_GOTO(size < 0, EXIT, "<%s> Read data from http stream", mdf_err_to_name(ret));

        if (size > 0) {
            /* @brief  Write firmware to flash */
            ret = mupgrade_firmware_download(data, size);
            MDF_ERROR_GOTO(ret != MDF_OK, EXIT, "<%s> Write firmware to flash, size: %d, data: %.*s",
                           mdf_err_to_name(ret), size, size, data);
        } else {
            MDF_LOGW("<%s> esp_http_client_read", mdf_err_to_name(ret));
            goto EXIT;
        }
    }

    MDF_LOGI("The service download firmware is complete, Spend time: %ds",
             (xTaskGetTickCount() - start_time) * portTICK_RATE_MS / 1000);

    start_time = xTaskGetTickCount();

    /**
     * @brief 4. The firmware will be sent to each node.
     */
    ret = mupgrade_firmware_send((uint8_t *)dest_addr, sizeof(dest_addr) / MWIFI_ADDR_LEN, &upgrade_result);
    MDF_ERROR_GOTO(ret != MDF_OK, EXIT, "<%s> mupgrade_firmware_send", mdf_err_to_name(ret));

    if (upgrade_result.successed_num == 0) {
        MDF_LOGW("Devices upgrade failed, unfinished_num: %d", upgrade_result.unfinished_num);
        goto EXIT;
    }

    MDF_LOGI("Firmware is sent to the device to complete, Spend time: %ds",
             (xTaskGetTickCount() - start_time) * portTICK_RATE_MS / 1000);
    MDF_LOGI("Devices upgrade completed, successed_num: %d, unfinished_num: %d", upgrade_result.successed_num, upgrade_result.unfinished_num);

    /**
     * @brief 5. the root notifies nodes to restart
     */
    const char *restart_str = "restart";
    ret = mwifi_root_write( upgrade_result.successed_addr, upgrade_result.successed_num,
                           &data_type, restart_str, strlen(restart_str), true);
	/**
	*@ will restart itself
	*/
	MDF_LOGE("Finish update");
	evt.cmd = EVT_SYS_REST;
	mevt_send(&evt, 100/portTICK_RATE_MS);
	MDF_LOGE("Finish update, try to reset \n");	
	_ota_status_set(OTA_DONE);
    
    MDF_ERROR_GOTO(ret != MDF_OK, EXIT, "<%s> mwifi_root_recv", mdf_err_to_name(ret));

	
	MDF_FREE(data);
	mupgrade_result_free(&upgrade_result);
	esp_http_client_close(client);
	esp_http_client_cleanup(client);
	return;
	
EXIT:
	
	_ota_status_set(OTA_ERR);
    MDF_FREE(data);
    mupgrade_result_free(&upgrade_result);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

}

static mdf_err_t _mlink_ota_begin(mlink_handle_data_t *handle_data){
	char *p_data = NULL, *p_url = NULL;
	mdf_err_t rc = MDF_OK;
	Evt_mesh_t evt = {0};
	
	MDF_PARAM_CHECK(handle_data);
	MDF_PARAM_CHECK(handle_data->req_data);
	MDF_PARAM_CHECK( esp_mesh_is_root() );
	
	MDF_LOGD("receive data %s \n", handle_data->req_data);

	
	_ota_status_set(OTA_ERR);

	rc = mlink_json_parse(handle_data->req_data, "cid", &evt.p_cid);
	rc = mlink_json_parse(handle_data->req_data, "deviceId", &evt.p_devid);
		
	// get url 
	rc = mlink_json_parse(handle_data->req_data, "data", &p_data);
	MDF_ERROR_GOTO( NULL == p_data, End, "Failt json get data rc = %d \n", rc);
	rc = mlink_json_parse(p_data, "Firmware-Url", &p_url);
	MDF_ERROR_GOTO( NULL == p_url, End, "Failt json get url rc = %d \n", rc);
	
	evt.cmd = MEVT_TC_COMMAND_RESPOND;
	evt.status_code = 200;
	rc = mevt_send(&evt,  10);
	if(MDF_OK != rc){
		
		mevt_clean(&evt);
		MDF_FREE(p_url);
		goto End;
	}

	if(p_url){
		evt.p_cid = NULL;
		evt.data_len = strlen(p_url);
		evt.p_data = (uint8_t *)p_url;
		evt.cmd = EVT_SYS_OTA_START;
		rc = mevt_send(&evt,  10);
		if( MDF_OK != rc){
			MDF_FREE(p_url);
			goto End;
		}
		
		_ota_status_set(OTA_START);
	}	
	// send respond event 
	
	
End:
	MDF_FREE(p_data);
	return rc;
}

static mdf_err_t _mevent_handle_ota_start(Evt_mesh_t *p_evt){
	mdf_err_t rc = MDF_OK;

	MDF_PARAM_CHECK(p_evt);
	MDF_PARAM_CHECK(p_evt->p_data);
	light_status_set(LSYS_STATUS_OTAING);
	_handle_ota((char *) p_evt->p_data);
	light_status_set(LSYS_STATUS_ONLINE);
	light_led_indicator();

	return rc;
}
extern const char *mlink_device_get_version();

static mdf_err_t _mlink_ota_check(mlink_handle_data_t *handle_data)
{
	
	mdf_err_t rc = MDF_OK;
	Evt_mesh_t evt;
	char *p_json = NULL, *p_msg = NULL;
	rc = mlink_json_parse(handle_data->req_data, "cid", &evt.p_cid);
	rc = mlink_json_parse(handle_data->req_data, "deviceId", &evt.p_devid);

	// data 
	mlink_json_pack( &p_json, "_version", mlink_device_get_version());
	_ota_status_code_get((uint8_t **) &p_msg, &evt.status_code);
	
	if(p_msg){
		mlink_json_pack( &p_json, "msg", p_msg);
		MDF_FREE(p_msg);
		p_msg = NULL;
	}
	evt.p_data = (uint8_t *)p_json;
	evt.cmd = MEVT_TC_COMMAND_RESPOND;
	rc = mevt_send(&evt,  10);
	if( MDF_OK != rc){
		mevt_clean(&evt);
	}

	return rc;
}

void oat_init(void)
{
    esp_log_level_set(TAG, ESP_LOG_INFO);

	mevt_handle_func_register( _mevent_handle_ota_start,  EVT_SYS_OTA_START);
    MDF_ERROR_ASSERT(mlink_set_handle("ota_begin", _mlink_ota_begin) );
	MDF_ERROR_ASSERT(mlink_set_handle("ota_check", _mlink_ota_check) );
	
}

// Copyright 2017 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "mdf_common.h"
#include "mwifi.h"
#include "mlink.h"
#include "mupgrade.h"
#include "mespnow.h"

#include "mconfig_blufi.h"
#include "mconfig_chain.h"

#include "mesh_thincloud.h"
#include "mesh_event.h"
#include "light_handle.h"
#include "event_queue.h"
#include "utlis.h"
#include "button.h"
#include "sunriset.h"

//#include "esp_heap_trace.h"
//#define MEMORY_DEBUG
//#define NUM_RECORDS 100
//static heap_trace_record_t trace_record[NUM_RECORDS];

static TaskHandle_t g_root_write_task_handle = NULL;
static TaskHandle_t g_root_read_task_handle  = NULL;

static const char *TAG = "mqtt_examples";

/**
 * @brief Read data from mesh network, forward data to extern IP network by http or udp.
 */
static void root_write_task(void *arg)
{
    mdf_err_t ret = MDF_OK;
    char *data    = NULL;
    size_t size   = 0;
    uint8_t src_addr[MWIFI_ADDR_LEN] = {0x0};
    mwifi_data_type_t mwifi_type      = {0};

    MDF_LOGI("root_write_task is running");

    while (mwifi_is_connected() && esp_mesh_get_layer() == MESH_ROOT) {
        ret = mwifi_root_read(src_addr, &mwifi_type, &data, &size, portMAX_DELAY);
        MDF_ERROR_GOTO(ret != MDF_OK, FREE_MEM, "<%s> mwifi_root_read", mdf_err_to_name(ret));

        if (mwifi_type.upgrade) {
            ret = mupgrade_root_handle(src_addr, data, size);
            MDF_ERROR_GOTO(ret != MDF_OK, FREE_MEM, "<%s> mupgrade_handle", mdf_err_to_name(ret));
            goto FREE_MEM;
        }

        MDF_LOGD("Root receive, addr: " MACSTR ", size: %d, data: %.*s",
                 MAC2STR(src_addr), size, size, data);

        switch (mwifi_type.protocol) {
            case MLINK_PROTO_HTTPD: { // use http protocol
                mlink_httpd_t httpd_data  = {
                    .size       = size,
                    .data       = data,
                    .addrs_num  = 1,
                    .addrs_list = src_addr,
                };
                memcpy(&httpd_data.type, &mwifi_type.custom, sizeof(httpd_data.type));

                ret = mlink_httpd_write(&httpd_data, portMAX_DELAY);
                MDF_ERROR_BREAK(ret != MDF_OK, "<%s> mlink_httpd_write", mdf_err_to_name(ret));

                break;
            }

            case MLINK_PROTO_NOTICE: { // use udp protocol
                ret = mlink_notice_write(data, size, src_addr);
                MDF_ERROR_BREAK(ret != MDF_OK, "<%s> mlink_httpd_write", mdf_err_to_name(ret));
                break;
            }

            default:
                MDF_LOGW("Does not support the protocol: %d", mwifi_type.protocol);
                break;
        }

FREE_MEM:
        MDF_FREE(data);
    }

    MDF_LOGW("root_write_task is exit");

    MDF_FREE(data);
    g_root_write_task_handle = NULL;
    vTaskDelete(NULL);
}

/**
 * @brief Read data from extern IP network, forward data to destination device.
 */
static void root_read_task(void *arg)
{
    mdf_err_t ret               = MDF_OK;
    mlink_httpd_t *httpd_data   = NULL;
    mwifi_data_type_t mwifi_type = {
        .compression = true,
        .communicate = MWIFI_COMMUNICATE_MULTICAST,
    };

    MDF_LOGI("root_read_task is running");

    while (mwifi_is_connected() && esp_mesh_get_layer() == MESH_ROOT) {
        ret = mlink_httpd_read(&httpd_data, portMAX_DELAY);
        MDF_ERROR_GOTO(ret != MDF_OK || !httpd_data, FREE_MEM, "<%s> mwifi_root_read", mdf_err_to_name(ret));
        MDF_LOGD("Root send, addrs_num: %d, addrs_list: " MACSTR ", size: %d, data: %.*s",
                 httpd_data->addrs_num, MAC2STR(httpd_data->addrs_list),
                 httpd_data->size, httpd_data->size, httpd_data->data);

        mwifi_type.group = httpd_data->group;
        memcpy(&mwifi_type.custom, &httpd_data->type, sizeof(mlink_httpd_type_t));

        ret = mwifi_root_write(httpd_data->addrs_list, httpd_data->addrs_num,
                               &mwifi_type, httpd_data->data, httpd_data->size, true);
        MDF_ERROR_GOTO(ret != MDF_OK, FREE_MEM, "<%s> mwifi_root_write", mdf_err_to_name(ret));

FREE_MEM:

        if (httpd_data) {
            MDF_FREE(httpd_data->addrs_list);
            MDF_FREE(httpd_data->data);
            MDF_FREE(httpd_data);
        }
    }

    MDF_LOGW("root_read_task is exit");

    if (httpd_data) {
        MDF_FREE(httpd_data->addrs_list);
        MDF_FREE(httpd_data->data);
        MDF_FREE(httpd_data);
    }

    g_root_read_task_handle = NULL;
    vTaskDelete(NULL);
}

/**
 * @brief Handling data between wifi mesh devices.
 */
void node_handle_task(void *arg)
{
    mdf_err_t ret = MDF_OK;
    uint8_t *data = NULL;
    size_t size   = MWIFI_PAYLOAD_LEN;
    mwifi_data_type_t mwifi_type     = {0x0};
    uint8_t src_addr[MWIFI_ADDR_LEN] = {0x0};
    mlink_httpd_type_t *header_info  = NULL;

    while (true) {
        ret = mwifi_read(src_addr, &mwifi_type, &data, &size, portMAX_DELAY);
        MDF_ERROR_GOTO(ret != MDF_OK, FREE_MEM, "<%s> Receive a packet targeted to self over the mesh network",
                       mdf_err_to_name(ret));

        if (mwifi_type.upgrade) { // This mesh package contains upgrade data.
            ret = mupgrade_handle(src_addr, data, size);
            MDF_ERROR_GOTO(ret != MDF_OK, FREE_MEM, "<%s> mupgrade_handle", mdf_err_to_name(ret));
            goto FREE_MEM;
        }

        MDF_LOGI("Node receive, addr: " MACSTR ", size: %d, data: %.*s", MAC2STR(src_addr), size, size, data);

        /*< Header information for http data */
        header_info = (mlink_httpd_type_t *)&mwifi_type.custom;
        MDF_ERROR_GOTO(header_info->format != MLINK_HTTPD_FORMAT_JSON, FREE_MEM,
                       "The current version only supports the json protocol");

        /*****
         * @brief Processing request commands, generating response data
         *
         * @note  Handling only the body part of http, the header
         *        of http is handled by mlink_httpd
         *****/
         
        mlink_handle_data_t handle_data = {
            .req_data    = (char *)data,
            .req_size    = size,
            .req_fromat  = MLINK_HTTPD_FORMAT_JSON,
            .resp_data   = NULL,
            .resp_size   = 0,
            .resp_fromat = MLINK_HTTPD_FORMAT_JSON,
        };
			
        ret = mlink_handle_request(&handle_data);
		
        MDF_ERROR_GOTO(ret != MDF_OK, FREE_MEM, "<%s> mlink_handle", mdf_err_to_name(ret));

        if (handle_data.resp_fromat == MLINK_HTTPD_FORMAT_JSON) {
            mlink_json_pack(&handle_data.resp_data, "status_msg", mdf_err_to_name(ret));
            handle_data.resp_size = mlink_json_pack(&handle_data.resp_data, "status_code", -ret);
        }

        /**
         * @brief If this packet comes from a device on the mesh network,
         *  it will notify the App that the device's status has changed.
         */
        if (header_info->from == MLINK_HTTPD_FROM_DEVICE && mwifi_get_root_status()) {
            mwifi_type.protocol = MLINK_PROTO_NOTICE;
            ret = mwifi_write(NULL, &mwifi_type, "status", strlen("status"), true);
            MDF_ERROR_GOTO(ret != MDF_OK, FREE_MEM, "<%s> mlink_handle", mdf_err_to_name(ret));
        }

        /**
         * @brief Send the response data to the source device
         */
        if (header_info->resp) {
            uint8_t *dest_addr = (header_info->from == MLINK_HTTPD_FROM_SERVER) ? NULL : src_addr;
            /*< Populate the header information of http */
            header_info->format = handle_data.resp_fromat;
            header_info->from   = MLINK_HTTPD_FROM_DEVICE;

            mwifi_type.protocol = MLINK_PROTO_HTTPD;
            mwifi_type.compression = true;
            ret = mwifi_write(dest_addr, &mwifi_type, handle_data.resp_data, handle_data.resp_size, true);

            if (handle_data.resp_fromat == MLINK_HTTPD_FORMAT_HEX) {
                MDF_LOGI("Node send, size: %d, data: ", handle_data.resp_size);
                ESP_LOG_BUFFER_HEX(TAG, handle_data.resp_data, handle_data.resp_size);
            } else {
                MDF_LOGI("Node send, size: %d, data: %.*s", handle_data.resp_size,
                         handle_data.resp_size, handle_data.resp_data);
            }
        }

        MDF_FREE(handle_data.resp_data);
        MDF_ERROR_GOTO(ret != ESP_OK, FREE_MEM, "<%s> mdf_write", mdf_err_to_name(ret));

FREE_MEM:
        MDF_FREE(data);
    }

    MDF_FREE(data);
    vTaskDelete(NULL);
}

/**
 * @brief Initialize espnow_to_mwifi_task for forward esp-now data to the wifi mesh network.
 */
static void espnow_to_mwifi_task(void *arg)
{
    mdf_err_t ret       = MDF_OK;
    uint8_t *data       = NULL;
    uint8_t *addrs_list = NULL;
    size_t addrs_num    = 0;
    size_t size         = 0;
    uint32_t type       = 0;

    mwifi_data_type_t mwifi_type = {
        .protocol = MLINK_PROTO_HTTPD,
    };
    mlink_httpd_type_t header_info = {
        .format = MLINK_HTTPD_FORMAT_JSON,
        .from   = MLINK_HTTPD_FROM_DEVICE,
        .resp   = false,
    };

    memcpy(&mwifi_type.custom, &header_info, sizeof(mlink_httpd_type_t));

    while (mlink_espnow_read(&addrs_list, &addrs_num, &data, &size, &type, portMAX_DELAY) == MDF_OK) {
        /*< Send to yourself if the destination address is empty */
        if ( MWIFI_ADDR_IS_EMPTY(addrs_list) && addrs_num == 1) {
            esp_wifi_get_mac(ESP_IF_WIFI_STA, addrs_list);
        }

        mwifi_type.group = (type == MLINK_ESPNOW_COMMUNICATE_GROUP) ? true : false;
        MDF_LOGI("Mlink espnow read data: %.*s", size, data);

        for (int i = 0; i < addrs_num; ++i) {
            ret = mwifi_write(addrs_list  + 6 * i, &mwifi_type, data, size, true);
            MDF_ERROR_CONTINUE(ret != MDF_OK, "<%s> mwifi_write", mdf_err_to_name(ret));
        }

        MDF_FREE(data);
        MDF_FREE(addrs_list);
    }

    MDF_LOGW("espnow_to_mwifi_task is exit");
    vTaskDelete(NULL);
}

/**
 * @brief Timed printing system information
 */
 void print_system_info_timercb(void *timer)
{
    uint8_t primary                 = 0;
    wifi_second_chan_t second       = 0;
    mesh_addr_t parent_bssid        = {0};
    uint8_t sta_mac[MWIFI_ADDR_LEN] = {0};
    mesh_assoc_t mesh_assoc         = {0x0};
    wifi_sta_list_t wifi_sta_list   = {0x0};

    esp_wifi_get_mac(ESP_IF_WIFI_STA, sta_mac);
    esp_wifi_ap_get_sta_list(&wifi_sta_list);
    esp_wifi_get_channel(&primary, &second);
    esp_wifi_vnd_mesh_get(&mesh_assoc);
    esp_mesh_get_parent_bssid(&parent_bssid);

    MDF_LOGI("System information, channel: %d, layer: %d, self mac: " MACSTR ", parent bssid: " MACSTR
             ", parent rssi: %d, node num: %d, free heap: %u", primary,
             esp_mesh_get_layer(), MAC2STR(sta_mac), MAC2STR(parent_bssid.addr),
             mesh_assoc.rssi, esp_mesh_get_total_node_num(), esp_get_free_heap_size());
	local_time_printf();
    for (int i = 0; i < wifi_sta_list.num; i++) {
        MDF_LOGI("Child mac: " MACSTR, MAC2STR(wifi_sta_list.sta[i].mac));
    }
	

#ifdef MEMORY_DEBUG

    if (!heap_caps_check_integrity_all(true)) {
        MDF_LOGE("At least one heap is corrupt");
    }

    mdf_mem_print_heap();
    mdf_mem_print_record();
    mdf_mem_print_task();
#endif /**< MEMORY_DEBUG */
}

static mdf_err_t wifi_init()
{
    mdf_err_t ret          = nvs_flash_init();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        MDF_ERROR_ASSERT(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    MDF_ERROR_ASSERT(ret);

    tcpip_adapter_init();
    MDF_ERROR_ASSERT(esp_event_loop_init(NULL, NULL));
    MDF_ERROR_ASSERT(esp_wifi_init(&cfg));
    MDF_ERROR_ASSERT(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    MDF_ERROR_ASSERT(esp_wifi_set_mode(WIFI_MODE_STA));
    MDF_ERROR_ASSERT(esp_wifi_set_ps(WIFI_PS_NONE));
    MDF_ERROR_ASSERT(esp_mesh_set_6m_rate(false));
    MDF_ERROR_ASSERT(esp_wifi_start());

    return MDF_OK;
}

/**
 * @brief All module events will be sent to this task in esp-mdf
 *
 * @Note:
 *     1. Do not block or lengthy operations in the callback function.
 *     2. Do not consume a lot of memory in the callback function.
 *        The task memory of the callback function is only 4KB.
 */
static mdf_err_t event_loop_cb(mdf_event_loop_t event, void *ctx)
{
    MDF_LOGI("event_loop_cb, event: %d", event);
    mdf_err_t ret = MDF_OK;
    switch (event) {
        case MDF_EVENT_MWIFI_STARTED:
			ret = tcpip_adapter_set_hostname(TCPIP_ADAPTER_IF_STA, "InvisiHome R1ULC");
            MDF_LOGE("tcpip_adapter_set_hostname result %x\n", ret);
            MDF_LOGI("MESH is started");
            break;

        case MDF_EVENT_MWIFI_PARENT_CONNECTED:
            MDF_LOGI("Parent is connected on station interface");
			make_time_update_rq_2root();
            break;

        case MDF_EVENT_MWIFI_PARENT_DISCONNECTED:
            MDF_LOGI("Parent is disconnected on station interface");

            /** When the root node switches, sometimes no disconnected packets are received */
            //ret = mlink_notice_deinit();
            //MDF_ERROR_BREAK(ret != MDF_OK, "<%s> mlink_notice_deinit", mdf_err_to_name(ret));

            ret = mlink_httpd_stop();
            MDF_ERROR_BREAK(ret != MDF_OK, "<%s> mlink_httpd_stop", mdf_err_to_name(ret));

            if (esp_mesh_is_root()) {
                ret = mwifi_post_root_status(false);
                MDF_ERROR_BREAK(ret != MDF_OK, "<%s> mwifi_post_root_status", mdf_err_to_name(ret));
            }
            if (esp_mesh_is_root()) {
                tc_client_destory();
            }
			
            break;
        case MDF_EVENT_MWIFI_FIND_NETWORK: {
            MDF_LOGI("the root connects to another router with the same SSID");
            mwifi_config_t ap_config  = {0x0};
            wifi_second_chan_t second = 0;

            mdf_info_load("ap_config", &ap_config, sizeof(mwifi_config_t));
            esp_wifi_get_channel(&ap_config.channel, &second);
            esp_mesh_get_parent_bssid((mesh_addr_t *)ap_config.router_bssid);
            mwifi_set_config(&ap_config);
            mdf_info_save("ap_config", &ap_config, sizeof(mwifi_config_t));
            break;
        }

        case MDF_EVENT_MWIFI_ROUTING_TABLE_ADD:
            MDF_LOGI("MDF_EVENT_MWIFI_ROUTING_TABLE_ADD, total_num: %d", esp_mesh_get_total_node_num());
            if (esp_mesh_is_root()) {
				//uint8_t sta_mac[MWIFI_ADDR_LEN] = {0x0};
                //MDF_ERROR_ASSERT(esp_wifi_get_mac(ESP_IF_WIFI_STA, sta_mac));
                //ret = mlink_notice_write("http", strlen("http"), sta_mac);
               // MDF_ERROR_BREAK(ret != MDF_OK, "<%s> mlink_httpd_write", mdf_err_to_name(ret));
                /**
                 * @brief find new add device.
                 */
                mesh_event_table_add();
				//light_online_device_update_set(3000);
            }

            break;

        case MDF_EVENT_MWIFI_ROUTING_TABLE_REMOVE:
            MDF_LOGI("MDF_EVENT_MWIFI_ROUTING_TABLE_REMOVE, total_num: %d", esp_mesh_get_total_node_num());

            if (esp_mesh_is_root()) {
				
                /**
                 * @brief find removed device.
                 */
               mesh_event_table_remove();
			   //light_online_device_update_set(3000);
            }

            break;

        case MDF_EVENT_MWIFI_ROOT_GOT_IP: 
			light_status_set(LSYS_STATUS_CNN);
			if(esp_mesh_is_root()){
	            MDF_LOGI("Root obtains the IP address. It is posted by LwIP stack automatically");
				  /**
	             * @brief Initialization mlink notice for inform the mobile phone that there is a mesh root device
	             */
			#if 0  //  todo 
				 	ret = mlink_notice_init();
				 	MDF_ERROR_BREAK(ret != MDF_OK, "<%s> mlink_notice_init", mdf_err_to_name(ret));

				 	uint8_t sta_mac[MWIFI_ADDR_LEN] = {0x0};
				 	MDF_ERROR_ASSERT(esp_wifi_get_mac(ESP_IF_WIFI_STA, sta_mac));

				 	ret = mlink_notice_write("http", strlen("http"), sta_mac);
				 	MDF_ERROR_BREAK(ret != MDF_OK, "<%s> mlink_httpd_write", mdf_err_to_name(ret));
			#endif
	            /**
	             * @brief start mlink http server for handle data between device and moblie phone.
	             */
	            	ret = mlink_httpd_start();
	            	MDF_ERROR_BREAK(ret != MDF_OK, "<%s> mlink_httpd_start", mdf_err_to_name(ret));
				#if 1
	            /**
	             * @brief start root read/write task for hand data between mesh network and extern ip network.
	             */
	            if (!g_root_write_task_handle) {
	                xTaskCreatePinnedToCore(root_write_task, "root_write", 4 * 1024,
	                            NULL, CONFIG_MDF_TASK_DEFAULT_PRIOTY, &g_root_write_task_handle, 0);
	            }

	            if (!g_root_read_task_handle) {
					
	                xTaskCreatePinnedToCore(root_read_task, "root_read", 4 * 1024,
	                            NULL, CONFIG_MDF_TASK_DEFAULT_PRIOTY, &g_root_read_task_handle, 0);
	            }

	            ret = mwifi_post_root_status(true);
	            MDF_ERROR_BREAK(ret != MDF_OK, "<%s> mwifi_post_root_status", mdf_err_to_name(ret));
				#endif
				
				tc_client_creat();
				//mesh_event_table_update();

#if 0
	            xTaskCreate(root_write_task, "root_write", 4 * 1024,
	                        NULL, CONFIG_MDF_TASK_DEFAULT_PRIOTY, NULL);
	            xTaskCreate(root_read_task, "root_read", 4 * 1024,
	                        NULL, CONFIG_MDF_TASK_DEFAULT_PRIOTY, NULL);
#endif
	        }
			
		break;
		case MDF_EVENT_MUPGRADE_STARTED:
			// todo add device led
			light_status_set(LSYS_STATUS_OTAING);
			MDF_LOGW("Start to upgrade... \n");
			break;
		case MDF_EVENT_MUPGRADE_STOPED:
			light_status_set(LSYS_STATUS_ONLINE);
			break;
		case MDF_EVENT_MUPGRADE_FINISH:
			// todo add device led
			light_status_set(LSYS_STATUS_ONLINE);
			MDF_LOGW("Finish upgrade... \n");
			if(!esp_mesh_is_root() ){
				esp_restart();
			}
			break;
		case MDF_EVENT_MLINK_SYSTEM_REBOOT:
            MDF_LOGW("Restart PRO and APP CPUs");
            esp_restart();
            break;
			
	#if 0
        case MDF_EVENT_CUSTOM_MQTT_CONNECT:
            MDF_LOGI("MQTT connect");
            mwifi_post_root_status(true);
            break;

        case MDF_EVENT_CUSTOM_MQTT_DISCONNECT:
            MDF_LOGI("MQTT disconnected");
            mwifi_post_root_status(false);
            break;
	#endif
        default:
            break;
    }

    return MDF_OK;
}
void app_loop(void *arg){
	while(1){
		// todo 
		mevt_handle();
		// ret task 
		frtc_handle();
		// device 
		device_loop();
		vTaskDelay( 2 / portTICK_RATE_MS);
		light_online_update_loop();
	}
}
void app_main()
{
	/**
     * @brief Set the log level for serial port printing.
     */
    esp_log_level_set("*", ESP_LOG_INFO );
    esp_log_level_set(TAG, ESP_LOG_INFO );
	
	MDF_LOGD("ssid %s \n", CONFIG_ROUTER_SSID);
	MDF_LOGD("password %s \n", CONFIG_ROUTER_PASSWORD);
	
	MDF_LOGD("mesh_id %s \n", CONFIG_MESH_ID);
	MDF_LOGD("mesh_password %s \n", CONFIG_MESH_PASSWORD);
	
	MDF_LOGD("start app main task ...\n");
	/**
     * @brief check flash.
     */
	{
		mdf_err_t rc = MDF_FAIL;
		uint8_t base = 0;
		if( MDF_OK !=  mdf_info_load("S_basic", &base, sizeof(uint8_t)) ){
		
			mdf_info_erase( MDF_SPACE_NAME );
			vTaskDelay( 1000 / portTICK_RATE_MS);
			base = 1;
			if(MDF_OK == mdf_info_save("S_basic", &base, sizeof(uint8_t) ) ){
				
				vTaskDelay( 50 / portTICK_RATE_MS);
				esp_restart();
				vTaskDelay( 1000 / portTICK_RATE_MS);
			}
		}
		
	 }
	/**
    * @brief Initialize wifi mesh.
    */
	MDF_ERROR_ASSERT( mdf_event_loop_init( event_loop_cb ));
    MDF_ERROR_ASSERT( wifi_init() );
	MDF_ERROR_ASSERT( mespnow_init() );
	
	
	//ESP_ERROR_CHECK( heap_trace_init_standalone(trace_record, NUM_RECORDS) );

	/****** get certs ***************/
	tc_certs_init();
	/*** device init ****************/
	light_device_init();
	/** start app task. handle event.****/
	xTaskCreate(app_loop, "device_task", 7 * 1024,
                NULL, CONFIG_MDF_TASK_DEFAULT_PRIOTY, NULL);

	// connet to router if no ssid info start ble to config wifi router.
	light_get_wifi_config();
	
    //MDF_ERROR_ASSERT( mwifi_init(&cfg));
    //MDF_ERROR_ASSERT( mwifi_set_config(&config));
   // MDF_ERROR_ASSERT( mwifi_start() );
	/**
	* @brief Initialize espnow_to_mwifi_task for forward esp-now data to the wifi mesh network.
	* esp-now data from button or other device.
	*/
    // xTaskCreate(espnow_to_mwifi_task, "espnow_to_mwifi", 1024 * 3,  NULL, 1, NULL);

/**
 * @brief Handling data between wifi mesh devices.
 */
	 xTaskCreate(node_handle_task, "node_handle", 5 * 1024,NULL, CONFIG_MDF_TASK_DEFAULT_PRIOTY, NULL);
	
    /**
     * @brief Print system stack info. Please remove it in product.
     */
	TimerHandle_t timer = xTimerCreate("print_system_info", 10000 / portTICK_RATE_MS,
                                       true, NULL, print_system_info_timercb);
    xTimerStart(timer, 0);

}

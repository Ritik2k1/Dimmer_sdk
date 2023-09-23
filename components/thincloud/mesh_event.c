#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

#include "mwifi.h"
#include "mlink.h"

#include "event_queue.h"
#include "utlis.h"
#include "mesh_thincloud.h"
#include "mesh_event.h"
#include "mesh_dev_table.h"



static const char *TAG          = "mesh_event"; 

/******** Node routing tables******************/
typedef struct {

    size_t last_num;
    uint8_t *last_list;
    size_t change_num;
    uint8_t *change_list;
	
} node_list_t;

// 获取最新的 node list
static node_list_t node_list;
/******** 获取当前      被移除的节点****/
static bool _addrs_remove(uint8_t *addrs_list, size_t *addrs_num, const uint8_t *addr)
{
    for (int i = 0; i < *addrs_num; i++, addrs_list += MWIFI_ADDR_LEN) {
        if (!memcmp(addrs_list, addr, MWIFI_ADDR_LEN)) {
            if (--(*addrs_num)) {
                memcpy(addrs_list, addrs_list + MWIFI_ADDR_LEN, (*addrs_num - i) * MWIFI_ADDR_LEN);
            }

            return true;
        }
    }

    return false;
}

// 上报移除的 子设备 事件
mdf_err_t mesh_event_table_remove(void){
	/**
	* @brief find removed device.
	*/
	size_t table_size	  = esp_mesh_get_routing_table_size();
	uint8_t *routing_table = MDF_MALLOC(table_size * sizeof(mesh_addr_t));
	
	ESP_ERROR_CHECK(esp_mesh_get_routing_table((mesh_addr_t *)routing_table,
				   table_size * sizeof(mesh_addr_t), (int *)&table_size));

	for (int i = 0; i < table_size; ++i) {
	   _addrs_remove(node_list.last_list, &node_list.last_num, routing_table + i * MWIFI_ADDR_LEN);
	}

	node_list.change_num  = node_list.last_num;
	node_list.change_list = MDF_MALLOC(node_list.last_num * MWIFI_ADDR_LEN);
	memcpy(node_list.change_list, node_list.last_list, node_list.change_num * MWIFI_ADDR_LEN);

	node_list.last_num  = table_size;
	memcpy(node_list.last_list, routing_table, table_size * MWIFI_ADDR_LEN);
	MDF_FREE(routing_table);


	if(node_list.change_num > 0){
		Evt_mesh_t  mevt = {0};
		
		mevt.cmd = EVT_SYS_TAB_RMV;
		mevt.data_len = node_list.change_num * MWIFI_ADDR_LEN;
		mevt.p_data = MDF_MALLOC( node_list.change_num * MWIFI_ADDR_LEN );
		memcpy(mevt.p_data, node_list.change_list,  node_list.change_num * MWIFI_ADDR_LEN );

		//utlis_byte_printf("change node ", mevt.p_data, mevt.data_len);
		if(MDF_OK != mevt_send( &mevt, 0)){

			MDF_LOGW("send node table remove event failt \n");
			MDF_FREE(mevt.p_data);
		} else{
			MDF_LOGD("send node table remove event successfully !\n");
		}
	}
	
	MDF_FREE(node_list.change_list);
	node_list.change_list = NULL;
	
	return MDF_OK;
}
mdf_err_t _event_handle_table_remove(Evt_mesh_t *p_mevt){

	// 取消订阅
	// 在 tab 中移除.
	if(p_mevt->p_data && p_mevt->data_len >= MWIFI_ADDR_LEN){
		
		uint items = p_mevt->data_len / MWIFI_ADDR_LEN;
		uint8_t *p_del = NULL, p_mac[MWIFI_ADDR_LEN] = {0};
		
		//tc_dev_remove(p_mevt->p_data, (size_t) items);
		utlis_byte_printf("change node ", p_mevt->p_data, p_mevt->data_len);

		for( p_del = p_mevt->p_data;  p_del < (p_mevt->p_data + p_mevt->data_len); p_del +=MWIFI_ADDR_LEN){

			
			MDF_LOGE("Del mac: ");
			MDF_LOGE(_MAC_STR_FORMAT, PR_MAC2STR(p_del));
			
			 memcpy(p_mac, p_del, MWIFI_ADDR_LEN);
			 tc_unsub_dev(p_mac, NULL);
		}
		
	}

	return MDF_OK;
}

// 上报有设备添加到子网下 事件。
mdf_err_t mesh_event_table_add(void){

	/**
	 * @brief find new add device.
	 */
	node_list.change_num  = esp_mesh_get_routing_table_size();
	node_list.change_list = MDF_MALLOC(node_list.change_num * sizeof(mesh_addr_t));
	ESP_ERROR_CHECK(esp_mesh_get_routing_table( ( mesh_addr_t *)node_list.change_list,
	                node_list.change_num * sizeof( mesh_addr_t ), (int *)&node_list.change_num));

	for (int i = 0; i < node_list.last_num; ++i) {
	    _addrs_remove( node_list.change_list, &node_list.change_num, node_list.last_list + i * MWIFI_ADDR_LEN);
	}

	node_list.last_list = MDF_REALLOC(node_list.last_list,
	                                  (node_list.change_num + node_list.last_num) * MWIFI_ADDR_LEN);

	memcpy( node_list.last_list + node_list.last_num * MWIFI_ADDR_LEN,
	        node_list.change_list, node_list.change_num * MWIFI_ADDR_LEN);
	node_list.last_num += node_list.change_num;

	/**
	 * @brief subscribe topic for new node
	 */
	MDF_LOGI("total_num: %d, add_num: %d", node_list.last_num, node_list.change_num);

	if(node_list.change_num > 0){
		Evt_mesh_t  mevt = {0};
	
		mevt.cmd = EVT_SYS_TAB_ADD;
		mevt.data_len = node_list.change_num * MWIFI_ADDR_LEN;
		mevt.p_data = MDF_MALLOC( node_list.change_num * MWIFI_ADDR_LEN );
		memcpy(mevt.p_data, node_list.change_list,  node_list.change_num * MWIFI_ADDR_LEN );

		if(MDF_OK != mevt_send( &mevt, 0)){

			MDF_LOGW("send node table Add event failt \n");
			MDF_FREE(mevt.p_data);
		} else{
			MDF_LOGD("send node table Add event successfully !\n");
		}
	}
	
	MDF_FREE(node_list.change_list);
	node_list.change_list = NULL;
	
	return MDF_OK;
}
mdf_err_t _event_handle_table_add(Evt_mesh_t *p_mevt){

	uint8_t p_mac[MWIFI_ADDR_LEN] = {0};
	// 取消订阅
	// 在 tab 中移除.
	if(p_mevt->p_data && p_mevt->data_len >= MWIFI_ADDR_LEN){
		
		uint8_t *p_add = NULL;

		// todo 
		//tc_dev_add_subdev( TC_SUBSCRIBED, p_mevt->p_data, (size_t) items);

		for( p_add = p_mevt->p_data;  p_add < (p_mevt->p_data + p_mevt->data_len); p_add +=MWIFI_ADDR_LEN){
			memset(p_mac, 0, MWIFI_ADDR_LEN);
			memcpy(p_mac, p_add, MWIFI_ADDR_LEN);
			mlink_get_devinfo_report_send(p_mac);
		}
		
	}

	return MDF_OK;
}

// 获取所有的
// 只有 root 才能调用
/******** 获取当前被移出当前节点下的 routing tables ******/
mdf_err_t  mesh_event_table_update(void){
	/**
	* @brief subscribe topic for all subnode
	*/
	size_t table_size      = esp_mesh_get_routing_table_size();
	uint8_t *routing_table = MDF_MALLOC(table_size * sizeof(mesh_addr_t));
	
	ESP_ERROR_CHECK( esp_mesh_get_routing_table((mesh_addr_t *)routing_table, \
					table_size * sizeof(mesh_addr_t), (int *)&table_size) );

	node_list.last_num  = table_size;
	node_list.last_list = MDF_REALLOC( node_list.last_list, \
	                               node_list.last_num * MWIFI_ADDR_LEN);
	
	memcpy(node_list.last_list, routing_table, table_size * MWIFI_ADDR_LEN);

	MDF_FREE(routing_table);
	
	MDF_LOGD("subscribe %d node", node_list.last_num);

	if(node_list.last_num > 0){
		Evt_mesh_t  mevt = {0};
	
		mevt.cmd = EVT_SYS_TAB_UPD;
		mevt.data_len = node_list.last_num * MWIFI_ADDR_LEN;
		mevt.p_data =  malloc_copy(node_list.last_list, node_list.last_num * MWIFI_ADDR_LEN);

		MDF_ERROR_GOTO( NULL == mevt.p_data, _End, "Failt: all mac list.\n");

		if(MDF_OK != mevt_send( &mevt, 0)){

			MDF_LOGW("send node table Update event failt \n");
			MDF_FREE(mevt.p_data);
			mevt.p_data = NULL;
		} else{
			MDF_LOGD("send node table Update event successfully !\n");
			
		}
	}
	//tc_dev_add_subdev( TC_SUBSCRIBED, node_list.last_list, node_list.last_num);

_End:

	MDF_FREE(node_list.change_list);

	return MDF_OK;
}
mdf_err_t mlink_get_devinfo_report_send(uint8_t *p_mac){

	char *p_json = NULL, p_rq_id[32] = {0};
	mdf_err_t ret = MDF_FAIL;
	//MDF_PARAM_CHECK(tc_is_connect());
	
	random_string_creat(p_rq_id, 16);

	ret = mlink_json_pack(&p_json, "request", "tc_get_dev_info");
	MDF_ERROR_CHECK(ret == MDF_OK, MDF_FAIL, "Failt: mlink_json_pack error \n");

	MDF_LOGD("mesh send data %s to ", p_json);
	MDF_LOGD(_MAC_STR_FORMAT, PR_MAC2STR(p_mac));
	MDF_LOGD("\t\n");
	ret = mesh_send_with_id(p_mac,(uint8_t *) p_rq_id, &p_json);
	
	MDF_FREE(p_json);

	return ret;
}
mdf_err_t _event_handle_table_update(Evt_mesh_t *p_mevt){

	// 通过 mesh 给 mac list 数组中每个 mac 发送 tc_get_dev_info 命令.
	esp_log_level_set(TAG, ESP_LOG_DEBUG);
	
	if( p_mevt->p_data && p_mevt->data_len >= MWIFI_ADDR_LEN){
		
		//uint items = p_mevt->data_len / MWIFI_ADDR_LEN;
		uint8_t *p_update_dev = NULL;
		uint8_t p_mac[MWIFI_ADDR_LEN] = {0};
		MDF_LOGD("Event updating routing table.\n");
		// todo  get device id.		
		for( p_update_dev = p_mevt->p_data;  p_update_dev < (p_mevt->p_data + p_mevt->data_len); p_update_dev += MWIFI_ADDR_LEN){

			memset(p_mac, 0, MWIFI_ADDR_LEN);
			memcpy(p_mac, p_update_dev, MWIFI_ADDR_LEN);
			mlink_get_devinfo_report_send(p_mac);
		}
		
	}

	return MDF_OK;
}
// 1.发出 commission 请求， 完成用户绑定设备
// p_mac ==  NULL 时，则直接发送本机 mac commission request.
mdf_err_t event_make_commission(uint8_t *p_mac)
{
	mdf_err_t rc =0;
	Evt_mesh_t msg = {0};
	if(p_mac){
		memcpy(msg.p_mac, p_mac, 6);
	}else{
	    esp_wifi_get_mac(ESP_IF_WIFI_STA, msg.p_mac);
	}

	msg.cmd = MEVT_TC_COMMISSION_REQUEST;
	rc = mevt_send(&msg, 10/portTICK_RATE_MS );
	if(rc != MDF_OK){
		MDF_LOGW("Failt to send MEVT_TC_COMMISSION_REQUEST \n");
	}

	return rc;
}
void mesh_event_init(void){

    esp_log_level_set(TAG, ESP_LOG_INFO);

	mevt_handle_func_register( _event_handle_table_update, EVT_SYS_TAB_UPD);
	mevt_handle_func_register( _event_handle_table_add, EVT_SYS_TAB_ADD);	
	mevt_handle_func_register( _event_handle_table_remove, EVT_SYS_TAB_RMV);
}


#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "mwifi.h"
#include "tc_event.h"
#include "mdf_err.h"
#include "utlis.h"


static Func_tc_evt_handle func_tc_evt_handle[TC_EVT_CMD_MAX] = {0};

static const char *TAG          = "tc_event";

void tc_evt_msg_clean(Tc_evt_msg_t *p_msg){
	if(p_msg){
		MDF_FREE(p_msg->p_cmdid);
		MDF_FREE(p_msg->p_devid);
		MDF_FREE(p_msg->p_mac);
		MDF_FREE(p_msg->p_method);
		MDF_FREE(p_msg->p_data);
		memset(p_msg, 0, sizeof(Tc_evt_msg_t) );
		
		MDF_LOGV("End event Tc_evt_msg_t  Free heap %u\n", esp_get_free_heap_size( ) );
	}
	
	MDF_LOGV("-->end Free heap %u\n", esp_get_free_heap_size());
}
void tc_evt_msg_destory(Tc_evt_msg_t *p_msg){
	if(p_msg){
		MDF_FREE(p_msg->p_cmdid);
		MDF_FREE(p_msg->p_devid);
		MDF_FREE(p_msg->p_mac);
		MDF_FREE(p_msg->p_method);
		MDF_FREE(p_msg->p_data);
		
		memset(p_msg, 0, sizeof(Tc_evt_msg_t) );
		MDF_FREE(p_msg);
		
	MDF_LOGV("-->end Free heap %u\n", esp_get_free_heap_size());
	}
	
}


 mdf_err_t tc_event_function_register( TC_EVT_CMD cmd, Func_tc_evt_handle func){
	MDF_PARAM_CHECK(cmd < TC_EVT_CMD_MAX);
	MDF_PARAM_CHECK(func);

	func_tc_evt_handle[cmd] = func;

	return MDF_OK;
}
void handle_tc_event(xQueueHandle evt_queue){
	Tc_evt_msg_t evt = {0};
	if( xQueueReceive( evt_queue, &evt, ( TickType_t )10) == pdPASS ){
		if(evt.cmd < TC_EVT_CMD_MAX){
			func_tc_evt_handle[evt.cmd](&evt);
			
		}
		tc_evt_msg_clean(&evt);
	}
}

xQueueHandle tc_event_init(void){
	
    esp_log_level_set(TAG, ESP_LOG_WARN);
	return xQueueCreate(5, sizeof(Tc_evt_msg_t) );
}
void tc_event_deint(xQueueHandle evt_queue){
	Tc_evt_msg_t evt = {0};
	while(1){
		memset(&evt, 0, sizeof(Tc_evt_msg_t));
		
		if( xQueueReceive( evt_queue, &evt, ( TickType_t )10) == pdPASS ){
			if(evt.cmd < TC_EVT_CMD_MAX){
				func_tc_evt_handle[evt.cmd](&evt);
				
			}
			tc_evt_msg_clean(&evt);
		}else{
			// 确保队列事件已经处理完成.
			break;
		}
	}
	
    vQueueDelete(evt_queue);
	
}

#include "utlis.h"
#include "event_queue.h"
#include "mlink.h"


static const char *TAG          = "event_queue"; 

xQueueHandle mevt_queue = NULL;
Func_evt_handle _func_evt_handle[MEVT_CMD_MAX];

Func_evt_handle func_rtc_handle[FRTC_CMD_MAX];

void *p_frtc_arg[FRTC_CMD_MAX] = {0};

mdf_err_t frtc_function_register(F_RTC_CMD cmd, 	Func_evt_handle  func, void *p_arg ){
	MDF_PARAM_CHECK(cmd < FRTC_CMD_MAX);
	MDF_PARAM_CHECK(func);

	func_rtc_handle[cmd] = func;
	p_frtc_arg[cmd] =  p_arg;

	return MDF_OK;
}
void frtc_handle(void){
	int i = 0;

	for(i =0;i<FRTC_CMD_MAX; i++ ){
		if( func_rtc_handle[i] )
			func_rtc_handle[i](p_frtc_arg[i]);
	}
	
}
mdf_err_t mevt_handle_func_register(Func_evt_handle f_handle, M_EVENT_CMD cmd){
	MDF_PARAM_CHECK(cmd < MEVT_CMD_MAX);
	MDF_PARAM_CHECK(f_handle);
	
	_func_evt_handle[cmd] = f_handle;

	return MDF_OK;
}
void mevt_clean(Evt_mesh_t *p_evt){

	if(p_evt){
		MDF_FREE(p_evt->p_data);
		MDF_FREE(p_evt->p_cid);
		p_evt->p_data = NULL;
		p_evt->p_cid = NULL;
	}
	
}

// 返回 0 正常，否则为销毁该线程.
uint8_t mevt_handle(void){
	static uint8_t destory = 0;
	Evt_mesh_t mevt = {0};

	if( !mevt_queue)
		return 0;
	
	memset(&mevt, 0, sizeof(Evt_mesh_t) );
	
	if(xQueueReceive(mevt_queue, &mevt, (TickType_t) 30) == pdPASS){
			
		if( mevt.cmd < MEVT_CMD_MAX){
			
			if(mevt.cmd == EVT_SYS_DESTORY_QUEUE)
				destory = 1;
			
			MDF_LOGV("evt cmd %d func %p  \n", mevt.cmd , _func_evt_handle[mevt.cmd]);
			if(_func_evt_handle[ mevt.cmd]){
				_func_evt_handle[mevt.cmd](&mevt);
			}
		}else{
			
			MDF_LOGW("Unknow event commmand !!\n");
		}
		mevt_clean(&mevt);
		MDF_LOGV("End Evt_mesh_t  Free heap %u\n", esp_get_free_heap_size( ) );
	}else if(destory){
		destory =0;
		return 1;
	}
	
	return 0;
}

void evt_loop_task(void *p_arg){	
	while(1){
		// 接受到 销毁指令，同时队列为空，则销毁.
		if( mevt_handle())
			break;

		Delay_ms(20);
	}
	

	if( mevt_queue )
		vQueueDelete(mevt_queue);

	MDF_LOGW("Mqueue was desoty ! \n");
	mevt_queue = NULL;
	vTaskDelete(NULL);
}

mdf_err_t mevt_send(Evt_mesh_t *p_mevt, uint32_t tm_to_wait){
	if( !xQueueSend(mevt_queue, p_mevt, tm_to_wait) )
		return MDF_FAIL;

	return MDF_OK;
}
mdf_err_t mevt_init(void){
	
    esp_log_level_set(TAG, ESP_LOG_INFO);
	if( NULL == mevt_queue ){
		mevt_queue =  xQueueCreate( 10, sizeof( Evt_mesh_t ) );
		
	}
	
	return MDF_OK;
}

mdf_err_t mevt_deinit(void){
	Evt_mesh_t mevt = {0};

	mevt.cmd = EVT_SYS_DESTORY_QUEUE;
	mevt_send(&mevt, 1000);
	MDF_LOGW("Send destory queue event !!\n");
	return MDF_OK;
}

/***
** 源数据json:
{
	  "request":"xxx",
	  "deviceId":"xxx",
	  "cid": "98d67f00-ae7d-4830-9e09-8a2767760cad",
	  "data": {"power":1,"timer":"00:00:00","name":"demo","type":"","brightness":50,"fade":1.02,"vacationmode":1,"remote_id":"02-xx","learn":true}
	}

**
*************/
mdf_err_t mevt_command_respond_creat(char *p_src_json, int status_code, char *p_data){
	mdf_err_t rc = 0;
	Evt_mesh_t evt = {0};

	MDF_PARAM_CHECK(p_src_json);
	
	rc = mlink_json_parse(p_src_json, "deviceId", (char *)evt.p_devid);
	
	MDF_ERROR_GOTO(NULL==evt.p_devid, Error_end, "No device Id\n");
	
	rc = mlink_json_parse(p_src_json, "cid", (char **)&evt.p_cid);
	MDF_ERROR_GOTO(NULL==evt.p_devid, Error_end, "No cid\n");

	if(p_data){
		evt.p_data = malloc_copy((uint8_t *)p_data, strlen(p_data) );
	}
	
	evt.cmd = MEVT_TC_COMMAND_RESPOND;
	evt.status_code = status_code;
	rc = mevt_send(&evt, 10/portTICK_RATE_MS);
	MDF_ERROR_GOTO( MDF_OK != rc, Error_end, "Fait to send MEVT_TC_COMMAND_RESPOND\n");

	return rc;
	
Error_end:
	mevt_clean(&evt);
	
	return rc;
}

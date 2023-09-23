#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include "mdf_err.h"
#include "event_queue.h"
#include "utlist.h"
#include "utlis.h"
#include "mesh_dev_table.h"


static const char *TAG          = "mesh_dev_table"; 

static SemaphoreHandle_t tab_lock = NULL;
static Dev_tab_t *p_had = NULL;

#define _LOCK_TAB() ( NULL != tab_lock &&  xSemaphoreTake( tab_lock, ( TickType_t ) 100 ) == pdTRUE )
#define _UNLOCK_TAB()	(xSemaphoreGive( tab_lock ))



Dev_tab_t *dev_tab_add(unsigned char *p_mac, unsigned char *p_devid){

	Dev_tab_t *p_find = NULL, *p_add = NULL;

    esp_log_level_set(TAG, ESP_LOG_INFO);

	if( !tab_lock)
		dev_tab_creat();

	if(  (p_mac || p_devid) ){
		
		p_find	= dev_tab_find(p_mac, p_devid);
		
		if( !p_find ){
			
			p_add = MDF_MALLOC( sizeof(Dev_tab_t) );
	
			if( !p_add ){
				
				MDF_LOGE("Failt: Alloc failt \n");
				return NULL;
			}
			
			memset( p_add, 0, sizeof(Dev_tab_t));
			p_find = p_add;
		}
		if(_LOCK_TAB()){

			if(p_mac){
				MDF_LOGD("add mac ");
				//MDF_LOGI(_MAC_STR_FORMAT, PR_MAC2STR(p_mac) );
				memcpy( p_find->p_mac, p_mac, 6 );
			}
			
			if(p_devid){
				MDF_LOGD("add devid %s\n", p_devid);
				memcpy( p_find->p_devid, p_devid, TC_ID_LENGTH );
				}
			// 如果是新增建立的则添加到链表.
			if(p_add)
				LL_APPEND(p_had, p_find);

			_UNLOCK_TAB();
		}
	}else{
		
		MDF_LOGE("Failt to add tab to list \n");
		MDF_FREE(p_find);
		p_find = NULL;

	}
	
	return p_find;
}
Dev_tab_t *dev_tab_find(unsigned char *p_mac, unsigned char *p_devid){
	Dev_tab_t *p_find = NULL;
	Dev_tab_t *el = NULL, *tmp = NULL;

	if(NULL == p_had)
		return NULL;
	//MDF_ERROR_CHECK( NULL == p_had, NULL, "Device tables have  not creat ! Head pointer was NULL!! \n");
	
	if(p_had && (p_mac || p_devid) && _LOCK_TAB() ){
		LL_FOREACH_SAFE(p_had, el, tmp){
			if(p_mac){
				
				if(!memcmp( el->p_mac, p_mac, 6) ){
					p_find = el;
					break;
				}
			}else if( p_devid ){
				if(!memcmp( el->p_devid, p_devid, TC_ID_LENGTH) ){
					p_find = el;
					break;
				}
			}
		}

		_UNLOCK_TAB();
	}

	if(p_find)
		MDF_LOGD("Find device id %s\n", p_find->p_devid);

	return p_find;
}
extern uint8_t *light_devid_get(void);
char *dev_tap_deviceid_json_get(void){
	char *p_array = NULL, p_devid[TC_ID_LENGTH + 4] = {0};
	bool more = false;
	int len  = 2;
	char *p_l_devid = (char *) light_devid_get();
	
	p_array = (char *)utlis_malloc(len);
	MDF_ERROR_CHECK( NULL == p_array, NULL, "Failt to malloc");

	p_array[0]= '[';
	p_array[1] = '\0';
	
	if(p_had && _LOCK_TAB()){
		Dev_tab_t *p_el = NULL, *p_tmp = NULL;
		int id_len =0;
	
		LL_FOREACH_SAFE(p_had, p_el, p_tmp){

			if( p_l_devid && 0 ==  memcmp( p_el->p_devid, p_l_devid, TC_ID_LENGTH) )
				continue;
			
			id_len = strlen( (char *)p_el->p_devid );
			if(p_el->p_topic_sub_cmd && id_len > 0){
				int sub_len  = 0;
				memset(p_devid, 0, TC_ID_LENGTH + 4);
				if( more){
					p_devid[0] = ',';
					sub_len = 1;
				}
				p_devid[sub_len] = '\"';
				strcat( p_devid, (char *)p_el->p_devid );
				p_devid[strlen( p_devid )] = '\"';
				
				len += strlen(p_devid);
				p_array = MDF_REALLOC(p_array, len);
				if(p_array){
					strcat( p_array, (char *)p_devid);
					more = true;
				}else{
					MDF_LOGE("Failt to realloc");
					_UNLOCK_TAB();
					return NULL;
				}
			}

		}
		_UNLOCK_TAB();
	}
	if(p_array ){
		len = strlen(p_array) + 2;
		p_array = MDF_REALLOC(p_array, len);
		p_array[len - 2] = ']';
		p_array[len - 1] = '\0';
		return p_array;
	}
	
	return NULL;
}

mdf_err_t dev_tab_del(Dev_tab_t *p_del){

	MDF_PARAM_CHECK(p_had);
	
	if(p_del && _LOCK_TAB() ){
		LL_DELETE( p_had, p_del);

		MDF_FREE(p_del->p_topic_sub_cmd);
		MDF_FREE(p_del->p_topic_sub_cmis);
		MDF_FREE(p_del->p_topic_sub_respond);
		MDF_LOGD("1\n");

		MDF_FREE(p_del);
		_UNLOCK_TAB();
		}
	return MDF_OK;
}
mdf_err_t dev_tab_remove(unsigned char *p_mac, unsigned char *p_devid){
	Dev_tab_t *p_del = NULL;

	MDF_PARAM_CHECK(p_had);
	
	if(p_had && (p_mac || p_devid) ){
		p_del = dev_tab_find(p_mac, p_devid);
		return dev_tab_del( p_del);
	}
	
	return MDF_OK;
}
Dev_tab_t * dev_tab_had_get(void){
	return p_had;
}

Dev_tab_t *dev_tab_creat(void){

    esp_log_level_set(TAG, ESP_LOG_WARN);

	if(NULL == tab_lock){
		MDF_LOGI("Creat tab list head \n");
		tab_lock = xSemaphoreCreateMutex();

	}
	return NULL;
}

mdf_err_t dev_tab_destory(){

	if(p_had && _LOCK_TAB()){
		Dev_tab_t *p_el = NULL, *p_tmp = NULL;

		LL_FOREACH_SAFE(p_had, p_el, p_tmp){
		
			MDF_FREE(p_el->p_topic_sub_cmd);
			MDF_FREE(p_el->p_topic_sub_cmis);
			MDF_FREE(p_el->p_topic_sub_respond);
			
			LL_DELETE(p_had, p_el);
			MDF_FREE(p_el);
		}

		p_had = NULL;
		_UNLOCK_TAB();
	}
	
	return MDF_OK;
}


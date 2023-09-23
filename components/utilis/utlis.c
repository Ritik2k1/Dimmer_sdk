/*
 * @Author: sky
 * @Date: 2020-02-28 15:22:51
 * @LastEditTime: 2020-02-28 15:23:53
 * @LastEditors: Please set LastEditors
 * @Description: In User Settings Edit
 * @FilePath: \mqtt_example\components\utilis\utlis.c
 */

#include <time.h>
#include <sys/time.h>
#include "cJSON.h"

#include "mwifi.h"
#include "utlis.h"
#include "mlink.h"

#include "mdf_common.h"
#include "mdf_info_store.h"
#include "lwip/apps/sntp.h"


static const char *TAG          = "utlis";

// malloc + memset 
void *utlis_malloc(size_t size){
	void *p = NULL;
	if(size){
		p = MDF_MALLOC(size);
		if(p){
			memset(p, 0, size);
		}
	}
	
	return p;
}
size_t utils_info_len_get(const char *key){
	
	MDF_PARAM_CHECK(key);
	
	esp_err_t ret	  = ESP_OK;
	nvs_handle handle = 0;
	size_t length = NULL;



	/**< Initialize the default NVS partition */
	mdf_info_init();

	/**< Open non-volatile storage with a given namespace from the default NVS partition */
	ret = nvs_open(MDF_SPACE_NAME, NVS_READWRITE, &handle);
	MDF_ERROR_CHECK(ret != ESP_OK, ret, "Open non-volatile storage");

	/**< get variable length binary value for given key */
	ret = nvs_get_blob(handle, key, NULL, &length);

	/**< Close the storage handle and free any allocated resources */
	nvs_close(handle);

	if (ret == ESP_ERR_NVS_NOT_FOUND) {
		MDF_LOGD("<ESP_ERR_NVS_NOT_FOUND> Get len for given key, key: %s", key);
		return 0;
	}

	MDF_ERROR_CHECK(ret != ESP_OK, ret, "Get len for given key, key: %s", key);

	return length;
}
void *utlis_info_load(char *p_key, int *p_len){
	mdf_err_t rc =0;
	int rlen = 0;
	char p_tmp[3] = {0};
	void *p = NULL;

	MDF_ERROR_CHECK( NULL == p_key, NULL, "No key\n");
	rlen = utils_info_len_get(p_key);
	MDF_LOGD("get len %d \n", rlen);
	
	if( rlen > 0){
		p = utlis_malloc( rlen );
		MDF_ERROR_CHECK( NULL == p, NULL, "No men\n");

		rc = mdf_info_load(p_key, p, rlen );
		if(rc != ESP_OK){
			MDF_FREE(p);
			MDF_LOGE("Failt to get info-key %s from flash\n", p_key);
		}else{
			*p_len = rlen;
		}
	}
	
	return p;
}
size_t utils_system_info_len_get(Store_space_t spa, const char *key){
	
	MDF_PARAM_CHECK(key);
	
	esp_err_t ret	  = ESP_OK;
	nvs_handle handle = 0;
	size_t length = NULL;



	/**< Initialize the default NVS partition */
	mdf_info_init();

	/**< Open non-volatile storage with a given namespace from the default NVS partition */
	ret = nvs_open(_p_store_space_str[spa], NVS_READWRITE, &handle);
	MDF_ERROR_CHECK(ret != ESP_OK, ret, "Open non-volatile storage");

	/**< get variable length binary value for given key */
	ret = nvs_get_blob(handle, key, NULL, &length);

	/**< Close the storage handle and free any allocated resources */
	nvs_close(handle);

	if (ret == ESP_ERR_NVS_NOT_FOUND) {
		MDF_LOGD("<ESP_ERR_NVS_NOT_FOUND> Get len for given key, key: %s", key);
		return 0;
	}

	MDF_ERROR_CHECK(ret != ESP_OK, ret, "Get len for given key, key: %s", key);

	return length;
}

void *utlis_system_info_load(Store_space_t spa,char *p_key, int *p_len){
	mdf_err_t rc =0;
	int rlen = 0;
	char p_tmp[3] = {0};
	void *p = NULL;

	MDF_ERROR_CHECK( NULL == p_key, NULL, "No key\n");
	rlen = utils_system_info_len_get(spa, p_key);
	MDF_LOGD("get len %d \n", rlen);
	
	if( rlen > 0){
		p = utlis_malloc( rlen );
		MDF_ERROR_CHECK( NULL == p, NULL, "No men\n");

		rc = utlis_store_load(spa, p_key, p, rlen );
		if(rc != ESP_OK){
			MDF_FREE(p);
			MDF_LOGE("Failt to get info-key %s from flash\n", p_key);
		}else{
			*p_len = rlen;
		}
	}
	
	return p;
}


uint8_t *malloc_copy(uint8_t *p_src, int len){
	uint8_t *p = MDF_MALLOC( len + 1 );
	if(!p){
		MDF_LOGE("Failt to malloc \n");
		return NULL;
	}

	memset(p, 0, len + 1);
	memcpy(p, p_src, len);

	return p;
}
uint8_t *malloc_copy_str(char *p_string){
	return malloc_copy((uint8_t *)p_string, strlen(p_string) );
}

void random_string_creat(char *p_dst, int len){
	uint8_t  i;
	uint32_t r = esp_random();
	
	memset(p_dst, 0x30, len-1);
	for(i=0; i < len && r > 0; i++){
		p_dst[i] = 0x30 +  r % 10;
		r = r / 10;
	}
}
// x^y
int64_t utils_pow(int x, int y){
	int i = 0;
	int64_t out =1;
    
	for(i=0;i<y;i++){
		if(i < 1){
			out = x;
		}else
			out *=  x;
	}
	
	return out;

}
void utlis_byte_printf(const char *p_str , uint8_t *p_src, int len){
	if(p_str)
		printf("\n%s:", p_str);
	if( p_str && len > 0 ){
		int i=0;

		for(i=0;i<len;i++)
			printf("[%02x]", p_src[i]);

		printf("\n");
	}
}
int64_t utils_get_current_time_ms(void){

  //nowtime=gmtime(&now);
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return  (tv.tv_sec * 1000LL + (tv.tv_usec / 1000LL));
}
int utils_get_current_week( void ){
	time_t now = time(NULL);

	//nowtime=gmtime(&now);
	struct tm ctime = {0};
	localtime_r(&now, &ctime);

	return ctime.tm_wday;
}
// 获取当前的周时间, 即距离周一零刻的秒数。最大是 '0x93a80'
uint32_t _sys_get_current_wtime(void)
{
	uint32_t dtime = 0;
	time_t now = time(NULL);

	//nowtime=gmtime(&now);
	struct tm ctime = {0};
	localtime_r(&now, &ctime);

	dtime =( +  ctime.tm_hour*3600 + ctime.tm_min*60 + ctime.tm_sec);
	dtime += ctime.tm_wday * 24 * 60 * 60 ;
	return dtime;
}

mdf_err_t mesh_data_send(uint8_t *dst_addr, const char* data,int len)
{

	MDF_LOGI("mesh send data %s to ", dst_addr);
	MDF_LOGI("\t\n");
	MDF_LOGI("data :: %s \n ", data);
	//MDF_LOGI(_MAC_STR_FORMAT, PR_MAC2STR(dst_addr));	
	 //const static mwifi_data_type_t _mesh_transmit_type	  = {0,0,0x1,0x0,0x1,0xa0000};
	 mwifi_data_type_t mwifi_type = {
		 .communicate = MWIFI_COMMUNICATE_MULTICAST,
		 .protocol = MLINK_PROTO_HTTPD,
	 };
	 mlink_httpd_type_t header_info = {
		 .format = MLINK_HTTPD_FORMAT_JSON,
		 .from	 = MLINK_HTTPD_FROM_DEVICE,
		 .resp	 = false,
	 };
    memcpy(&mwifi_type.custom, &header_info, sizeof(mlink_httpd_type_t));
	 return mwifi_write(dst_addr, &mwifi_type, data,len, true);
}
// 注意  p_data_j_str 必须是 json string. 否则会出错.
mdf_err_t mesh_send_with_id(uint8_t *dst_addr, uint8_t *p_request_id,  char **pp_data_j_str)
{
	MDF_PARAM_CHECK( NULL != p_request_id );
	
	mlink_json_pack(pp_data_j_str, "cid", p_request_id);

	MDF_LOGI("mesh send data %s to ", *pp_data_j_str);
	MDF_LOGI(_MAC_STR_FORMAT, PR_MAC2STR(dst_addr));
	MDF_LOGI("\t\n");
	
	return mesh_data_send(dst_addr, *pp_data_j_str, strlen( *pp_data_j_str ));
	
}
mdf_err_t mesh_send_2root_with_id(uint8_t *p_request_id,  char **pp_data_j_str)
{
	
	uint8_t dst_addr[6]=MWIFI_ADDR_ROOT;
	char p_cid[32] = {0};

	if( NULL == p_request_id ){
		random_string_creat(p_cid, 16);
	}else{
		memcpy(p_cid, p_request_id, strlen((char *)p_request_id));
	}
	
	mlink_json_pack(pp_data_j_str, "cid", p_cid);

	MDF_LOGI("mesh send data %s \n", *pp_data_j_str);
	
	return mesh_data_send(dst_addr, *pp_data_j_str, strlen( *pp_data_j_str ));
	
}
/********* Array json handle *************************/
mdf_err_t utlis_json_array_get_item(	
				void **pp_json_output,const char *json_str,  
				const char *key, const char *sub_key){
				
	mdf_err_t rc = MDF_FAIL;
	cJSON *p_array = NULL, *p_item = NULL;
	cJSON *p_cjson = cJSON_Parse(json_str);
	
	MDF_ERROR_CHECK( NULL == p_cjson, MDF_FAIL, "Failt: Illgel json\n");

	p_array = cJSON_GetObjectItemCaseSensitive(p_cjson, key);
	
	if( cJSON_IsArray( p_array) ){
		
		cJSON_ArrayForEach(p_item, p_array){
			
			if(  sub_key && cJSON_GetObjectItemCaseSensitive(p_item, sub_key)  ){
				
				char *p_strjson = cJSON_PrintUnformatted(p_item);
				MDF_ERROR_GOTO(!p_strjson, ERR_EXIT, "cJSON_PrintUnformatted");
				MDF_LOGV("find sub json %s\n", p_strjson);
				
				*((char **)pp_json_output) = p_strjson;
				rc =  MDF_OK;
				break;
			}
		}
	}
	
ERR_EXIT:

    cJSON_Delete(p_cjson);
	return rc;	
}
void unix_time2string(int64_t second, char *p_str, int len){
	time_t rawtime = second;
	struct tm timeinfo = { 0 };

	localtime_r(&rawtime, &timeinfo );
	
	strftime(p_str, len, "%c", &timeinfo);
}

void local_time_printf(void){
	time_t now = 0;
	struct tm timeinfo = { 0 };
	char strftime_buf[64] = {0};

	time(&now);
	localtime_r(&now, &timeinfo );

	strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    MDF_LOGI("Current date/time in UTC  is: %s", strftime_buf);
}


int local_time_zone_get(void){
	time_t time_utc=0;
	struct tm *p_tm_time;
	int time_zone = 0;
	
	p_tm_time = localtime( &time_utc );//转成当地时间bai

	
//	MDF_LOGE("Yesr : %d tm_sec %d tm_isdst %d \n", p_tm_time->tm_year, p_tm_time->tm_sec, p_tm_time->tm_isdst);
	time_zone = ( p_tm_time->tm_hour > 12)?(p_tm_time->tm_hour-=24):p_tm_time->tm_hour;
//	MDF_LOGE("time_zone: %d \n", time_zone);
	return time_zone;
}

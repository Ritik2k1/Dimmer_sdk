#ifndef _M_UTILIS_H
#define _M_UTILIS_H

#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_mesh.h"
#include "mdf_err.h"
#include "utlis_store.h"


/// 宏定义
#define PATTERN_REQ_MQTT_GET_DEV_ID	("{\"request\":\"get_deviceId\",\"deviceId\":\"%s\",\"mac\":\"%s\"}")

#define PR_MAC2STR(a) (a)[0],(a)[1],(a)[2],(a)[3],(a)[4],(a)[5]

#ifndef DIFF
#define DIFF(a,b)  (( (a) > (b) )?( (a) - (b) ):( (b)- (a) ))
#endif

#ifndef MINI
#define MINI(a,b) (a > b)? (b):(a)
#endif

#define _MAC_STR_FORMAT  "%02x%02x%02x%02x%02x%02x"

#define _OLD_PY_ID_FORMAT "002-%02x%02x%02x%02x%02x%02x"

#define _PY_ID_FORMAT "009-%02x%02x%02x%02x%02x%02x"

#define _CLIENT_ID_FORMAT "CID-001-%02x%02x%02x%02x%02x%02x"

#define CHECK_ERR_NO_RETURN(con) do { \
        if (!(con)) { \
            MDF_LOGE("<MDF_ERR_INVALID_ARG> !(%s)", #con); \
            return ; \
        } \
    } while(0)

#define MAC_STR_2_BYTE(B, A)  	do{ \
								sscanf(A, _MAC_STR_FORMAT, (uint*)&(B)[0],(uint*)&(B)[1],(uint*)&(B)[2],(uint*)&(B)[3],(uint*)&(B)[4],(uint*)&(B)[5]); \
								}while(0)

#define DEV_IS_ROOT()	( esp_mesh_get_layer() == MESH_ROOT)

#define Delay_ms(ms)	vTaskDelay( ms / portTICK_RATE_MS)

/************ Functions ****************************/
void *utlis_malloc(size_t size);
void *utlis_info_load(char *p_key, int *p_len);
size_t utils_info_len_get(const char *key);
size_t utils_system_info_len_get(Store_space_t spa, const char *key);
void *utlis_system_info_load(Store_space_t spa,char *p_key, int *p_len);


void random_string_creat(char *p_dst, int len);
uint8_t *malloc_copy(uint8_t *p_src, int len);
uint8_t *malloc_copy_str(char *p_string);

void utlis_byte_printf(const char *p_str , uint8_t *p_src, int len);

int64_t utils_get_current_time_ms(void);
int utils_get_current_week( void );

uint32_t _sys_get_current_wtime(void);
mdf_err_t mesh_send_with_id(uint8_t *dst_addr, uint8_t *p_request_id,  char **pp_data_j_str);
mdf_err_t mesh_send_2root_with_id(uint8_t *p_request_id,  char **pp_data_j_str);

mdf_err_t utlis_json_array_get_item(	
				void **pp_strjson_output,const char *json_str,  
				const char *key, const char *sub_key);
int64_t utils_pow(int x, int y);
void local_time_printf(void);
int local_time_zone_get(void);

void unix_time2string(int64_t second, char *p_str, int len);

#endif

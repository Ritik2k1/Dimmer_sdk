#ifndef _MESH_DEV_TABLE_T

#define	_MESH_DEV_TABLE_T
#include "freertos/FreeRTOS.h"

#include "thincloud.h"

typedef struct DEV_TAB_LIST_T{
	struct DEV_TAB_LIST_T *next;

	char flag_sub_cmd;
	char flag_sub_respond;
	uint8_t p_mac[6];
	uint8_t p_devid[TC_ID_LENGTH];

	char *p_topic_sub_cmd;
	char *p_topic_sub_cmis;
	
	char *p_topic_sub_respond;
	
}Dev_tab_t;



Dev_tab_t *dev_tab_add(unsigned char *p_mac, unsigned char *p_devid);

Dev_tab_t *dev_tab_find(unsigned char *p_mac, unsigned char *p_devid);
mdf_err_t dev_tab_remove(unsigned char *p_mac, unsigned char *p_devid);
mdf_err_t dev_tab_del(Dev_tab_t *p_del);

Dev_tab_t * dev_tab_had_get(void);
Dev_tab_t *dev_tab_creat(void);
mdf_err_t dev_tab_destory(void);
char *dev_tap_deviceid_json_get(void);



#endif


#ifndef _MESH_EVENT_H
#define	_MESH_EVENT_H

#include <stdint.h>
#include "freertos/FreeRTOS.h"



mdf_err_t mesh_event_table_remove(void);
mdf_err_t mesh_event_table_add(void);
mdf_err_t mesh_event_table_update(void);

mdf_err_t event_make_commission(uint8_t *p_mac);

// 注册事件处理函数.
void mesh_event_init(void);
mdf_err_t mlink_get_devinfo_report_send(uint8_t *p_mac);


#endif //_MESH_EVENT_H


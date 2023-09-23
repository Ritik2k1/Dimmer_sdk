
#ifndef _TC_EVENT_H
#define _TC_EVENT_H
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
// #include "freertos/semphr.h"

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "mdf_err.h"

typedef enum {

    TC_EVT_CMD_NONE,
    TC_EVT_TC_CMIS,		// 收到 commission 回应.
    TC_EVT_TC_CMD,		// 收到 tc command 推送.
    TC_EVT_GET_DEV_INFO,	// root 节点请求 设备回应信息.
    
    TC_EVT_CMD_MAX
}TC_EVT_CMD;
typedef struct TC_EVT_MSG_T{
	
	TC_EVT_CMD cmd;

	uint8_t *p_cmdid;
	uint8_t *p_devid;
	uint8_t *p_mac;
	uint8_t *p_method;
	uint8_t *p_data;
	
}Tc_evt_msg_t;

typedef mdf_err_t (*Func_tc_evt_handle)( Tc_evt_msg_t * );


void tc_evt_msg_clean(Tc_evt_msg_t *p_msg);
void tc_evt_msg_destory(Tc_evt_msg_t *p_msg);


mdf_err_t tc_event_function_register( TC_EVT_CMD cmd, Func_tc_evt_handle func);
void handle_tc_event(xQueueHandle evt_queue);
xQueueHandle tc_event_init(void);
void tc_event_deint(xQueueHandle evt_queue);


#endif  // _TC_EVENT_H

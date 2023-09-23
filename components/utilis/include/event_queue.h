
#ifndef _EVENT_QUEUE_H
#define _EVENT_QUEUE_H

#include <stdint.h>
#include "thincloud.h"
typedef enum {
	EVT_CMD_NONE,
	EVT_SYS_REST,	// 重启
	EVT_SYS_TAB_UPD,	// 更新整个子网络的 table, 连接路由成功和服务器成功时应该触发该事件，只有 root 能触发.
	EVT_SYS_DESTORY_QUEUE,	//销毁 事件队列 自身.
	EVT_SYS_TAB_ADD, // 有新节点加入自网络.只有 root 节点能触发.
	EVT_SYS_TAB_RMV, // 子 mesh 有设备移除. 只有root 节点能触发
	EVT_SYS_OTA_START,	// 开始升级
	EVT_SYS_TYPE_SET,	// 设置 设备类型.
	EVT_SYS_WIFI_RESET,	//忘记 wifi 密码.
	EVT_SYS_FACTORY_REST, // 恢复出厂设置.
	EVT_SYS_DEV_STATUS_SET,	// 修改设备的状态.
	
	MEVT_TC_INFO_REPORT,		// 上报设备状态信息.
	MEVT_TC_PUBLISH_TO_SERVER,	// root 节点发送数据到 服务器.
	MEVT_TC_COMMAND,			// tc 格式 指令.
	MEVT_TC_COMMAND_RESPOND,	// comand 回应
	MEVT_TC_COMMISSION_REQUEST,	// Commission reuest 请求.
	
	EVT_BUTTON_TU,
	EVT_BUTTON_TD,
	EVT_BUTTON_DTU,
	EVT_BUTTON_DTD,
	EVT_BUTTON_HOLD_START_UP,
	EVT_BUTTON_HOLD_START_DOWN,
	EVT_BUTTON_HOLD_START_DUP,
	EVT_BUTTON_HOLD_START_DDOWN,
	EVT_BUTTON_HOLD_STOP,

	EVT_UART_CMD,
	
	MEVT_CMD_MAX 
}M_EVENT_CMD;

typedef enum{
	FRTC_CMD_SCH,

	FRTC_CMD_MAX
}F_RTC_CMD;

typedef struct EVT_MESH_T{

	M_EVENT_CMD cmd;
	/* todo  */
	uint16_t status_code;	// 回应码
	uint8_t p_mac[6];
	uint8_t p_devid[TC_ID_LENGTH];
	uint8_t *p_cid;
	uint16_t data_len;
	uint8_t *p_data;

}Evt_mesh_t;

typedef mdf_err_t (*Func_evt_handle)( Evt_mesh_t * );

typedef mdf_err_t (*Func_rtc_handle)( void *);


mdf_err_t mevt_send(Evt_mesh_t *p_mevt, uint32_t tm_to_wait);
void evt_loop_task(void *p_arg);

uint8_t mevt_handle(void);
void frtc_handle(void);

mdf_err_t frtc_function_register(F_RTC_CMD cmd, Func_evt_handle  func, void *p_arg );

mdf_err_t mevt_handle_func_register(Func_evt_handle f_handle, M_EVENT_CMD cmd);
void mevt_clean(Evt_mesh_t *p_evt);
mdf_err_t mevt_init(void);
mdf_err_t mevt_deinit(void);

mdf_err_t mevt_command_respond_creat(char *p_src_json, int status_code, char *p_data);

#endif

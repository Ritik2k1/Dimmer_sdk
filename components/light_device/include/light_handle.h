/*
 * @Author: your name
 * @Date: 2020-03-09 18:34:28
 * @LastEditTime: 2020-03-09 18:48:19
 * @LastEditors: Please set LastEditors
 * @Description: In User Settings Edit
 * @FilePath: \mqtt_example\components\light_device\include\light_device.h
 */

#ifndef _LIGHT_HANDLE_H
#define _LIGHT_HANDLE_H

#include <stdint.h>

#include "mdf_err.h"
#include "light_device.h"
/**
 * @brief The value of the cid corresponding to each attribute of the light
 */
typedef enum  {
    LIGHT_CID_POWER            = 0,
	LIGHT_CID_BRI,
	LIGHT_CID_FADE,
	LIGHT_CID_SHUTDOWN_TIME,
	
	
	LIGHT_CID_MAX
} LIGHT_CID_T;

mdf_err_t make_time_update_rq_2root(void);
mdf_err_t send_hue_2root(const char *p_url, int meth_index, const char *p_buff);
mdf_err_t send_node_press(const char *p_buff , const char *type,const char *p_url);
mdf_err_t light_device_init();
mdf_err_t mesh_data_send(uint8_t *dst_addr, const char* data,int len);
void light_online_device_update_set( uint64_t t_ms);
void light_online_update_loop(void);

#endif  // _LIGHT_HANDLE_H

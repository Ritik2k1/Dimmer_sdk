/*
 * @Author: sky
 * @Date: 2020-02-27 15:37:09
 * @LastEditTime: 2020-02-28 11:43:17
 * @LastEditors: Please set LastEditors
 * @Description: 什么 thincloud 相关的方法
 * @FilePath: \esp\mqtt_example\components\thincloud\include\mesh_thincloud.h
 */
#ifndef _MESH_THINCLOUD_H
#define _MESH_THINCLOUD_H
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
// #include "freertos/semphr.h"

#include <stdint.h>
#include "mdf_err.h"
#include "thincloud.h"
#include "mesh_dev_table.h"
#include "tc_event.h"
#include "aws_iot_mqtt_client_interface.h"

typedef enum {
    TC_UNSUBSCRIBED,
    TC_SUBSCRIBED
}TC_MQTT_CMD;
	

void tc_msg_destory(Tc_evt_msg_t *p_msg);
mdf_err_t tc_client_creat(void);
mdf_err_t tc_make_commission(Dev_tab_t *p_tab);
mdf_err_t tc_sub_device_command(  Dev_tab_t *p_tab);
mdf_err_t tc_unsub_dev(uint8_t *p_mac, uint8_t *p_devid);

void tc_msg_publish( uint8_t *p_deviceid, uint8_t* p_data);
void tc_dev_remove(uint8_t *mac_list, size_t subdev_num);
mdf_err_t tc_client_destory(void);
mdf_err_t tc_dev_add_subdev(TC_MQTT_CMD type, uint8_t *mac_list, size_t subdev_num);
mdf_err_t tc_send_publish(uint8_t *p_mac, uint8_t *p_devid, char *p_json);
mdf_err_t tc_subscrib_one_device(uint8_t *p_mac, uint8_t *p_devid);
mdf_err_t tc_send_comamnd_respond(uint8_t *p_devid, uint8_t *p_cid, char *p_json);
mdf_err_t tc_mac_commission(uint8_t *p_mac);

bool tc_is_connect(void);

#endif  // _MESH_THINCLOUD_H

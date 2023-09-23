/*
 * @Author: sky
 * @Date: 2020-03-09 18:34:28
 * @LastEditTime: 2021-02-18 14:27:56
 * @LastEditors: Please set LastEditors
 * @Description:  uart 相关
 * @FilePath: \mqtt_example\components\light_device\light_device_uart.c
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

#include "driver/gpio.h"

#include "event_queue.h"
#include "utlis.h"
#include "mesh_dev_table.h"
#include "light_device_config.h"
#include "light_device_uart.h"

#define UART_BUF_SIZE (512)

static const char *TAG          = "light_device_uart"; 

typedef struct UART_PROTOCOL_MSG_T{
  uint16_t head;//标头0x55aa
  uint8_t	ver;//版本
  uint8_t	cmd;//命令
  uint16_t data_len;//长度
  uint8_t dpid;
  uint8_t data_type;
  uint16_t feature_len;
  uint8_t	p_feature[0];//数据位 | 校验位
}Uart_msg_t;

uint8_t _sum(uint8_t* p_src, int len){
	uint8_t sum = 0;
	if(p_src && len > 0){
		int i;
		for(i=0; i< len; i++)
			sum += p_src[i]; 
	}
	return sum;
}

/**
*******/
mdf_err_t _uart_data_alloc(uint8_t        **pp_dst, int *p_dst_len, Uart_cmd cmd, void *p_data){

	uint8_t *p = NULL, *p_tmp = NULL;
	uint8_t buffer[126]={0}, len = 0;
	Uart_msg_t *p_msg = (Uart_msg_t*)buffer;
	

	MDF_PARAM_CHECK(pp_dst);
	MDF_PARAM_CHECK(p_dst_len);

	
	p_msg->head=0xaa55;
	p_msg->ver=0x00;
	p_msg->cmd=0x06;
	p_msg->data_len=0x0500;


	switch(cmd){
		
		case UART_CMD_BRI:
			p_msg->dpid = 0x03;
			p_msg->data_type = 0x01;
			p_msg->feature_len = 0x0500;
			p_msg->data_len=0x0900;
			
			memcpy(p_msg->p_feature, p_data, 5);
			//utlis_byte_printf("src data ", (uint8_t *)p_data, 5);
			//utlis_byte_printf("send data ", (uint8_t *)&p_msg->p_feature[0], 5);
			
			p_msg->p_feature[5] = _sum(buffer,  ( sizeof( Uart_msg_t ) + 6) );
			len = ( sizeof( Uart_msg_t ) + 5 ) + 1;
			break;
		case UART_CMD_FADE:
			
			p_msg->data_len=0x0800;
			p_msg->dpid = 0x66;
			p_msg->data_type = 0x02;
			p_msg->feature_len = 0x0400;
			
			memcpy(p_msg->p_feature, p_data, 4);
			p_msg->p_feature[4] = _sum(buffer,	( sizeof( Uart_msg_t ) + 5) );
			
			len = ( sizeof( Uart_msg_t ) + 4 ) + 1;
			break;
			
		case UART_CMD_STATUS:
			p_msg->dpid = 0x01;
			p_msg->data_type = 0x01;
			p_msg->feature_len = 0x0500;
			p_msg->data_len=0x0900;
			memcpy(p_msg->p_feature, p_data, 5);

			
			//utlis_byte_printf("src data ", (uint8_t *)p_data, 5);
			//utlis_byte_printf("send data ", (uint8_t *)&p_msg->p_feature[0], 5);

			p_msg->p_feature[5] = _sum(buffer,  ( sizeof( Uart_msg_t ) + 6) );
			len = ( sizeof( Uart_msg_t ) + 5 ) + 1;
			//p_msg->p_feature[1] = _sum(buffer,	( sizeof( Uart_msg_t ) + 2) );
			
			//len = ( sizeof( Uart_msg_t ) + 1 ) + 1;
			break;
		case UART_CMD_DIMMER:
			p_msg->dpid = 0x07;
			p_msg->data_type = 0x01;
			//sp_msg->feature_len = 0x0100;
			p_msg->feature_len = 0x0500;
			p_msg->data_len=0x0900;
			
			memcpy(p_msg->p_feature, p_data, 5);
			//p_msg->p_feature[0] = *( (uint8_t *)p_data );
			p_msg->p_feature[5] = _sum(buffer,	( sizeof( Uart_msg_t ) + 6) );
			
			len = ( sizeof( Uart_msg_t ) + 5 ) + 1;
			break;
		case UART_CMD_LIMIT_MAX:
			p_msg->dpid = 0x08;
			p_msg->data_type = 0x01;
			p_msg->feature_len = 0x0100;
			p_msg->p_feature[0] = *( (uint8_t *)p_data );
			p_msg->p_feature[1] = _sum(buffer,	( sizeof( Uart_msg_t ) +  2) );
			
			len = ( sizeof( Uart_msg_t ) + 1 ) + 1;
			break;
			
		case UART_CMD_LIMIT_MIM:
			p_msg->dpid = 0x09;
			p_msg->data_type = 0x01;
			p_msg->feature_len = 0x0100;
			p_msg->p_feature[0] = *( (uint8_t *)p_data );
			p_msg->p_feature[1] = _sum(buffer,	( sizeof( Uart_msg_t ) + 2) );
			
			len = ( sizeof( Uart_msg_t ) + 1 ) + 1;
			break;
		default:
			return MDF_FAIL;
			
	}
	
	//len = ( sizeof( Uart_msg_t ) + htons( p_msg->feature_len) ) + 1;
	if( len > 0)
		p = malloc_copy(buffer, len);
	
	if(p){
		*pp_dst = p;
		*p_dst_len = len;
		return MDF_OK;
	}else 
		return MDF_FAIL;
}

int uart_receive(const char **pp_recv){

	uint8_t *p_data = NULL;
	int rec_len = 0;
	static int old_rec_len =0;
	static int64_t old_rec_time = 0;
	int64_t ctime = utils_get_current_time_ms();
	ESP_ERROR_CHECK( uart_get_buffered_data_len(UART_NUM_1, (size_t*)&rec_len ) );
	
	
	if( rec_len >  0 && old_rec_len == rec_len && DIFF(ctime, old_rec_time) > 50 ){
			p_data = utlis_malloc( ( size_t ) ( rec_len + 1) );

			if(p_data){

				rec_len = uart_read_bytes(UART_NUM_1, p_data, (uint32_t) (rec_len + 1), 20 / portTICK_RATE_MS);

				//utlis_byte_printf("Uart receive : ", p_data, rec_len);
				*pp_recv = (char *)p_data;
			
			}else{

				rec_len = 0;
				MDF_LOGE("Error in read uart \n");
			}

			old_rec_len = 0;
			old_rec_time = 0;
			return rec_len;
	}else{
		
		if(rec_len >  0   && old_rec_len != rec_len){
			old_rec_time = ctime;
			old_rec_len = rec_len;
		}
	}

	return 0;
}
static mdf_err_t  _uart_event_send(Uart_cmd cmd, uint8_t *p_data, int len){

	Evt_mesh_t evt = {0};

	evt.cmd = EVT_UART_CMD;
	if(len > 0 && p_data){
		evt.data_len = (uint16_t)len + 1;
		evt.p_data =  utlis_malloc( len + 2 );
		MDF_ERROR_CHECK( NULL == evt.p_data, MDF_FAIL, "Failt to alloc\n");

		evt.p_data[0] = cmd;
		memcpy(&evt.p_data[1], p_data, len);

		return  mevt_send(&evt,  10/portTICK_RATE_MS);
	}

	return MDF_FAIL;
}
int uart_recv_handle(void){

	char *p_rec = NULL, *p_next = NULL;
	int rec_len = 0, remain_len = 0, feature_len = 0;
	Uart_msg_t *p_u = NULL;
	

	rec_len = uart_receive((const char **)&p_rec);
	
	if(rec_len == 0)
		return 0;
	
	p_next = p_rec;
	remain_len = rec_len;
	while( NULL != p_next && remain_len > sizeof(Uart_msg_t )){

		utlis_byte_printf("Uart remain ", (uint8_t *)p_next, remain_len);
		p_u = (Uart_msg_t *) p_next;

		//MDF_LOGD("version %us\n", p_u->head);
		if( p_u->head != 0xaa55)
			break;
		
		feature_len = p_u->feature_len>>8 | (0xff00 & p_u->feature_len<<8 );
		remain_len = remain_len - ( sizeof(Uart_msg_t ) + feature_len + 1);

		
		MDF_LOGD("remain_len   %d\n", remain_len );
		MDF_LOGD("feature len %#x trn len = %#x \n", p_u->feature_len, feature_len);
		//utlis_byte_printf("p_u ", (uint8_t *)p_u, ( sizeof(Uart_msg_t ) + feature_len + 1) );
		if( p_u->cmd == 0x07 ){
			switch(p_u->dpid){
				case 0x01:  // power 
					if( p_u->data_type == 0x01 && feature_len == 5){
						_uart_event_send(UART_CMD_STATUS,p_u->p_feature, feature_len );
						MDF_LOGE("recev power %u \n", p_u->p_feature[0]);
					}
					break;
				case 0x03:  // bri
					if(  feature_len > 0){
						_uart_event_send(UART_CMD_BRI,p_u->p_feature, feature_len );
						MDF_LOGE("recev bri %u \n", p_u->p_feature[0]);
					}
					break;
				case 0x66:  // fade
					if( feature_len == 4){
						uint32_t fade = 0;
						uint32_t *p_fade = &fade;
						// todo 
						p_fade[0] = p_u->p_feature[0];
						p_fade[1] = p_u->p_feature[1];
						p_fade[2] = p_u->p_feature[2];
						p_fade[3] = p_u->p_feature[3];

						// todo .
						//_uart_event_send(UART_CMD_FADE, p_fade, 4);
						MDF_LOGI("recev fade %u \n",   fade );
					}
					break;
				case 0x06:  // 3-ways
					if( feature_len == 4){
						_uart_event_send(UART_CMD_3WAY,p_u->p_feature, 1);
						MDF_LOGI("recev 3-ways %u \n", p_u->p_feature[0]);
					}
				}
		}

		if( remain_len <= 0){
				break;
			}else{
				p_next = p_next + sizeof(Uart_msg_t ) + feature_len + 1;
				if( NULL == p_next)
					break;
			}
	}
		
	MDF_FREE(p_rec);

	return 0;
}
int uart_send(const char *p_data, size_t data_len){

	MDF_ERROR_CHECK( NULL == p_data || data_len == 0, 0, "Inval data and size.\n");
	utlis_byte_printf("uart send ", (uint8_t *)p_data, (int)data_len);

	return	uart_write_bytes(UART_NUM_1,  p_data, data_len); 
}
int uart_cmd_send(Uart_cmd cmd, void *p_data){
	char *p_send = NULL;
	int send_len = 0, rc =0;

	MDF_PARAM_CHECK( cmd < UART_CMD_MAX );
	// todo fade..
	_uart_data_alloc((uint8_t **)&p_send, &send_len, cmd, p_data );
	if(p_send && send_len > 0){
		rc = uart_send(p_send, send_len);
		vTaskDelay( 30/ portTICK_RATE_MS );
	}
	MDF_FREE(p_send);
	p_send = NULL;

	return rc;
}

mdf_err_t _uart_init(void){

	
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
	uart_config_t uart_config = {
		.baud_rate = 9600,
		.data_bits = UART_DATA_8_BITS,
		.parity    = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE
	};
	uart_param_config( UART_NUM_1, &uart_config);
	uart_set_pin( UART_NUM_1, CNF_PIN_TXD, CNF_PIN_RXD, CNF_PIN_RTS, CNF_PIN_CTS);
	uart_driver_install( UART_NUM_1, UART_BUF_SIZE, UART_BUF_SIZE, 0, NULL, 0);	
	
	return ESP_OK;
}

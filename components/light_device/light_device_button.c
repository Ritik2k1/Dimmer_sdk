/*
 * @Author: sky
 * @Date: 2020-03-09 18:34:28
 * @LastEditTime: 2020-03-09 18:45:24
 * @LastEditors: Please set LastEditors
 * @Description: 设备信息，特性注册，接口提供
 * @FilePath: \mqtt_example\components\light_device\light_device.c
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"

#include <string.h>


#include "thincloud.h"
#include "event_queue.h"

#include "utlis.h"

#include "mesh_dev_table.h"
#include "light_device_config.h"
#include "light_device_button.h"
#include "light_device_uart.h"



#define _UP_TAP_PIN		CNF_TAP_UP_PIN
#define _DOWN_TAP_PIN 	CNF_TAP_DOWN_PIN
#define _LED_PIN 		CNF_LED_PIN
#define GPIO_TAP_PIN_SEL  ((1ULL<<_UP_TAP_PIN) | (1ULL<<_DOWN_TAP_PIN))
#define _OUTPUT_PIN_SEL  (1ULL<<_LED_PIN) 
#define _RESET_PIN     CNF_RSET_PIN
//#define GPIO_INPUT_IO_1     5
//#define GPIO_INPUT_PIN_SEL  ((1ULL<<_RESET_PIN) | (1ULL<<GPIO_INPUT_IO_1))
#define GPIO_INPUT_PIN_SEL  ( 1ULL<<_RESET_PIN )
#define _KEYS_PRESS_STATUS CNF_KEYS_PRESS_STATUS


static const char *TAG          = "light_device_button"; 
typedef struct __LED_CTL_T{
	uint8_t old_status;
	uint8_t pin;
	uint16_t period;
	int64_t status_star_time;
}_LED_CTL_T;

typedef struct HARD_CTL_T{
	uint8_t p_uart_buff[128];
	xQueueHandle gpio_evt_in_queue;		
	
}Hard_ctl_t;

typedef struct __KEYS_EVENT_T{
	uint8_t pin;
	uint8_t old_status;
	uint8_t event_index;
	uint8_t recv_press;
	uint8_t hold_event;
	
	uint16_t inv_tm; 
	uint32_t start_time;
	int event_record[4];
}_Keys_event_t;

#define _ONECLICK	(50)

typedef enum _KEY_EVENT_T{
	KEYS_EVENT_NONE = 0,
	KEYS_EVNET_CLICK,
	KEYS_EVNET_DOUBLE_CLICK,
	KEYS_EVENT_CTYPE_DIMM,
	KEYS_EVENT_CTYPE_NONDIMM,
	KEYS_EVENT_CTYPE_SMART,
	KEYS_EVENT_CTYPE_REMOT,
	KEYS_EVNET_REST_WIFI,
	KEYS_EVNET_REST_SYS,
	KEYS_EVNET_START_HOLD,
	KEYS_EVNET_START_HOLD_DOUBLE,
	KEYS_EVNET_STOP_HOLD,
	
	KEYS_EVENT_MAX,

}KEY_EVENT_T;

const int keys_event_format[KEYS_EVENT_MAX][4] = {
	{0, 0, 0,0},// hold
	{_ONECLICK, 0, 0,0}, //KEYS_EVNET_CLICK
	{_ONECLICK, 1, 0,0}, //KEYS_EVNET_CLICK
	{4500, _ONECLICK, 0,0},
	{4500, _ONECLICK, 1,0}, // hold 5s, click one KEYS_EVENT_CTYPE_DIMM
	{4500, _ONECLICK, 2,0}, // 
	{4500, _ONECLICK, 3,0},//单击
	{9500, _ONECLICK, 2,0}, // KEYS_EVNET_REST_WIFI
	{9500, _ONECLICK, 4,0}, //KEYS_EVNET_REST_SYS
	{250, 0, 0,0},  // KEYS_EVNET_HOLD
	{0, 0, 0,0}
	
};
	

_LED_CTL_T _led;
_Keys_event_t _up_key, _down_key;

static Hard_ctl_t hctl;
static TimerHandle_t task_key_timer = NULL;

static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    uint32_t gpio_num = (uint32_t) arg;
    xQueueSendFromISR(hctl.gpio_evt_in_queue, &gpio_num, NULL);
}

/**
* 设置 led 灯的状态
* status < 0 , 则调用时不立即改变当前 led 的状态。
* period  led 变化的周期
***/
void led_action_set(int status, uint16_t period ){
	_led.period = period;
	if(status >= 0 ){
		_led.old_status = status == 0?0:1;
		gpio_set_level( _led.pin, _led.old_status);
	}
}
extern void light_led_indicator(void);
static void _led_indicator_callbck(TimerHandle_t xTimer){
	// let led fllow to the bri
	light_led_indicator();
}

static void _led_loop_ctl(_LED_CTL_T *p_led, int64_t ctime){
	if(p_led->period > 0 && DIFF(ctime, p_led->status_star_time ) > p_led->period){
		p_led->old_status = (p_led->old_status == 0) ? 1:0;
		gpio_set_level(p_led->pin, p_led->old_status);
		p_led->status_star_time = ctime;
	}
}
static void led_timer_handle(void *timer){
	int64_t ctime = utils_get_current_time_ms();
	_led_loop_ctl(&_led, ctime);

}
int led_indicator_frequency_set(int led_freq, int period)
{
    if( xTimerChangePeriod( task_key_timer, 500 / portTICK_PERIOD_MS, 100 ) == pdPASS )
    {
		led_action_set(-1, led_freq);
		// The command was successfully sent.
        xTimerStart(task_key_timer, period);
		return 0;
	}
	return -1;
}

static void _led_init(uint8_t pin, uint8_t status){

	gpio_config_t out_put_conf;
	
	_led.pin = pin;
	_led.old_status = status;

	    //disable interrupt
    out_put_conf.intr_type = GPIO_PIN_INTR_DISABLE;
    //set as output mode
    out_put_conf.mode = GPIO_MODE_OUTPUT;
    //bit mask of the pins that you want to set,e.g.GPIO18/19
    out_put_conf.pin_bit_mask = _OUTPUT_PIN_SEL;
    //disable pull-down mode
    out_put_conf.pull_down_en = 0;
    //disable pull-up mode
    out_put_conf.pull_up_en = 0;
    //configure GPIO with the given settings
    gpio_config(&out_put_conf);
	gpio_set_level( _led.pin, _led.old_status);

}

static KEY_EVENT_T _keys_start_hold_event_creat(_Keys_event_t *p_key_press, uint32_t current_tm){
	if( p_key_press->hold_event == 0 && p_key_press->recv_press == 0  
		&& gpio_get_level( p_key_press->pin ) == _KEYS_PRESS_STATUS 
		&&  DIFF(current_tm, p_key_press->start_time) >= 250 ){
		p_key_press->hold_event = 1;
		return KEYS_EVNET_START_HOLD;
	}else return KEYS_EVENT_NONE;
}

static KEY_EVENT_T _keys_start_hold_double_event_creat(_Keys_event_t *p_key_press, uint32_t current_tm){
	if( p_key_press->hold_event == 0 && p_key_press->recv_press == 1  
		&& gpio_get_level( p_key_press->pin ) == _KEYS_PRESS_STATUS 
		&&  DIFF(current_tm, p_key_press->start_time) >= 250 ){
		p_key_press->hold_event = 1;
		return KEYS_EVNET_START_HOLD_DOUBLE;
	}else return KEYS_EVENT_NONE;
}

static KEY_EVENT_T _keys_stop_hold_event_creat(_Keys_event_t *p_key_press, uint32_t current_tm){
	if(p_key_press->hold_event == 1 && p_key_press->recv_press == 1 ){
		p_key_press->hold_event = 0;
		return KEYS_EVNET_STOP_HOLD;
	}else
		return KEYS_EVENT_NONE;
}

static void _keys_subevent_creat(_Keys_event_t *p_key_press, _Keys_event_t *p_key_other,uint8_t level, uint32_t current_tm){
	// get press time.
	// push sub event 
	//MDF_LOGW("keys_subevent_creat current time: %d \n", (uint32_t) current_tm);
	if( level == _KEYS_PRESS_STATUS && p_key_press->old_status != level){
		p_key_press->start_time = current_tm;
		
		//MDF_LOGW("start_time %d", (uint32_t) p_key_press->start_time);
		p_key_press->recv_press = 0;
	}else if( level != _KEYS_PRESS_STATUS  && p_key_press->old_status != level){

		int press_tm = 0;
		uint32_t interv_tm = DIFF(current_tm,  p_key_press->start_time);

		//MDF_LOGW(" %d - %d  inv time:: %d \n", current_tm, p_key_press->start_time, interv_tm);
		//led_action_set(0, 0);

		p_key_press->start_time = current_tm;
		p_key_press->recv_press = 1; 

		if( interv_tm > _ONECLICK && interv_tm < 1000 ){ // one click
			press_tm = _ONECLICK;
			p_key_press->inv_tm = 400;
			MDF_LOGD("--> short press\n");
		}else if( interv_tm > 4000 && 6000 > interv_tm   \
			&& ( p_key_other->event_record[0] == 4500 || gpio_get_level( p_key_other->pin ) == _KEYS_PRESS_STATUS )){ // hold 5s			
			press_tm = 4500;
			p_key_press->inv_tm = 400;//1500;
			MDF_LOGD("--> press 5s\n");

		}else if( interv_tm > 7000 && 12000 > interv_tm  \
			&& ( p_key_other->event_record[0] == 9500 || gpio_get_level(p_key_other->pin) == _KEYS_PRESS_STATUS )){ // hold  10s 
			press_tm = 9500;
			p_key_press->inv_tm = 1500;
			MDF_LOGD("--> press 10\n");
		}
		if(press_tm){
			if( p_key_press->event_index > 0 && press_tm == p_key_press->event_record[ p_key_press->event_index -1]){
				p_key_press->event_record[ p_key_press->event_index ] = p_key_press->event_record[ p_key_press->event_index ] + 1;
				MDF_LOGD("double \n");
				MDF_LOGD("press index %u event %d \n", p_key_press->event_index, p_key_press->event_record[ p_key_press->event_index ]);
			
				goto _subevent_creat_t;
			}

			if( press_tm > 4000 )
				p_key_press->event_index = 0;
			
			p_key_press->event_record[ p_key_press->event_index] = press_tm;
			if( ( 1 + p_key_press->event_index ) >= 4 ){
				// todo creat event;
				;
			}else{
				p_key_press->event_index++;
			}
		}
	}
	
_subevent_creat_t:
	p_key_press->old_status = level;
}

static KEY_EVENT_T _keys_loop_event_creat(_Keys_event_t *p_key, uint32_t current_tm){
	int i =0;
	KEY_EVENT_T event = KEYS_EVENT_NONE; 
	
	KEY_EVENT_T start = 0, stop = 0;

	start = _keys_start_hold_event_creat(p_key, current_tm);
	stop  = _keys_stop_hold_event_creat(p_key, current_tm);
	event = (start > 0 )?start:stop;
		
	if( event > 0 ){
		return event;
	}

	//if(p_key->recv_press)
	//	MDF_LOGE("ctime %d \n", current_tm);
	// up tap 
	if( p_key->recv_press && DIFF(current_tm, p_key->start_time) > p_key->inv_tm){
		
		//MDF_LOGE("ctime %d \n", current_tm);
		for(i=0; i<KEYS_EVENT_MAX; i++ ){
			if( !memcmp((char*)keys_event_format[i], (char*)p_key->event_record, 4 * sizeof(int) ) ){
				event = i;
				break;
			}
		}
		// clear 
		p_key->recv_press = 0;
		p_key->event_index = 0;
		p_key->start_time = 0;
		p_key->inv_tm = 400;
		bzero((char*)p_key->event_record , sizeof(int) * 4);
		light_led_indicator();
	}

	return event;
}
static void _keys_double_press_indicator(uint32_t current_tm){
	
	if( _up_key.start_time != 0 && _down_key.start_time != 0 \
		&& _KEYS_PRESS_STATUS == gpio_get_level(_up_key.pin) && _KEYS_PRESS_STATUS == gpio_get_level(_down_key.pin) ){
		if( DIFF(current_tm,  _up_key.start_time)  > 4000 &&  6000 >  DIFF(current_tm,  _up_key.start_time) \
			&& DIFF(current_tm,  _down_key.start_time)  > 4000 &&  6000 >  DIFF(current_tm,  _down_key.start_time) ){
			led_action_set(-1, 200);
			
		}else if( DIFF(current_tm,  _up_key.start_time)  > 5500 && 7000 > DIFF(current_tm,  _up_key.start_time) \
			 && DIFF(current_tm,  _down_key.start_time)  > 5500 && 7000 > DIFF(current_tm,  _down_key.start_time)){
			led_action_set(0, 0);
			
		}else if( DIFF(current_tm,  _up_key.start_time)  > 7000 &&  12000 >  DIFF(current_tm,  _up_key.start_time) \
				&& DIFF(current_tm,  _down_key.start_time) > 7000 &&  12000 >  DIFF(current_tm,  _down_key.start_time)  ){
			led_action_set(-1, 200);
		}

	}

}
static void _keys_event_loop(uint32_t current_tm){

	uint32_t pin = 0, val = 0;

	if( !hctl.gpio_evt_in_queue)
		return ;
	
	if( xQueueReceive(hctl.gpio_evt_in_queue, &pin,  1/ portTICK_RATE_MS)) {
		val = gpio_get_level(pin);
		MDF_LOGD("GPIO[%d] intr, val: %d\n", pin, val);
		if( pin == _up_key.pin){
			_keys_subevent_creat(&_up_key, &_down_key, val, current_tm);
		} if( pin == _down_key.pin){
			_keys_subevent_creat(&_down_key, &_up_key, val, current_tm);
		}
	}
}
static  mdf_err_t keys_event_send(M_EVENT_CMD cmd, uint8_t *p_data, int data_len){
	Evt_mesh_t evt = {0};

	evt.cmd = cmd;
	if( p_data && data_len > 0){
		evt.p_data = malloc_copy(p_data, data_len);
		if(evt.p_data){
			evt.data_len = data_len;
		}
	}
	return mevt_send(&evt,  1/portTICK_RATE_MS);
}
static void keys_evet_excet(uint8_t pin, KEY_EVENT_T event){

	uint8_t type =0;
	uint8_t  *p_data = NULL;
	int data_len = 0;

	uint32_t ct = (uint32_t) utils_get_current_time_ms();
	//MDF_LOGE("ctime %d Button key event %d\n",  ct,  event);
	switch(event){
		case KEYS_EVNET_CLICK:
			if(pin == _UP_TAP_PIN){
				keys_event_send(EVT_BUTTON_TU, NULL, 0);
			}else
				keys_event_send(EVT_BUTTON_TD, NULL, 0);
			break;
		case KEYS_EVNET_DOUBLE_CLICK:
			if(pin == _UP_TAP_PIN){
				keys_event_send(EVT_BUTTON_DTU, NULL, 0);
			}else{
				keys_event_send(EVT_BUTTON_DTD, NULL, 0);
			}
			break;
		case KEYS_EVENT_CTYPE_DIMM:
			if(pin == _UP_TAP_PIN){
				type = DEVTYPE_Dimmable;
				keys_event_send(EVT_SYS_TYPE_SET, &type, 1);
			}
			break;
		case KEYS_EVENT_CTYPE_NONDIMM:
			
			if(pin == _UP_TAP_PIN){
				type = DEVTYPE_NonDimmable;
				keys_event_send(EVT_SYS_TYPE_SET, &type, 1);
				}
			break;
		case KEYS_EVENT_CTYPE_SMART:
			
			if(pin == _UP_TAP_PIN){
				type = DEVTYPE_Smart;
				keys_event_send(EVT_SYS_TYPE_SET, &type, 1);
			}
			break;
		case KEYS_EVENT_CTYPE_REMOT:
			
			if(pin == _UP_TAP_PIN){
				type = DEVTYPE_Remote;
				keys_event_send(EVT_SYS_TYPE_SET, &type, 1);
				}
			break;
		case KEYS_EVNET_REST_WIFI:
			keys_event_send(EVT_SYS_WIFI_RESET, NULL, 0);
			break;
		case KEYS_EVNET_REST_SYS:
			keys_event_send(EVT_SYS_FACTORY_REST, NULL, 0);
			break;
		case KEYS_EVNET_START_HOLD:
			{
				uint8_t u_data = 0;
				if( pin == _UP_TAP_PIN ){
					u_data = HOLD_INC_UP_START;
				keys_event_send(EVT_BUTTON_HOLD_START_UP, &u_data, 1);
				}else{
					u_data = HOLD_INC_DOWN_START;
				keys_event_send(EVT_BUTTON_HOLD_START_DOWN, &u_data, 1);
				}
				//keys_event_send(EVT_BUTTON_HOLD_START, &u_data, 1);
				//_uart_data_alloc(&p_data, &data_len, UART_CMD_DIMMER,(void *)&u_data );
				//if(p_data && data_len > 0 ){
				//	keys_event_send(EVT_BUTTON_HOLD_START, p_data, data_len);
				//}
			}
			break;
		case KEYS_EVNET_START_HOLD_DOUBLE:
			{
				uint8_t u_data = 0;
				if( pin == _UP_TAP_PIN ){
					u_data = HOLD_INC_UP_START_DOUBLE;
				keys_event_send(EVT_BUTTON_HOLD_START_DUP, &u_data, 1);
				}else{
					u_data = HOLD_INC_DOWN_START_DOUBLE;
				keys_event_send(EVT_BUTTON_HOLD_START_DDOWN, &u_data, 1);
				}
				//keys_event_send(EVT_BUTTON_HOLD_START, &u_data, 1);
				//_uart_data_alloc(&p_data, &data_len, UART_CMD_DIMMER,(void *)&u_data );
				//if(p_data && data_len > 0 ){
				//	keys_event_send(EVT_BUTTON_HOLD_START, p_data, data_len);
				//}
			}
			break;
		case KEYS_EVNET_STOP_HOLD:
			{
				uint8_t u_data = 0;

				if( pin == _UP_TAP_PIN ){
					
					u_data = HOLD_INC_UP_STOP;
				}else
					u_data = HOLD_INC_DOWN_STOP;
				
				keys_event_send(EVT_BUTTON_HOLD_STOP, &u_data, 1);
				//_uart_data_alloc(&p_data, &data_len, UART_CMD_DIMMER,(void *)&u_data );
				//if(p_data && data_len > 0 ){
				//	keys_event_send( EVT_BUTTON_HOLD_STOP, p_data, data_len);
				//}
			}
			break;
		default:
			break;
	}
	
	MDF_FREE(p_data);
}

static void _keys_loop(int64_t ctime){
	KEY_EVENT_T event = 0;
	uint32_t current_tm =  (uint32_t)ctime;
	_keys_event_loop(current_tm);
	
	event = _keys_loop_event_creat(&_up_key,current_tm);

	if(event){
		MDF_LOGI("up keys event:: %d\n", event);
		if(event > KEYS_EVNET_DOUBLE_CLICK)
			led_indicator_frequency_set(200, 1000);
		keys_evet_excet(_up_key.pin, event);
	}
	event = _keys_loop_event_creat(&_down_key,current_tm);
	if(event){
		MDF_LOGI("down keys event:: %d\n", event);
		keys_evet_excet(_down_key.pin, event);
	}
	_keys_double_press_indicator(current_tm);
}

static void _keys_init(uint8_t up_pin, uint8_t down_pin){
	gpio_config_t io_conf;
	//interrupt of both edge
	io_conf.intr_type = GPIO_PIN_INTR_ANYEDGE;
	//bit mask of the pins, use GPIO4/5 here
	io_conf.pin_bit_mask = GPIO_TAP_PIN_SEL;
	//set as input mode
	io_conf.mode = GPIO_MODE_INPUT;
	//enable pull-up mode
	io_conf.pull_up_en = 1;
	gpio_config(&io_conf);

	//change gpio intrrupt type for one pin
	gpio_set_intr_type(up_pin, GPIO_INTR_ANYEDGE);
	gpio_set_intr_type(down_pin, GPIO_INTR_ANYEDGE);

	//create a queue to handle gpio event from isr
	hctl.gpio_evt_in_queue = xQueueCreate(10, sizeof(uint32_t));
	//start gpio task

	//install gpio isr service
	//gpio_install_isr_service(ESP_INTR_FLAG_HIGH);
	gpio_install_isr_service(0);
	//hook isr handler for specific gpio pin
	gpio_isr_handler_add(up_pin, gpio_isr_handler, (void*) up_pin);
	
	gpio_isr_handler_add(down_pin, gpio_isr_handler, (void*) down_pin);

	_up_key.pin = up_pin;
	_down_key.pin = down_pin;
	
	_up_key.inv_tm = 400;
	_down_key.inv_tm = 400;

	_up_key.old_status = ( _KEYS_PRESS_STATUS == 0)?1:0;
	_down_key.old_status = ( _KEYS_PRESS_STATUS == 0)?1:0;

	//task_key_timer = xTimerCreate(    "key_led_indicator", ( 1000 / portTICK_PERIOD_MS  ),pdFALSE, ( void * ) 10, _led_indicator_callbck );

#if 1
	task_key_timer = xTimerCreate( "key_led_indicator",       // Just a text name, not used by the kernel.
                                  ( 1000 / portTICK_PERIOD_MS  ),   // The timer period in ticks.
                                  pdFALSE,        // The timers will auto-reload themselves when they expire.
                                  ( void * ) 10,  // Assign each timer a unique id equal to its array index.
                                  _led_indicator_callbck // Each timer calls the same callback when it expires.
                                  );

#endif
}

void button_loop(int64_t ctime){
	
	_keys_loop(ctime);
	//_led_loop_ctl(&_led, ctime);
}
void button_init(void){
	
    esp_log_level_set(TAG, ESP_LOG_WARN);
	_led_init(_LED_PIN, 0);
	led_action_set(1, 0);
	_keys_init(_UP_TAP_PIN, _DOWN_TAP_PIN);
	
	TimerHandle_t timer_led = xTimerCreate("led timer", 100 / portTICK_RATE_MS,
                                       true, NULL, led_timer_handle);
    xTimerStart(timer_led, 0);
}


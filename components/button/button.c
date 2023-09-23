/*
 * @Author: sky
 * @Date: 2020-02-28 15:22:51
 * @LastEditTime: 2020-02-28 15:23:53
 * @LastEditors: Please set LastEditors
 * @Description: In User Settings Edit
 * @FilePath: \mqtt_example\components\utilis\utlis.c
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "utlis.h"
#include "driver/gpio.h"
#include "button.h"

// todo need config 
#define TEST_KEY	(GPIO_NUM_2)
static const char *TAG                   = "Button";
static  xQueueHandle gpio_evt_in_queue = NULL;	

static void IRAM_ATTR _button_isr_handler(void* arg)
{
    uint32_t gpio_num = (uint32_t) arg;

	CHECK_ERR_NO_RETURN(gpio_evt_in_queue);
    xQueueSendFromISR(gpio_evt_in_queue, &gpio_num, NULL);
	
}

mdf_err_t  test_button_init(void){
	gpio_config_t io_conf;
	//interrupt of both edge
	io_conf.intr_type = GPIO_PIN_INTR_ANYEDGE;
	//bit mask of the pins, use GPIO4/5 here
	io_conf.pin_bit_mask = (1ULL<<TEST_KEY) ;
	//set as input mode
	io_conf.mode = GPIO_MODE_INPUT;
	//enable pull-up mode
	io_conf.pull_up_en = 1;
	gpio_config(&io_conf);

	//change gpio intrrupt type for one pin
	
	gpio_set_intr_type(TEST_KEY, GPIO_INTR_ANYEDGE);

	//create a queue to handle gpio event from isr
	if( !gpio_evt_in_queue)
		gpio_evt_in_queue = xQueueCreate(10, sizeof(uint32_t));
	//start gpio task

	//install gpio isr service
	gpio_install_isr_service(0);
	//hook isr handler for specific gpio pin
	gpio_isr_handler_add(TEST_KEY, _button_isr_handler, (void*) TEST_KEY);

	return MDF_OK;
}

int button_press(void){

	uint32_t pin = 0, val = 0;

	MDF_PARAM_CHECK( gpio_evt_in_queue);
	if( xQueueReceive(gpio_evt_in_queue, &pin,  3 / portTICK_RATE_MS)) {
		val = gpio_get_level(pin);
		MDF_LOGI("GPIO[%d] intr, val: %d\n", pin, val);
		if( pin == TEST_KEY && val == 1){
			return 1;
		}
	}
	
	return 0;
}


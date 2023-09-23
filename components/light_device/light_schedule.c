/*
 * @Author: sky
 * @Date: 2020-03-09 18:34:28
 * @LastEditTime: 2021-03-03 16:23:04
 * @LastEditors: Please set LastEditors
 * @Description: ota
 * @FilePath: \mqtt_example\components\light_device\_LIGHT_SCHEDULE_H.c
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include <string.h>
#include <stdlib.h>
#include "cJSON.h"

#include "mwifi.h"
#include "mupgrade.h"
#include "mlink.h"

#include "utlis.h"
#include "event_queue.h"
#include "hue.h"
#include "light_schedule.h"
#include "light_device.h"
#include "light_device_config.h"
#include "sunriset.h"

#define _STORE_LAT_KEY	"f_Lat_info"
#define _STORE_LON_KEY	"f_Lon_info"

#include "esp_heap_trace.h"


#define _BYTE_SECOND	(10000000)
#define _BYTE_FUNCTION	(100)
#define _WEEK_TIME_GET(t, zone)	( ( t + (zone * 3600)) % 86400 )

#ifndef DIFF
#define DIFF(a,b)  (( (a) > (b) )?( (a) - (b) ):( (b)- (a) ))
#endif

/***********************************************************************************************
** tap, arm 在 flash 中储存的结构，数据的存储使用esp32 中的 key-value 进行保存和寻找。
** "schedule_nums"  中保存了当前设备有多少个 tap, alarm 以及最近一次的读取位置
**					结构如下：
**				typedef struct	{
			            Sch_had_t  p_had[ _SCH_CMD_MAX ];
			            uint8_t read_position[ _SCH_CMD_MAX ];
			        }_SCH_CTL_T;
**
** "TC_TAP" 保存了当前所有 tap 信息的索引列表
		 typedef struct SCH_LIST_T{
            char p_sub_key[4];	// alarm key  默认是 Ala, tap 默认是 Tap
            Item_t p_list[];
                typedef struct ITEM_T{
                    uint32_t idx;
                    uint64_t sch_time_key;  // 索引
                        "sch_time_key"
                            {data}
                }Item_t;
        }Sch_List_t;
	
**  "TC_ALARM" 保存了当前所有 alarm 信息的索引列表
		typedef struct SCH_LIST_T{
			   char p_sub_key[4];  // alarm key  默认是 Ala, tap 默认是 Tap
			   Item_t p_list[];
				   typedef struct ITEM_T{
					   uint32_t idx;
					   uint64_t sch_time_key;  // 索引
						   "sch_time_key"
							   {data}
				   }Item_t;
		   }Sch_List_t;
**	内容： "sch_time_key" 保存了该项的详细内容。
**********************************/
#define _PACKAGE_MAX_LEN	(1024)
static const char *TAG          = "schedule"; 
typedef struct SCHEDULE_HAD_T{
	uint64_t time_id; // 0123456786400 =  1234567(星期) + 86400(日内时间) + 01 (00 代表 alarm)，11(tu) 12(td)13(dtu) ..

	uint8_t bri;
	uint8_t self;

	uint32_t fade;
	uint32_t countdown_tm;
	
	uint8_t p_id[16]; 
	char SwitchDeviceId[50];
	char PlugDeviceId[50];
	char GroupId[50];
	char HueURL[50];
	char HueScene[50];
	bool UseThisBrightness; 
	char srss[50];
}Schedule_had_t;


typedef struct SCHEDULE_T{

	Schedule_had_t had;
	uint16_t data_len;
	uint8_t *p_data;
}Sch_t;

typedef struct SCHEDULE_FILE_T{

	Schedule_had_t had;
	uint16_t data_len;
	uint8_t p_data[];
	//char *p_data; // 
}Sch_file_t;
/*****
******
** 无论是 alarm 还是 tap，都有类似的结构，其实子结构也就那点东西，bri,fade,cuttime,以及控制其它灯或者是触发场景.
** 但是 tab 和 alarm 又有一些自身的结构，所以这里劈开两部分储存一部分是共性的 Sch_file_t 结构，另一部分是 tab 或者
** Alarm 自有特殊部分 Alarm_sub_t， Tap_sub_t。
** alarm list 入口key 为 TC_ALARM; alarm 子结构 Sch_file_t 的key 为 00_time, Item_sub_t 的key 为 Ala_index
** tab list 入口 key 为 TC_tabp; tab 子结构 Sch_file_t 的key 为 01_time "TU",11_time "TD",21_time "DTU",31_time "DTD"...
********
********/
/***
** alarm 总的入口为 Alarm_list_t 结构
** 每一个 alarm 结构为 子项 sub 的 key 的index， 该index和关键字母 p_sub_key 结合可以找到Alarm_sub_t 的 value。
** 每个 alarm 由两部分构成： Sch_file_t 储存 schedule 详细的信息，Alarm_sub_t 储存额外的信息，如名称等。
******/
typedef enum{
	_SCH_TYPE_ALARM = 0,
	_SCH_TYPE_TAP = 10,

	_SCH_TYPE_MAX
}_SCH_TYPE_T;

typedef struct _SCH_SETTING_T{
	uint8_t p_power[_SCH_TAP_MAX];
	uint8_t p_bri[_SCH_TAP_MAX];
	uint8_t p_dimmer[_SCH_TAP_MAX];

	uint32_t p_fade[_SCH_TAP_MAX];
	uint32_t cutdown;
}_sch_setting_t;


const char *_p_tap_type[_SCH_TAP_MAX] = {"TU","TD","DTU","DTD", NULL};

typedef struct ITEM_T{
	uint32_t idx;
	//uint8_t sub_item;
	uint64_t sch_time_key;
}Item_t;

// sub 储存 alarm 的 name.
typedef struct ITEM_SUB_T{
	uint8_t name_len;
	char p_name[];
}Item_sub_t;

typedef struct TAPSCH_SUB_T{
	uint8_t name_len;
	char p_name[];
}Tapsch_sub_t;

typedef struct TAP_SUB_T{
	uint8_t name_len;
	uint64_t tap_items[8]; //  可以删掉.
	char p_name[];
}Tap_sub_t;

typedef struct SCH_LIST_T{
	char p_sub_key[4];	// alarm key  默认是 Ala, tap 默认是 Tap
	Item_t p_list[];
}Sch_List_t;
#if 0
 typedef enum {
	_SCH_CMD_ALAM,
	_SCH_CMD_TAP,
	
	_SCH_CMD_MAX
}_Sch_type_t;
#endif
const char *_p_key_prifx[_SCH_CMD_MAX] = {
	"A",
	"T",
	NULL
};
const char *_p_sch_key[_SCH_CMD_MAX] = {
	"TC_ALARM",
	"TC_TAP",
	NULL
};
typedef struct SCH_HAD_T{
	uint8_t nums;
}Sch_had_t;
typedef struct	{
	
	Sch_had_t  p_had[ _SCH_CMD_MAX ];
	uint8_t read_position[ _SCH_CMD_MAX ];
}_SCH_CTL_T;

#define HMS2UNINT(str,num)  do{ \
	int a=0,b=0,c=0;\
	sscanf(str,"%d:%d:%d",&a,&b,&c);\
	num = a*3600+b*60+c;\
	}while(0)
#define HMS2STRING(p_str, wtime) do{ int h = wtime / 3600, m= (wtime % 3600)/60, s= (wtime % 3600) % 60;\
										sprintf(p_str, "%02d:%02d:%02d", h,m,s);}while(0)

static uint64_t current_id[_SCH_CMD_MAX] = {0}, next_id[_SCH_CMD_MAX] = {0}, next_secend[_SCH_CMD_MAX] = {0};
static uint64_t update_tm[_SCH_CMD_MAX] = {0};

static Sch_file_t *p_sch_tap[_SCH_TAP_MAX] = {0};

char *_w_day[7]={"Mon","Tue","Wed","Thu","Fri","Sat","Sun"};

//Sch_had_t schd_had[ _SCH_CMD_MAX ];

_SCH_CTL_T _ctl = {0};

#define _SCH_FILE_LEN(p_sch)	( sizeof(Sch_file_t) + p_sch->data_len )

extern mdf_err_t event_device_info_update(void);
static mdf_err_t _tap_sub_get(char **pp_tap, int *p_tap_len, uint64_t key_id, char **pp_error);
static mdf_err_t _tapsch_sub_save(uint64_t id, char *p_json, char **pp_error);
static mdf_err_t _tapsch_get_array_id(uint64_t **pp_id, int *p_len, char *p_src);
static mdf_err_t _tapsch_del(_Sch_type_t type, int del_all, char **pp_respond , uint64_t *p_del_id, int del_len, char **pp_error);
static void _sch_update(_Sch_type_t type);
static mdf_err_t _sch_loop_detect(void *p_arg);
static void _sch_tap_setting_update(uint64_t tid);

/**
** printf a file
**/
void _sch_file_printf(Sch_file_t *p_sch){
	MDF_LOGD("B==================_sch_file_printf=================\n");
	
	// printf time
	MDF_LOGD("time id %llu\n", p_sch->had.time_id);
	
	// bri
	MDF_LOGD("bri %d\n", p_sch->had.bri);

	// fade
	MDF_LOGD("fade %d\n", p_sch->had.fade);
	
	// self
	MDF_LOGD("self : %d\n", p_sch->had.self);
	
	// countdown_time
	MDF_LOGD("countdown time %d\n", p_sch->had.countdown_tm);
	
	MDF_LOGD("SwitchDeviceId: %s\n", p_sch->had.SwitchDeviceId);
	MDF_LOGD("PlugDeviceId: %s\n", p_sch->had.PlugDeviceId);
	MDF_LOGD("GroupId: %s\n", p_sch->had.GroupId);
	MDF_LOGD("UseThisBrightness: %d\n", p_sch->had.UseThisBrightness);
	MDF_LOGD("HueURL: %s\n", p_sch->had.HueURL);
	MDF_LOGD("HueScene: %s\n", p_sch->had.HueScene);
	MDF_LOGD("srss: %s\n", p_sch->had.srss);

	if(p_sch->p_data){
		MDF_LOGD("Additional len = %d : %s   \n", p_sch->data_len, p_sch->p_data);
	}
	MDF_LOGD("E==================_sch_file_printf=================\n");
}
static Sch_file_t *_sch_file_read_with_id(uint64_t id, int *p_len){
	Sch_file_t *p_sch = NULL;
	char p_key[32] = {0};

	MDF_ERROR_GOTO( (0 == id), End, "sch id must not be zero\n");
	sprintf(p_key, "%llu", id);
	
	MDF_LOGD("Get key %s \n", p_key);

	//p_sch = utlis_info_load(p_key, p_len);
	utlis_store_blob_get(US_SPA_SCH, p_key, (void **)&p_sch, (size_t *)p_len );

	if(p_sch){
		MDF_LOGD("Successfully read key %s get %d byte \n", p_key, *p_len);
	}
End:
	
	return p_sch;
}
mdf_err_t _sch_json2time_id(uint64_t *p_tm_index, const char *p_src){

	mdf_err_t rc = MDF_FAIL;
	uint32_t start_time = 0;
	uint64_t tm_index = 0;
	int i=0, j = 0;
	uint8_t nums = 0;
	char *p_start_tm = NULL, *p_week[7] = {NULL}, *p_start_time = NULL;

	//MDF_LOGD("src data: %s\n", p_src);
	// get start time.
	mlink_json_parse(p_src, "StartTime", &p_start_tm);
	MDF_ERROR_CHECK( NULL == p_start_tm,  MDF_FAIL, "Fait to get start time\n");

	// get week.
	rc = mlink_json_parse(p_src, "Days", &nums);
	MDF_ERROR_GOTO(MDF_OK != rc, End , "Fait to get days\n");
	// combination.

	rc = mlink_json_parse(p_src, "Days", p_week);
	MDF_ERROR_GOTO(MDF_OK != rc, End , "Fait to get days\n");
	for(i=0; i< nums; i++){
		for(j=0;j<7;j++){
			if(!strcasecmp(p_week[i], _w_day[j]))
			{
				tm_index += (j == 0)?1: utils_pow(10, j);
				//MDF_LOGD("week %d, out %llu \n", j, tm_index);
				MDF_FREE(p_week[i]);
				p_week[i] = NULL;
				rc = MDF_OK;
				j = 0;
			   break;
			}
		}
		
		MDF_FREE(p_week[i]);
		p_week[i] = NULL;
	}
	// 012345678 week
	//MDF_LOGD("get week %llu\n", tm_index);
	// get day time  24 * 60 * 60 = 86400 所以 再需要前移 1000000 位
	
	mlink_json_parse(p_src, "StartTime", &p_start_time);
	HMS2UNINT(p_start_time,  start_time);
	tm_index *= (uint64_t) 100000;
	tm_index += start_time;
	//MDF_LOGD("get week + time %llu\n", tm_index);
	*p_tm_index = tm_index;
	
End:
	
	MDF_FREE(p_start_time);
	MDF_FREE(p_start_tm);
	return rc;
}

// 1. json 转 struct 结构.
/***
{
  "start_time": "05:00:00",
  "days": ["Sun","Mon","Tue","Wed","Thu","Fri","Sat"  ],
  "countdown": "00:00:00",
  "name": "Morning",
  "brightness": 100,
  "self": 1,
  "fade": 1.2,
  "additional": {...}
}
****/
static mdf_err_t sch_json2struct(uint64_t id, Sch_file_t **pp_sch, char **pp_error, const char *p_src){
	Sch_file_t *p_sch = NULL;
	char *p_additional = NULL, *p_countdown = NULL;
	int additional_len = 0;
	float fade = 0;

	MDF_PARAM_CHECK(p_src);

	if(  mlink_json_parse(p_src, "Additional", &p_additional) == MDF_OK && NULL != p_additional ){
		additional_len = strlen( p_additional );
	}
	
	p_sch = ( Sch_file_t *) utlis_malloc( sizeof( Sch_file_t ) + additional_len + 8 );
	MDF_ERROR_GOTO( NULL == p_sch, Error_End, "Endto malloc \n");
	if( p_additional && additional_len > 0 ){
		memcpy(p_sch->p_data, p_additional,  additional_len);
		p_sch->data_len =  additional_len;
	}
	if(id == 0){
		mdf_err_t rc = _sch_json2time_id(&p_sch->had.time_id, p_src);
		
		MDF_ERROR_GOTO( MDF_OK != rc &&( *pp_error = (char *)malloc_copy_str("Failt to get Time"),1), \
			Error_End, "Fait to get timeId \n");
		MDF_LOGW("rc = %d \n", rc);
	}else p_sch->had.time_id = id;
	
	mlink_json_parse( p_src, "Brightness", &p_sch->had.bri);
	
	if(MDF_OK ==  mlink_json_parse( p_src, "Fade", &fade) ){
		p_sch->had.fade = fade * 1000;
	}
	
	mlink_json_parse( p_src, "Self", &p_sch->had.self);

	// get Countdown time 
	mlink_json_parse( p_src, "Countdown", &p_countdown);

	if( p_countdown ){
		HMS2UNINT( p_countdown, p_sch->had.countdown_tm);
		MDF_FREE( p_countdown );
		p_countdown = NULL;
	}
	MDF_LOGW("p_additional = %s \n", p_additional);
	mlink_json_parse( p_additional, "SwitchDeviceId", &p_sch->had.SwitchDeviceId);
	mlink_json_parse( p_additional, "PlugDeviceId", &p_sch->had.PlugDeviceId);
	mlink_json_parse( p_additional, "GroupId", &p_sch->had.GroupId);
	mlink_json_parse( p_additional, "UseThisBrightness", &p_sch->had.UseThisBrightness);
	mlink_json_parse( p_additional, "HueURL", &p_sch->had.HueURL);
	mlink_json_parse( p_additional, "HueScene", &p_sch->had.HueScene);
	mlink_json_parse( p_additional, "srss", &p_sch->had.srss);

	*pp_sch =  p_sch;
	//MDF_FREE(p_sch);//suspect
	MDF_FREE(p_additional);
	//MDF_FREE(p_countdown); //suspect
	return MDF_OK;
	
Error_End:

	MDF_FREE(p_sch);
	MDF_FREE(p_additional);
	MDF_FREE(p_countdown);
	
	return MDF_FAIL;
}
static inline mdf_err_t _sch_file_check(Sch_file_t *p_sch){

	MDF_PARAM_CHECK(p_sch);
	MDF_PARAM_CHECK(p_sch->had.time_id > 999999);

	return MDF_OK;
}
// 把 schedule 写入 flash里.
// 1.计算 key
// 2.写入
static mdf_err_t _sch_save(Sch_file_t *p_sch){
	mdf_err_t rc = MDF_FAIL;
	char p_key[32] = {0};

	MDF_PARAM_CHECK(MDF_OK == _sch_file_check( p_sch ) );

	// todo debug 
	//_sch_file_printf(p_sch);
	
	sprintf(p_key, "%llu", p_sch->had.time_id);
	
	rc = utlis_store_save(US_SPA_SCH, p_key, p_sch, _SCH_FILE_LEN(p_sch)  );

	MDF_LOGD("save data to flash key = %s rc = %d\n", p_key, rc);

	return rc;
}
/*********************
**
  {"StartTime": "05:00",
  	"id":xxx,
   "Days": ["Mon","Tue","Wed","Thu","Fri"]
  }
**
*****************/ 
static mdf_err_t _sch_starttime2json(char **pp_json, uint64_t tid){

	char p_tmp[128] = {0}, *p_w  = NULL;
	uint64_t start_tm = 0, week_tm = 0;
	int i;

	MDF_PARAM_CHECK(tid > 0);
	MDF_PARAM_CHECK( pp_json );

	// 0123456786400 =  1234567(星期) + 86400(日内时间) + 01 (00 代表 alarm)，11(tu) 12(td)13(dtu) ..
	start_tm = (tid / 100) %100000;
	MDF_LOGD("tid %llu \n", start_tm);

	sprintf(p_tmp, "%llu", tid);	
	mlink_json_pack(pp_json, "id", p_tmp);
	
	memset(p_tmp, 0, 128);
	HMS2STRING( p_tmp, start_tm);
	mlink_json_pack( pp_json, "StartTime", p_tmp);
	memset(p_tmp, 0, 128);

	// week.
	week_tm = tid / 10000000;
	MDF_LOGD("week %llu \n", week_tm);
	
	p_w = p_tmp;
	p_w[0]='[';
	p_w++;
	
	for(i =0; i< 7;i++){
		if( ( week_tm %10) > 0 ){
			if( strlen(p_tmp) > 3 ){
				*p_w = ',';
				p_w += 1;
			}

			*p_w='\"';
			p_w++;
			memcpy(p_w, _w_day[i], strlen( _w_day[i]) );
			p_w += strlen(_w_day[i]);
			*p_w='\"'; 
			p_w++;
			//MDF_LOGD("Week string %s \n", p_tmp);
		}
		week_tm = week_tm/10;
		//MDF_LOGD("week_tm %llu \n", week_tm);
		if(week_tm == 0)
			break;
	}

	*p_w = ']';
	MDF_LOGD("week string %s\n ", p_tmp);
	mlink_json_pack( pp_json, "Days", p_tmp);

	return MDF_OK;
}
static mdf_err_t _sch_struct2json(char **pp_json , Sch_file_t *p_sch, int parse_week_time){

	mdf_err_t rc = MDF_OK;
	double fade = 0;
	char p_tmp[64] = {0};
	MDF_PARAM_CHECK(pp_json);
	MDF_PARAM_CHECK(p_sch);

	
	// todo 
	//sprintf(p_tmp, "%llu", p_sch->had.time_id );
	//rc = mlink_json_pack(pp_json, "TimeId", p_tmp );
	if(parse_week_time)
		_sch_starttime2json(pp_json, p_sch->had.time_id);

	rc = mlink_json_pack(pp_json, "Brightness", p_sch->had.bri);
	rc = mlink_json_pack(pp_json, "Self", p_sch->had.self);
	
	fade = p_sch->had.fade / 1000.0;
	rc = mlink_json_pack_double(pp_json, "Fade", fade);
	
	// rc = mlink_json_pack(pp_json, "Fade", p_sch->had.fade);
	memset(p_tmp, 0, 64);
	HMS2STRING(p_tmp, p_sch->had.countdown_tm);
	rc = mlink_json_pack(pp_json, "Countdown", p_tmp);

	if(p_sch->p_data && p_sch->data_len > 0){
		char *p_data = (char *)p_sch->p_data;
		rc = mlink_json_pack(pp_json, "Additional", p_data);
	}
	if(*pp_json)
		MDF_LOGD("sch file to  json %s \n",  *pp_json );

	return rc;
}
/****************
// 1. 解析转换成 struct,并解析出该 alarm 的 id= 3123456786400
// 2. 写入 3123456786400-Sch_file_t.
// 3 向"Alarm_list" 添加 "3123456786400" 记录
** alarm 结构：Alarm_list 收录所有 alarm 条目，在计算下一个 alarm，删除单个或所有 alarm时用到，是alarm 的总入口.

key:Alarm_list
value:{ uint8_t numbers; uint64_t p_item[]  }
注意其 value 的结构是 { uint8_t numbers; uint64_t p_item[]  }

************/
#if 0
static mdf_err_t _alarm_list_add( Sch_file_t *p_sch, void *p_data){
	// 获取当前 alarm list.
	// 添加到末尾.添加 sub 子选项.

	mdf_err_t rc = MDF_OK;
	Sch_List_t *p_list = NULL;
	if(mdf_info_load("TC_ALARM", p_list, len))
	
}
#endif
static void _ctl_nums_increase(_Sch_type_t cmd){
	_ctl.p_had[cmd].nums++;
	
	//mdf_info_save("schedule_nums",	&_ctl.p_had , ( sizeof(Sch_had_t) *  _SCH_CMD_MAX) );
	
	utlis_store_save(US_SPA_SCH, "schedule_nums",  _ctl.p_had , ( sizeof(Sch_had_t) *	_SCH_CMD_MAX) );
}
static void _ctl_nums_decrease(_Sch_type_t cmd){
	_ctl.p_had[cmd].nums = ( _ctl.p_had[cmd].nums == 0)?0:(_ctl.p_had[cmd].nums -1);
	
	utlis_store_save(US_SPA_SCH, "schedule_nums",  _ctl.p_had , ( sizeof(Sch_had_t) *	_SCH_CMD_MAX) );
}

static inline int _ctl_nums_get(_Sch_type_t cmd){
	return _ctl.p_had[cmd].nums;
}
static inline uint8_t _ctl_position_get(_Sch_type_t type){
	return _ctl.read_position[type];
}
static inline void _ctl_position_set(_Sch_type_t type, uint8_t position){
	//MDF_LOGE("Position set %d  to %d \n", type, position);
	_ctl.read_position[type] = position;

}

static mdf_err_t _item_sub_add_save(_Sch_type_t stype, uint64_t index, const char *p_src){
	mdf_err_t rc = MDF_FAIL;
	char p_key_str[64] = {0}, *p_name = NULL;
	
	MDF_PARAM_CHECK( p_src);
	MDF_PARAM_CHECK( stype < _SCH_CMD_MAX);

	mlink_json_parse(p_src, "Name", &p_name);

	if( p_name && strlen( p_name) > 0 ){

		Item_sub_t *p_sub = utlis_malloc( sizeof( Item_sub_t ) + strlen( p_name) +1);
		MDF_ERROR_GOTO( NULL == p_sub, End, "Failt to Alloc\n");
		p_sub->name_len = strlen( p_name);
		memcpy(p_sub->p_name, p_name, p_sub->name_len);
		
		sprintf(p_key_str, "%s%llu",_p_key_prifx[stype], index ); // KG changed from %s_%llu to %s%llu
		MDF_LOGE("Name key is %s", p_key_str);
		//mdf_info_save( p_key_str,  p_sub, sizeof( Item_sub_t ) + p_sub->name_len );
		utlis_store_save(US_SPA_SCH, p_key_str, p_sub , sizeof( Item_sub_t ) + p_sub->name_len);

		MDF_LOGD("Add sub item %s to key %s \n", p_sub->p_name, p_key_str);
		MDF_FREE(p_sub);
		p_sub = NULL;
	}
	
	rc = MDF_OK; 
End:

	MDF_FREE(p_name);
	
	return rc;
}
static mdf_err_t item_sub_del(_Sch_type_t stype, uint64_t index){
	mdf_err_t rc = MDF_FAIL;
	char p_key_str[64] = {0};

	MDF_PARAM_CHECK( stype < _SCH_CMD_MAX);
	
	sprintf(p_key_str, "%s%llu", _p_key_prifx[stype], index ); // KG changed from %s_%llu to %s%llu
	rc = mdf_info_erase( p_key_str);
	
	return rc;
}
#if 1
static mdf_err_t item_sub_json_get(char **pp_json, _Sch_type_t stype, Item_t *p_item){
	mdf_err_t rc = MDF_FAIL;
	int len = 0;
	char p_key[64] = {0};
	Item_sub_t *p_sub = NULL;

	sprintf(p_key, "%s%llu", _p_key_prifx[stype], p_item->sch_time_key); // KG changed from %s_%llu to %s%llu

	//p_sub = utlis_info_load(p_key, &len);
	utlis_store_blob_get(US_SPA_SCH, p_key, (void **)&p_sub,(size_t *) &len );
	MDF_ERROR_CHECK( NULL == p_sub, MDF_FAIL, "Failt to read sub item\n");

	if(p_sub->name_len > 0)
		rc = mlink_json_pack(pp_json, "Name", p_sub->p_name);

	MDF_FREE(p_sub);
	
	return rc;
}
#endif

static Sch_List_t *_sch_List_t_get(_Sch_type_t type){

	mdf_err_t rc = MDF_OK;
	Sch_List_t *p_sch = NULL;
	int rlen = 0;
	//int rlen = sizeof(Sch_List_t) + (  _ctl_nums_get(type) * sizeof( Item_t) ) ;

	//p_sch= utlis_malloc(rlen);
	//MDF_ERROR_GOTO( NULL == p_sch, End, "Failt to alloc\n");
	rc = utlis_store_blob_get(US_SPA_SCH, _p_sch_key[type], (void **)&p_sch, (size_t *)&rlen);
	//rc = mdf_info_load( _p_sch_key[type], p_sch, rlen);
	if(rc != MDF_OK ){
		MDF_LOGW("Failt to get %s from flash\n", _p_sch_key[type] );
	}else{

	}
	
	return p_sch;
}
static int _sch_list_add_save( _Sch_type_t stype, uint64_t tm_key){ // if we do it here, it gets both

	mdf_err_t rc = MDF_OK;
	Sch_List_t *p_sch_list = NULL;
	Item_t *p_find = NULL; 
	int idx = -1;
	int rlen = 0, wlen = 0, nums = _ctl_nums_get(stype), i =0;
	
	rlen = sizeof(Sch_List_t)  + ( nums * sizeof(Item_t) );
	wlen = rlen + sizeof(Item_t);
	p_sch_list = (Sch_List_t *) utlis_malloc( wlen );

	if( nums > 0 ){
		Sch_List_t *p_sch_read = NULL;
		p_sch_read = _sch_List_t_get(stype);
		//rc = mdf_info_load( _p_sch_key[stype] , p_sch_list, strlen( _p_sch_key[stype] ) );
		MDF_ERROR_GOTO( NULL == p_sch_read , End, "Faitl to get %s\n", _p_sch_key[stype] );
		memcpy( p_sch_list, p_sch_read, rlen);
		MDF_FREE(p_sch_read);
	}

	for(i=0;i< nums; i++){
		if(   p_sch_list->p_list[i].sch_time_key == tm_key  ){
			p_find = &p_sch_list->p_list[i];
			MDF_LOGD("modify schedule %llu\n", p_find->sch_time_key);
			break;
		}
	}
	
	// new one and try to add to the end of array.
	if(p_find == NULL){
		p_sch_list->p_list[nums].idx = nums +1;
		p_sch_list->p_list[nums].sch_time_key = tm_key;
		_ctl_nums_increase(stype);
		p_find = &p_sch_list->p_list[nums];
		
		MDF_LOGD("Add schedule %llu  to %s successfully \n", p_find->sch_time_key, _p_sch_key[stype]);

		//rc = mdf_info_save( _p_sch_key[stype], p_sch_list, (size_t) wlen );
		utlis_store_save(US_SPA_SCH, _p_sch_key[stype], p_sch_list, (size_t) wlen );
		if(rc != MDF_OK){
			_ctl_nums_decrease(stype);
			goto End;
		}
	}

	idx = p_find->idx;	
End:
	//MDF_FREE(p_find); //suspect //can't free
	MDF_FREE(p_sch_list);
	return idx;
}

static mdf_err_t _sch_list_remove(_Sch_type_t type, uint64_t del_id){
	Sch_List_t  *p_list = _sch_List_t_get(type);

	if(p_list){
		int i =0, nums = _ctl_nums_get(type), wlen = 0;
		
		for( i=0; i< nums; i++){
			
			MDF_LOGD("del id %llu  src id %llu \n", del_id, p_list->p_list[i].sch_time_key);
			 if( p_list->p_list[i].sch_time_key == del_id ){
			 	
			 	MDF_LOGI("Remove %llu \n", del_id);

				if( i <  (nums -1) ){
					memcpy(&p_list->p_list[i], &p_list->p_list[nums-1], sizeof( Item_t ) );
				}
				break;
			 }
		}

		wlen = sizeof(Sch_List_t)  + ( (nums -1 ) * sizeof(Item_t) );

		//if( MDF_OK ==  mdf_info_save( _p_sch_key[type], p_list, (size_t) wlen ) ){
		if( MDF_OK ==  utlis_store_save(US_SPA_SCH,  _p_sch_key[type], p_list, (size_t) wlen )  ){

			MDF_LOGD("save modify  %s \n", _p_sch_key[type]);
			_ctl_nums_decrease(type);
			return MDF_OK;
		}
		
	}
	return MDF_FAIL;
}
/**
** 构造 tap 结构. 
{
"Self": true,
"Brightness": 100,
"Fade": 0.5,
"Countdown": "00:00:00",
"Additional": {
  "UseThisBrightness": xxx
}
}

*****/
static mdf_err_t _alarm_sub_get(char **pp_alarm, int *p_tap_len, uint64_t key_id, char **pp_error){

	// get list 
	mdf_err_t rc = MDF_FAIL;
	Sch_file_t *p_sf = NULL;
	int sch_len = 0;
	MDF_PARAM_CHECK(pp_alarm);
	MDF_PARAM_CHECK(p_tap_len);
	
	p_sf = _sch_file_read_with_id( key_id,  &sch_len); 
	MDF_ERROR_CHECK( (NULL == p_sf &&(*pp_error =(char *) malloc_copy_str( "Failt to read schedule file" ), 1) ), 
		MDF_FAIL, "Failt to read schedule file \n");

	MDF_LOGD("_tap_sub_get \n");

	//_sch_file_printf(p_sf);
	// struct to json 
	rc = _sch_struct2json(pp_alarm, p_sf, 0);

	if(*pp_alarm)
		MDF_LOGD("json %s \n", *pp_alarm);

	MDF_FREE( p_sf );
	p_sf = NULL;
	
	return rc;
}

/***
** 按照 读取 index 获取一项 alarm json.
** 1. 获取 alarm tab.
** 2. 根据 tab 解析并从 flash 中获取各项.
** 3. 组合成 json.
*******/
/**
** 构造 tap 结构.
"alarm get": 
{"id":xx, "time":xx}
*****/
static mdf_err_t _arlarm_single_get(char **pp_tap, int *p_tap_len, Item_t *p_item, char **pp_error){

	// get list 
	int64_t t_id = 0;
	mdf_err_t rc = 0;
	char *p_json = NULL;
	
	int  json_len =0;
	
	t_id = p_item->sch_time_key;
	_alarm_sub_get( &p_json, &json_len, t_id, pp_error );
	if(p_json){
		_sch_starttime2json(&p_json, t_id);

	}else{
		if(NULL == pp_error)
			*pp_error = (char *)malloc_copy_str("Failt to get tap json");

		MDF_LOGW( "Failt  \n");
		MDF_FREE(p_json);
		return MDF_FAIL;
	}
	
	item_sub_json_get(&p_json, _SCH_CMD_ALAM, p_item);
	MDF_LOGI("Tap json %s\n", p_json);

	*pp_tap = p_json;
	*p_tap_len = strlen(p_json);

	MDF_LOGD("len  = %d\n", *p_json);
	return rc;

}

// 输出 [ {alarm1},{alarm2},{alarm3},{alarm4}]
static mdf_err_t _alarm_get(char **pp_taps, char **pp_error){

	int i=0, total_len =0, new_len = 0, nums = 0;
	char *p_taps = NULL, *p_new = NULL;
	
	Sch_List_t *p_sch_list = NULL;
	
	p_sch_list = _sch_List_t_get( _SCH_CMD_ALAM );

	MDF_ERROR_CHECK( NULL == p_sch_list && ( *pp_error = (char *)malloc_copy_str("Failt read alarm"), 1 ) ,
		MDF_FAIL, "Failt read alarm \n");

	nums = _ctl_nums_get( _SCH_CMD_ALAM );
	
	MDF_LOGD("Device have %d \n", nums);
	new_len = 4;
	p_taps = utlis_malloc( new_len );
	MDF_ERROR_CHECK( NULL == p_taps && ( *pp_error = (char *)malloc_copy_str("Failt to alloc"), 1 ) ,
		MDF_FAIL, "Failt to alloc\n");
	
	p_taps[0]='['; 
	total_len = 1;
	for( i=0;i< nums; i++ ){

		if(i <_ctl_position_get( _SCH_CMD_ALAM ) ){
			MDF_LOGI("skip %d\n", i);
			continue;
		}
		
		// p_new = "Alarm": [{"head":xx},{""}]
		_arlarm_single_get(&p_new, &new_len, &p_sch_list->p_list[i], pp_error);
		
		// error happend.
		if( *pp_error || (p_new == NULL || 0 == new_len) ){
			_ctl_position_set( _SCH_CMD_ALAM, 0);
			
			MDF_LOGW("error \n");
			if(*pp_error)
				MDF_LOGW("Error: %s\n", *pp_error);
			
			MDF_FREE(p_taps);
			p_taps = NULL;
			break;
		}

		MDF_LOGD("get json %s \n", p_new);
		
		if( ( total_len + new_len ) > _PACKAGE_MAX_LEN ){
			//i -=1;
			MDF_LOGW("Free p_new \n");
			MDF_FREE(p_new);
			p_new = NULL;
			new_len = 0;
			break;
		}
		MDF_LOGI("get sub  schedule %s\n ", p_new);
		
		total_len += new_len + 2;

		MDF_LOGD( "tap len = %d, new len = %d \n", strlen( p_taps ), strlen( p_new ) );
		MDF_LOGD("total len = %d\n", total_len);
		p_taps = MDF_REALLOC(p_taps ,  total_len );
		
		if(p_taps){
			int olen = strlen(p_taps);
			
			if(olen > 10){
				p_taps[olen] = ',';
				p_taps[olen + 1 ] = '\0';
			}
			strcat( p_taps, p_new );
			MDF_LOGI("get alarm %s\n ", p_taps);

		}else{
			MDF_LOGE("Failt to realloc \n");
		}

		MDF_FREE( p_new );
		p_new = NULL;
		new_len = 0 ;
	}
	if(   p_taps ){

		total_len = strlen(p_taps) + 2;
		
		MDF_LOGI("get tap schedule %s\n ", p_taps);
		MDF_LOGD("total len = %d\n", total_len);

		p_taps = MDF_REALLOC( p_taps,  total_len );
		
		if(p_taps){
			
			int Remaining  = 0;
			p_taps[ total_len - 2 ] = ']';
			p_taps[ total_len -1 ] = '\0';	

			
			i = (i >= nums)?nums:i;
			Remaining = nums - i;
			_ctl_position_set( _SCH_CMD_ALAM, i);
			MDF_LOGI("get tap schedule %s\n ", p_taps);
			
			mlink_json_pack(pp_taps, "Alarms",  p_taps );
			mlink_json_pack(pp_taps, "Remaining",  Remaining);
			MDF_FREE( p_taps );
			p_taps = NULL;
			
		}else{
			MDF_LOGE("Failt to alloc \n");
		}
	
	}

	MDF_FREE(p_new);
	return MDF_OK;
}
static mdf_err_t _alarm_set( uint64_t *p_id, const uint8_t *p_src, char **pp_error){
	int idx = 0;
	mdf_err_t rc = MDF_OK;
	uint64_t id = 0;

	
	rc = _sch_json2time_id(&id, (const char *)p_src);
	id *= 100;
	id += _SCH_TYPE_ALARM;
	// save sub ... 
	idx = _sch_list_add_save( _SCH_CMD_ALAM, id);
	MDF_ERROR_GOTO( -1 == idx &&( *pp_error = (char *)malloc_copy_str("Failt to add to flash!") , 1), \
		End, "Failt to add to flash.\n");
	
	rc = _item_sub_add_save(_SCH_CMD_ALAM, id, (char *)p_src);
	rc = _tapsch_sub_save(id, (char *)p_src, pp_error);
	*p_id = id;
End:
	return rc;
}

/***************************************
 Calculate Sunrise Sunset Function With 
 Longitude Latitude Stored in Flash
****************************************/
static int SrHour,SrMin,SSHour,SSMin;
void CalculateSrAndSs()
{
    struct tm *info;
    double rise, set;
	
	char Lat_2[12],Lon_2[12];
	mdf_info_load(_STORE_LAT_KEY, Lat_2,sizeof(Lat_2)+1);
	mdf_info_load(_STORE_LON_KEY, Lon_2,sizeof(Lon_2)+1);
	//MDF_LOGI("LatiTude %s\n", Lat_2);   //logcomment            
	//MDF_LOGI("LongiTude %s\n", Lon_2);  //logcomment
	
	double lon = atof(Lon_2);
	double lat = atof(Lat_2);
	
	MDF_LOGI("LatiTude %f\n", lat);               
	MDF_LOGI("LongiTude %f\n", lon);
	
    time_t t = time(NULL);
    info = gmtime(&t);
   // MDF_LOGE("UTC:       %s", asctime(info));
    // MDF_LOGE("Date DD-MM-YYYY %02d-%02d-%d \n",info->tm_mday,info->tm_mon+1,info->tm_year+1900);
   // MDF_LOGE("Time HH:MM  %d:%d UTC \n",info->tm_hour,info->tm_min); //logcomment
	
    sun_rise_set( info->tm_year+1900,info->tm_mon+1, info->tm_mday, lon, lat,&rise, &set );
	double min,min2;
	rise = rise - 7;
	set = set - 7;
    SrHour = rise;
    min = (rise - SrHour)*60;
    SrMin = min;

    SSHour = set;
    min2 = (set - SSHour)*60;
    SSMin = min2;

    // MDF_LOGE("SR(HH:MM) %d:%d\n",SrHour,SrMin);
    // MDF_LOGE("SS(HH:MM) => %d:%d \n",SSHour,SSMin);
	
}
static char timebuf_1[14]={0};
void CalculateTimeBuff(int sunrise,int offsetValue)
{
	if(sunrise == 1) // SunRise
	{
		
		int Minutes = SrMin + (SrHour*60);
		Minutes = Minutes + offsetValue;
		SrHour = Minutes/60;
		SrMin = Minutes - (SrHour*60);
		sprintf(timebuf_1,"%02d:%02d:00",SrHour,SrMin);
		// MDF_LOGE("timebuf %s \n",timebuf_1); 
	}
	else
	{
		// MDF_LOGE("bodystr contains sunset"); //sunset
		int Minutes = SSMin + (SSHour*60);
		Minutes = Minutes + offsetValue;
		SSHour = Minutes/60;
		SSMin = Minutes - (SSHour*60);
		sprintf(timebuf_1,"%02d:%02d:00",SSHour,SSMin);
		// MDF_LOGE("timebuf %s \n",timebuf_1); 
	}
}

/***********************************************
 Function that Removes whitespaces from a string
************************************************/
char *remove_white_spaces(char *str)
{
	int i = 0, j = 0;
	while (str[i])
	{
		if ((str[i] != ' ') && (str[i] != '\t'))
		{
          str[j++] = str[i];
		}
		i++;
	}
	str[j] = '\0';
	return str;
}

/***********************************************
 Function that Checks For StartTime Contains
 SR/SS Extract offsetValue from StartTime String
************************************************/
void change_alarm_set_1(char *bodystr, int Tap, char **p_body)
{
	//char check_P[10], check_N[10];
	//char	*srss = NULL;
	size_t heap_before = esp_get_free_heap_size();
	int sunrise = 0, offsetValue = 0;
	//char * TapSchedule =  NULL;
	cJSON *root = cJSON_Parse(bodystr);
	cJSON *startTime  = NULL, *json  = NULL;
	if(Tap == 0)
	{
		//mlink_json_parse( *bodystr, "StartTime",  &srss);
		startTime  = cJSON_GetObjectItemCaseSensitive(root, "StartTime");
	}
	else
	{
		//mlink_json_parse( *bodystr, "TapSchedule",  &TapSchedule);
		// MDF_LOGE("TapSchedule = %s ",TapSchedule); //logcomment
		//mlink_json_parse( TapSchedule, "StartTime",  &srss);
		// MDF_LOGE("srss = %s ",srss); //logcomment
		json  = cJSON_GetObjectItemCaseSensitive(root, "TapSchedule");
		startTime  = cJSON_GetObjectItemCaseSensitive(json, "StartTime");
	}
	if(startTime->valuestring != NULL)
	{
		//check_R = strstr(srss,"R");
		if(strstr(startTime->valuestring,"R"))
		{
			sunrise = 1;   //1 for SR , 0 for SS
			// MDF_LOGE("check_R = %s ",check_R);
		}
		else
		{
			sunrise = 0;
			// MDF_LOGE("SunSet");
		}
		
		if(strstr(startTime->valuestring,"+"))
		{
			// MDF_LOGE("check_P = %s ",check_P);
			//strcpy(check_P,strstr(startTime->valuestring,"+"));
			offsetValue = atoi(strstr(startTime->valuestring,"+"));
			MDF_LOGE("offsetValue = %d ",offsetValue);
		}
		else if(strstr(startTime->valuestring,"-"))
		{
			// MDF_LOGE("check_N = %s ",check_N);
			//strcpy(check_N,strstr(startTime->valuestring,"-"));
			offsetValue = atoi(strstr(startTime->valuestring,"-"));
			MDF_LOGE("offsetValue = %d ",offsetValue);
		}
		else
		{
			offsetValue = 0;  //0 for no offset
		}
		//MDF_FREE(srss);
		//MDF_FREE(TapSchedule);
	}
	if(Tap == 0)
	{
		cJSON *Additional = NULL;
		CalculateSrAndSs();
		// MDF_LOGE("startTime = %s ",startTime->valuestring); 
		CalculateTimeBuff(sunrise,offsetValue);
		Additional  = cJSON_GetObjectItemCaseSensitive(root, "Additional"); 
		cJSON_AddItemToObject(Additional,"srss", cJSON_CreateString(startTime->valuestring)); 
		// MDF_LOGE("srss added to json: %s \n", (startTime->valuestring));
		cJSON_ReplaceItemInObject(root, "StartTime", cJSON_CreateString(timebuf_1)); 
		// MDF_LOGE("StartTime replaced %s \n", timebuf); //logcomment
	
		*p_body = cJSON_PrintUnformatted(root);
		// MDF_LOGE("Returning Converted Alarm Set ==> %s \n", out); //logcomment
		cJSON_Delete(root);
		
		//return out;
	}
	else
	{
		cJSON *TU  = NULL,*Additional  = NULL,*Taps = NULL;
		//cJSON *root = cJSON_Parse(*bodystr);
		//cJSON *json  = cJSON_GetObjectItemCaseSensitive(root, "TapSchedule");
		//cJSON *startTime  = cJSON_GetObjectItemCaseSensitive(json, "StartTime");
		CalculateSrAndSs();
		// MDF_LOGE("startTime = %s ",startTime->valuestring); 
		CalculateTimeBuff(sunrise,offsetValue);
		Taps  = cJSON_GetObjectItemCaseSensitive(json, "Taps");
		if(cJSON_HasObjectItem(Taps, "TU"))
		{
			TU  = cJSON_GetObjectItemCaseSensitive(Taps, "TU");
			Additional  = cJSON_GetObjectItemCaseSensitive(TU, "Additional");
			cJSON_AddItemToObject(Additional,"srss", cJSON_CreateString(startTime->valuestring));
		}
		cJSON_ReplaceItemInObject(json, "StartTime", cJSON_CreateString(timebuf_1)); 
		*p_body = cJSON_PrintUnformatted(root);
		//MDF_LOGE("Returning Converted Alarm Set ==> %s \n", *bodystr); //logcomment
		cJSON_Delete(root);
		
		size_t heap_now = esp_get_free_heap_size();
		MDF_LOGE("leak %d \n",(heap_before-heap_now));
		MDF_LOGE("length %d \n",strlen(*p_body));
		
		
		//return out;
		//int i = 0;
		// if(Taps != NULL)
		// {
			// MDF_LOGE(" Taps \n");
			// i++;
		// }
		// if(json != NULL)
		// {
			// MDF_LOGE(" json \n");
			// i++;
		// }
		// if(TU != NULL)
		// {
			// MDF_LOGE(" TU \n");
			// i++;
		// }
		// if(Additional != NULL)
		// {
			// MDF_LOGE(" Additional \n");
			// i++;
		// }
		// if(Taps != NULL)
		// {
			// MDF_LOGE(" Taps \n");
			// i++;
		// }
	}
	
}

// 设置 alram
// 1. 获取 arlarm json
// 2. 解析json 转换为 struct
// 3. 把 struct 保存到 flash
// 4. 回应 服务器.
mdf_err_t mlink_alarm_set(mlink_handle_data_t        *p_mdata )
{
	mdf_err_t rc = MDF_OK;
	char *p_data = NULL, *p_rep = NULL, *p_error = NULL;
	int status_code = 300;
	uint64_t id = 0;
	char *if_srss = NULL;
	int is_srss = 0;
	char * new_body = NULL;
	MDF_PARAM_CHECK(p_mdata);
	MDF_PARAM_CHECK(p_mdata->req_data);
	
	MDF_LOGD("alarm set get %s\n", p_mdata->req_data);

	// get data
	rc = mlink_json_parse( p_mdata->req_data, "data",  &p_data);
	
	mlink_json_parse( p_data, "StartTime",  &if_srss);
	//if_srss = strstr(if_srss, "S");  
	if(strstr(if_srss, "S"))
	{
		// MDF_LOGE("if_srss => %s",if_srss); //logcomment
		is_srss = strlen(strstr(if_srss, "S"));
	}
	if(is_srss > 0 )
	{
		// MDF_LOGE("p_data contains sunrise/sunset"); //logcomment
		//p_data = change_alarm_set_1(p_data, 0);
		 //new_body = change_alarm_set_1(p_data, 0);//tempcomment
		char *p_body_n = NULL;
		change_alarm_set_1(p_data, 0, &p_body_n);
		MDF_LOGE("p_body_n after change_alarm_set => %s",p_body_n); 
		MDF_FREE(p_data);
		p_data = NULL;
		MDF_ERROR_GOTO( ( NULL == p_body_n && ( p_rep =(char *)malloc_copy_str("Failt Not data\n"), 1) ) , \
		End_Respond, "Failt Not data\n");
	
		rc = _alarm_set( &id, (uint8_t *)p_body_n, &p_error);
		MDF_FREE(p_body_n);
		p_body_n = NULL;
		
		if( MDF_OK == rc)
		_sch_update(_SCH_CMD_ALAM);

		if(  p_error ){
			status_code = 300;
			p_rep = p_error;
			MDF_FREE(p_error); 
		}else {
			
			char p_tmp[32] = {0};
			sprintf(p_tmp, "%llu", id);
			rc = mlink_json_pack( &p_rep, "id", p_tmp);
			status_code = 200;
			
			update_tm[_SCH_CMD_ALAM] = utils_get_current_time_ms()/ 1000;
			event_device_info_update();
			MDF_LOGD("respond %s \n", p_rep);
		}
	}
	else
	{
	MDF_ERROR_GOTO( ( NULL == p_data && ( p_rep =(char *)malloc_copy_str("Failt Not data\n"), 1) ) , \
		End_Respond, "Failt Not data\n");
	rc = _alarm_set( &id, (uint8_t *)p_data, &p_error);
	
	if( MDF_OK == rc)
		_sch_update(_SCH_CMD_ALAM);

	if(  p_error ){
		status_code = 300;
		p_rep = p_error;
		MDF_FREE(p_error); 
	}else {
		
		char p_tmp[32] = {0};
		sprintf(p_tmp, "%llu", id);
		rc = mlink_json_pack( &p_rep, "id", p_tmp);
		status_code = 200;
		
		update_tm[_SCH_CMD_ALAM] = utils_get_current_time_ms()/ 1000;
		event_device_info_update();
		MDF_LOGD("respond %s \n", p_rep);
	}
	MDF_FREE(p_data);
	p_data = NULL;
	}

End_Respond:

	rc = mevt_command_respond_creat( (char *)p_mdata->req_data, status_code, p_rep);
	MDF_FREE(p_rep);
	MDF_FREE(if_srss);
	return rc;
}
/***
** 接收到 
"data": {
    "method": "alarm_get",
    "from_beginning": 1
  }
*******************/
mdf_err_t mlink_alarm_get(mlink_handle_data_t        *p_mdata )
{
	mdf_err_t rc = MDF_FAIL;
	int status_code = 300, begin = 0;
	char *p_tap = NULL, *p_error = NULL, *p_respond = NULL, *p_data = NULL;

	MDF_PARAM_CHECK(p_mdata);
	MDF_PARAM_CHECK(p_mdata->req_data);

	MDF_LOGD("Receive %s \n", p_mdata->req_data);

	mlink_json_parse(p_mdata->req_data, "data", &p_data);

	if(p_data){
		
		mlink_json_parse(p_data, "begin", &begin);
		MDF_LOGD("begin %d \n", begin);

		if(begin)
			_ctl_position_set( _SCH_CMD_ALAM, 0);

		MDF_FREE(p_data);
		p_data = NULL;
	}
	
	rc = _alarm_get(&p_tap, &p_error);
	
	if( p_error ){
		status_code = 300;
		MDF_FREE(p_tap);
		p_tap = NULL;
		p_respond = p_error;
	}else {
		status_code = 200;
		p_respond = p_tap;
	}
	rc = mevt_command_respond_creat( (char *)p_mdata->req_data, status_code, p_respond);
	
	MDF_FREE( p_tap );
	MDF_FREE( p_error );
	
	return rc;
}


mdf_err_t mlink_alarm_del(mlink_handle_data_t        *p_mdata )
{
	mdf_err_t rc = MDF_FAIL;
	int all = 0, status_code = 300, id_len = 0;
	uint64_t *p_array = NULL;
	char  *p_error = NULL, *p_respond = NULL, *p_data = NULL;

	MDF_PARAM_CHECK( p_mdata );
	MDF_PARAM_CHECK( p_mdata->req_data );
	
	MDF_LOGW( "mlink_schedule_del src %s \n", p_mdata->req_data);

	// get data 
	mlink_json_parse( p_mdata->req_data, "data", &p_data);
	
	MDF_ERROR_GOTO( NULL == p_data && (p_error = (char*)malloc_copy_str("Failt to get data from json" ), 1), \
		End, "Failt to get data from json \n");
	
	// get del
	mlink_json_parse( p_data, "All", &all );
	// 
	_tapsch_get_array_id(&p_array, &id_len, p_data);

	if( all > 0 || ( p_array && id_len > 0) ){
		_tapsch_del(_SCH_CMD_ALAM, all, &p_respond, p_array, id_len, &p_error);
	}else{
		p_error = (char *) malloc_copy_str("Can't find All or TapSchedule.");
	}
	
End:

	if( p_error ){
		
		status_code = 300;
		MDF_FREE(p_respond);
		p_respond = p_error;

	}else {
		update_tm[_SCH_CMD_ALAM] = utils_get_current_time_ms()/ 1000;
		status_code = 200;
		event_device_info_update();
	}

	rc = mevt_command_respond_creat( (char *)p_mdata->req_data, status_code, p_respond);
	
	MDF_FREE( p_respond );
	MDF_FREE( p_error );

	return rc;

}

/**
** 1. 对 json 转换 为 struct 并保存.
**/
static mdf_err_t _tapsch_sub_save(uint64_t id, char *p_json, char **pp_error)
{
	mdf_err_t rc = 0;
	Sch_file_t *p_sch_f  = NULL;
	
	//MDF_LOGD("tap Add %llu to flash\n", id);
	rc = sch_json2struct( id, &p_sch_f, pp_error, p_json);

	MDF_ERROR_CHECK( ( *pp_error !=NULL || NULL == p_sch_f ), rc, "Failt to save sub taps\n");
	//_sch_file_printf( p_sch_f );

	rc = _sch_save( p_sch_f );
	MDF_FREE(p_sch_f);
	p_sch_f = NULL;
	return rc;
}
/***
源 json:
{
	"name":xx,
	"time":xx,
	"taps"{"tu":{},"td":{}...}
}
// 1. 获取 schedule json
// 2. 解析json 转换为 struct
// 3. 把 struct 保存到 flash
// 4. 回应 服务器.
*******/
static mdf_err_t _tapsch_single_set(char *p_json, uint64_t *p_id, char **pp_error){
	mdf_err_t  rc =0;
	uint64_t tap_id = 0, sub_id = 0;
	int i = 0, idx = 0;
	char *p_taps = NULL, *p_value = NULL;
	// get time  id 
	rc = _sch_json2time_id(&tap_id, p_json);
	MDF_ERROR_GOTO(rc != MDF_OK &&( *pp_error = (char *)malloc_copy_str("Failt to find StartTime or Days"),1 ), \
		End, "Failt to find StartTime or Days\n");
	// get tap id
	tap_id *= 100;
	tap_id += _SCH_TYPE_TAP;

	MDF_LOGD("Set id %llu \n", tap_id);
	// get tap
	mlink_json_parse(p_json, "Taps", &p_taps);
	
	MDF_ERROR_GOTO(NULL == p_taps && (*pp_error = (char*)malloc_copy_str("Failt to get Taps"), 1),End, "Failt to get Taps\n");
	// save sub ... 
	idx = _sch_list_add_save( _SCH_CMD_TAP, tap_id); //traced
	MDF_ERROR_GOTO( -1 == idx &&( *pp_error = (char *)malloc_copy_str("Failt to add to flash!") , 1), \
		End, "Failt to add to flash.\n");
	
	rc = _item_sub_add_save(_SCH_CMD_TAP, tap_id, p_json); //traced
	MDF_ERROR_GOTO( MDF_OK != rc, End, "Failt to add sub item.\n");

		
	for(i=0; i< _SCH_TAP_MAX && NULL !=  _p_tap_type[i]; i++){

		mlink_json_parse( p_taps, _p_tap_type[i], &p_value);

		if( p_value ){
			// get id 
			sub_id = tap_id + i;
			rc = _tapsch_sub_save(sub_id, p_value, pp_error); //traced
			
			MDF_FREE( p_value );
			p_value = NULL;
			if(MDF_OK != rc){
				char tmp[32] = {0};
				sprintf(tmp, "Failt to save %s", _p_tap_type[i]);
				*pp_error = (char *) malloc_copy_str(tmp);
				break;
			}
		}
	}

	*p_id = tap_id;
	// reset read position
	_ctl_position_set( _SCH_CMD_TAP, 0);
	
End:

	MDF_FREE( p_taps );
	MDF_FREE( p_value );

	return rc;
}
/*****
// 设置 tab schedule
接收到 json:
{
  "Method": "TapScheduleSet",
  "TapSchedule": {... }
}
************/
uint64_t id_update_srss = 0;
static mdf_err_t _tapsch_set(char *p_tap, char **pp_repond ,char **pp_error){
	mdf_err_t  rc = MDF_FAIL;
	uint64_t tap_id = 0;

	MDF_PARAM_CHECK( p_tap );
	MDF_PARAM_CHECK( pp_repond );


	rc = _tapsch_single_set(p_tap, &tap_id, pp_error); //tracing
	if( MDF_OK == rc)
		_sch_update(_SCH_CMD_TAP); //tracing
	
	if( NULL == *pp_error && tap_id != 0){
		char p_tmp[32] = {0};

		sprintf(p_tmp, "%llu", tap_id);
		id_update_srss = tap_id; 		//id_update_srss will be used in ScheduleSET function
		rc = mlink_json_pack( pp_repond, "id", p_tmp);
		MDF_LOGD("respond id %s \n", *pp_repond);
	}
	// get TapSchedule
	return rc;
}
static char *_tap_type_get(uint64_t id){

	int stype  = id % 10;
		
	MDF_LOGD("id %llu, type %d \n", id,  stype);
	if( stype < _SCH_TAP_MAX ){
		return (char *)_p_tap_type[stype];
	}
	return NULL;
}
/**
** 构造 tap 结构.
"TU": 
{
"Self": true,
"Brightness": 100,
"Fade": 0.5,
"Countdown": "00:00:00",
"Additional": {
  "UseThisBrightness": xxx
}
}

*****/
static mdf_err_t _tap_sub_get(char **pp_tap, int *p_tap_len, uint64_t key_id, char **pp_error){

	// get list 
	char *p_tap_type = NULL, *p_json = NULL;
	mdf_err_t rc = MDF_FAIL;
	Sch_file_t *p_sf = NULL;
	int sch_len = 0;
	MDF_PARAM_CHECK(pp_tap);
	MDF_PARAM_CHECK(p_tap_len);

	p_sf = _sch_file_read_with_id( key_id,  &sch_len);
	MDF_ERROR_CHECK( (NULL == p_sf &&(*pp_error =(char *) malloc_copy_str( "Failt to read schedule file" ), 1) ), 
		MDF_FAIL, "Failt to read schedule file \n");

	MDF_LOGD("_tap_sub_get \n");

	//_sch_file_printf(p_sf);
	// struct to json 
	rc = _sch_struct2json(&p_json, p_sf, 0);
	
	MDF_LOGD("json %s \n", p_json);

	// "TU":{json}
	p_tap_type = _tap_type_get(p_sf->had.time_id);
	
	if( p_tap_type && strlen(p_tap_type) > 0 ){
		mlink_json_pack( pp_tap, p_tap_type, p_json);
		MDF_LOGD("Tap %s \n", *pp_tap);
		rc = MDF_OK;
	}else{
		*pp_error = (char *)malloc_copy_str("Failt to find tap schedules.");
	}

	//MDF_LOGD("1 get tap: %s\n", p_json);
	
	MDF_FREE( p_json );
	p_json = NULL;
	MDF_FREE( p_sf );
	p_sf = NULL;

	return rc;
}
static mdf_err_t  _tap_sub_del(_Sch_type_t type, uint64_t tid){

	int i =0, len = 0;
	uint64_t sid = tid;
	char p_key[64] = {0};

	if( type == _SCH_CMD_TAP){
		for( i=0; i <_SCH_TAP_MAX; i++ ){
		
				sid = tid + i;
				
				memset(p_key, 0, 64);
				sprintf(p_key, "%llu", sid);
				len = strlen(p_key);
		
				MDF_LOGI( "erase key %s \n", p_key);
				
				if(len ){
					mdf_info_erase(p_key);
				}else {
					MDF_LOGW("Failt to dele key %llu", sid);
				}
			}
	}else{
		memset(p_key, 0, 64);
		sprintf(p_key, "%llu", tid);
		len = strlen(p_key);
		if(len ){
			mdf_info_erase(p_key);
		}else {
			MDF_LOGW("Failt to dele key %llu", tid);
		}
	}
	
	return MDF_OK;	
}
/**
** 构造 tap 结构.
"TapSchedule": 
{
"TU":{},"TD":{}
}
*****/
static mdf_err_t _tapsch_single_get(char **pp_tap, int *p_tap_len, Item_t *p_item, char **pp_error){

	// get list 
	int64_t t_id = 0;
	mdf_err_t rc = 0;
	char *p_json = NULL;
	
	int i =0, json_len =0;
	
	for(i=0;i<_SCH_TAP_MAX;i++){
		
		t_id = p_item->sch_time_key + i;
		_tap_sub_get( &p_json, &json_len, t_id, pp_error );

		if(p_json){
			// start time 
			MDF_LOGD(" p_json %s \n", p_json);
		}
		if( *pp_error || NULL == p_json ){

			MDF_LOGW( "Failt  \n");

			MDF_FREE(p_json);
			return MDF_FAIL;
		}
	}
	if(p_json)	
		_sch_starttime2json(&p_json, p_item->sch_time_key);

	// get name 
	item_sub_json_get(&p_json, _SCH_CMD_TAP, p_item);
	MDF_ERROR_GOTO( NULL == p_json &&(*pp_error = (char *)malloc_copy_str("Failt to get tap json") ,1), \
		Error_end, "Failt to get tap json\n");
	
	MDF_LOGI("Tap json %s\n", p_json);


	*pp_tap = p_json;
	*p_tap_len = strlen(p_json);

	return rc;

Error_end:

	MDF_LOGE("Error in tapsch_single-get \n");
	MDF_FREE(p_json);
	
	return rc;
}

static bool _tapsch_id_in_array(uint64_t id, uint64_t *p_array, int a_size){
	int i = 0;

	for(i=0; i < a_size; i++){
		if(id == p_array[i])
			return true;
	}

	return false;
}
/********
** 1. delete all the tu/td/dtd/dtu
** 2. delete sub item.
** 3. remove key from list.
*******/ 
static mdf_err_t _tap_sch_del_one(_Sch_type_t type, Item_t  *p_item){
	mdf_err_t rc = MDF_OK;
	/********
	** 1. delete all the tu/td/dtd/dtu
	****/
	rc = _tap_sub_del(type, p_item->sch_time_key);

	/***
	** 2. delete sub item.
	***/
	
	rc = item_sub_del( type , p_item->sch_time_key);
	/***
	** 3. remove key from list.
	*******/ 
	rc = _sch_list_remove(type, p_item->sch_time_key);
	
	return rc;
}
/**********
delete tap schedule 
**/
static mdf_err_t _tapsch_del(_Sch_type_t type, int del_all, char **pp_respond , uint64_t *p_del_id, int del_len, char **pp_error){
	mdf_err_t rc = 0;
	Sch_List_t *p_sch_list = NULL;
	int i=0, nums = 0;
	char *p_re = NULL;

	if( 0 == del_all && NULL == p_del_id)
		return rc;
	p_sch_list = _sch_List_t_get( type );
	MDF_ERROR_CHECK( NULL == p_sch_list && ( *pp_error = (char *)malloc_copy_str("Failt read schedule"), 1 ) ,
		MDF_FAIL, "Failt read schedule\n");
	
	nums = _ctl_nums_get( type );

	MDF_LOGD("nums %d", nums);
	for( i=0; i< nums; i++){
		
		if( del_all || _tapsch_id_in_array( p_sch_list->p_list[i].sch_time_key, p_del_id, del_len) ){
			// del one 
			rc = _tap_sch_del_one(type, &p_sch_list->p_list[i]);
			
			if( MDF_OK == rc){
				char p_tmp[64] = {0};
				int tmp_len = 0;

				// [12,34,56,78]
				sprintf(p_tmp, "%llu", p_sch_list->p_list[i].sch_time_key );

				MDF_LOGI( "del id %s \n", p_tmp);
				
				tmp_len = (int) strlen( p_tmp );
				if( tmp_len > 0){
					if( p_re == NULL){
						p_re = MDF_MALLOC(2);
						p_re[0] = '[';
						p_re[1] = '\0';
					
					}else{
						int nlen = strlen( p_re )  + 1;
						p_re = MDF_REALLOC(p_re,  nlen );
						p_re[nlen - 1] = ',';
						p_re[nlen] = '\0';
					}

					tmp_len = strlen( p_re ) + tmp_len + 1;
					p_re = MDF_REALLOC(p_re, tmp_len );
					
					MDF_ERROR_GOTO( NULL == p_re &&(*pp_error = (char*)malloc_copy_str("Failt to realloc"),1), \
						End, "Failt to realloc \n" );

					strcat(p_re, p_tmp);
					p_re[ tmp_len -1] = '\0';
					
					MDF_LOGI( "del ids  %s \n", p_re);
				}
			}else{
				MDF_LOGE("Failt to del %llu \n", p_sch_list->p_list[i].sch_time_key );
			}
			
		}
	}
	
	if(p_re && strlen(p_re) > 0){

		int nlen = strlen( p_re )  + 2;
		
		MDF_LOGI( "del ids	%s \n", p_re);
		p_re = MDF_REALLOC(p_re,  nlen );
		MDF_LOGI( "del ids	%s \n", p_re);
				
		if(p_re){
			p_re[nlen -2] = ']';
			p_re[nlen -1] = '\0';
			
			MDF_LOGI( "del ids	%s \n", p_re);
			*pp_respond = p_re;
			p_re = NULL;
		}
		
	}
	_sch_update(type);
End:

	MDF_FREE(p_sch_list);
	MDF_FREE(p_re);

	return rc;
}
/**
** {"taps":[ { x },{ x } ]}
*****/
static mdf_err_t _tapsch_get(char **pp_taps, char **pp_error){

	int i=0, total_len =0, new_len = 0, nums = 0;
	char *p_taps = NULL, *p_new = NULL;
	
	Sch_List_t *p_sch_list = NULL;
	
	p_sch_list = _sch_List_t_get( _SCH_CMD_TAP );

	MDF_ERROR_CHECK( NULL == p_sch_list && ( *pp_error = (char *)malloc_copy_str("Failt read schedule"), 1 ) ,
		MDF_FAIL, "Failt read schedule\n");

	nums = _ctl_nums_get( _SCH_CMD_TAP );
	
	MDF_LOGD("Device have %d \n", nums);
	
	new_len = 4;
	p_taps = utlis_malloc( new_len );
	MDF_ERROR_CHECK( NULL == p_taps && ( *pp_error = (char *)malloc_copy_str("Failt to alloc"), 1 ) ,
		MDF_FAIL, "Failt to alloc\n");
	
	p_taps[0]='['; 
	total_len = 1;
	for( i=0;i< nums; i++ ){

		if(i <_ctl_position_get( _SCH_CMD_TAP ) ){
			MDF_LOGI("skip %d\n", i);
			continue;
		}
		
		// p_new = "TapSchedule": [{"head":xx,"TU":{}, "TD":{} }]
		_tapsch_single_get(&p_new, &new_len, &p_sch_list->p_list[i], pp_error);

		// error happend.
		if( *pp_error || (p_new == NULL || 0 == new_len) ){
			_ctl_position_set( _SCH_CMD_TAP, 0);
			
			MDF_LOGW("error \n");
			if(*pp_error)
				MDF_LOGW("Error: %s\n", *pp_error);
			
			MDF_FREE(p_taps);
			p_taps = NULL;
			break;
		}
		
		MDF_LOGD("get json %s \n", p_new);
		if( ( total_len + new_len ) > _PACKAGE_MAX_LEN ){
			//i -=1;
			MDF_LOGW("Free p_new \n");
			MDF_FREE(p_new);
			p_new = NULL;
			new_len = 0;
			break;
		}
		MDF_LOGI("get sub  schedule %s\n ", p_new);
		
		total_len += new_len + 1;

		MDF_LOGD( "tap len = %d, new len = %d \n", strlen( p_taps ), strlen( p_new ) );
		MDF_LOGD("total len = %d\n", total_len);
		MDF_LOGD("free %u \n", esp_get_free_heap_size());
		p_taps = MDF_REALLOC(p_taps ,  total_len );
		
		if(p_taps){
			strcat( p_taps, p_new );
			MDF_LOGI("get schedule %s\n ", p_taps);

		}else{
			MDF_LOGE("Failt to realloc \n");
		}

		MDF_FREE(p_new);
		p_new = NULL;
		new_len = 0 ;

	}
	
	
	if(   p_taps ){

		total_len = strlen(p_taps) + 2;
		
		MDF_LOGI("get tap schedule %s\n ", p_taps);
		MDF_LOGD("total len = %d\n", total_len);

		p_taps = MDF_REALLOC( p_taps,  total_len );
		
		if(p_taps){
			
			int Remaining  = 0;
			p_taps[ total_len - 2 ] = ']';
			p_taps[ total_len -1 ] = '\0';	

			
			i = (i >= nums)?nums:i;
			Remaining = nums - i;
			_ctl_position_set( _SCH_CMD_TAP, i);
			MDF_LOGI("get tap schedule %s\n ", p_taps);
			
			mlink_json_pack(pp_taps, "TapSchedule",  p_taps );
			mlink_json_pack(pp_taps, "Remaining",  Remaining);
			
			MDF_FREE(p_taps);
			p_taps = NULL;
			
		}else{
			MDF_LOGE("Failt to alloc \n");
		}
	}

	MDF_FREE(p_new);
	return MDF_OK;
}

/*****
** del schedule. 
**
{
	"data":{
		"TapSchedule":["id_1", "id_2"],
		"All":1
	}
}
**
********/ 
static mdf_err_t _tapsch_get_array_id(uint64_t **pp_id, int *p_len, char *p_src){
	cJSON *p_array = NULL, *p_item = NULL;
	uint64_t *p_dst = NULL;
	int count = 0;

	MDF_LOGD("src %s \n", p_src);
	
	cJSON *p_cjson = cJSON_Parse( p_src );
	if(p_cjson){

		
		p_array = cJSON_GetObjectItemCaseSensitive(p_cjson, "deleteIds");

		MDF_LOGD("1 p_array %p \n", p_array);

		if( p_array && cJSON_IsArray( p_array)){

			MDF_LOGD("1 \n");
			cJSON_ArrayForEach(p_item, p_array){
				if( cJSON_IsString( p_item ) ){
					
					count += 1;
					p_dst = MDF_REALLOC(p_dst, count * sizeof( uint64_t ) );

					MDF_LOGD( "2 \n");
					sscanf( p_item->valuestring, "%llu", &p_dst[count - 1]);
					MDF_LOGI("src %s, id %llu \n", p_item->valuestring, p_dst[count - 1]);
				}	
			}
		}
		MDF_LOGD("3 \n");
		cJSON_Delete(p_cjson);
	}
	MDF_LOGD("3 \n");

	if( p_dst && count > 0 ){
		*pp_id = p_dst;
		*p_len = count;
	}else{
		MDF_FREE(p_dst);
	}
	
	return MDF_OK;
}
mdf_err_t mlink_schedule_del(mlink_handle_data_t        *p_mdata )
{

	mdf_err_t rc = MDF_FAIL;
	int all = 0, status_code = 300, id_len = 0;
	uint64_t *p_array = NULL;
	char  *p_error = NULL, *p_respond = NULL, *p_data = NULL;

	MDF_PARAM_CHECK( p_mdata );
	MDF_PARAM_CHECK( p_mdata->req_data );
	
	MDF_LOGW("mlink_schedule_del src %s \n",  p_mdata->req_data);

	// get data 
	mlink_json_parse( p_mdata->req_data, "data", &p_data);
	
	MDF_ERROR_GOTO( NULL == p_data && (p_error = (char*)malloc_copy_str("Failt to get data from json" ), 1), \
		End, "Failt to get data from json \n");
	
	// get del
	mlink_json_parse( p_data, "All", &all );
	// 
	_tapsch_get_array_id(&p_array, &id_len, p_data);

	if( all > 0 || ( p_array && id_len > 0) ){
		_tapsch_del(_SCH_CMD_TAP, all, &p_respond, p_array, id_len, &p_error);
	}else{
		p_error = (char *) malloc_copy_str("Can't find All or TapSchedule.");
	}
	
End:

	if( p_error ){
		
		status_code = 300;
		MDF_FREE(p_respond);
		p_respond = p_error;
		p_error = NULL;

	}else {
		status_code = 200;
		update_tm[_SCH_CMD_TAP] = utils_get_current_time_ms()/ 1000;
		event_device_info_update();
	}

	rc = mevt_command_respond_creat( (char *)p_mdata->req_data, status_code, p_respond);
	
	MDF_FREE( p_respond );
	MDF_FREE( p_error );

	return rc;
}

/*****
** get schedule. 
**
{
	"data":{
		"TapSchedule":[{TapSchedule1},{TapSchedule2}],
		"Remaining":xxx
	}
}
**
********/ 
mdf_err_t mlink_schedule_get(mlink_handle_data_t        *p_mdata )
{
	mdf_err_t rc = MDF_FAIL;
	int status_code = 300, begin = 0;
	char *p_tap = NULL, *p_error = NULL, *p_respond = NULL, *p_data = NULL;

	MDF_PARAM_CHECK(p_mdata);
	MDF_PARAM_CHECK(p_mdata->req_data);

	MDF_LOGD("Receive %s \n", p_mdata->req_data);

	mlink_json_parse(p_mdata->req_data, "data", &p_data);

	if(p_data){
		
		mlink_json_parse(p_data, "begin", &begin);
		MDF_LOGD("begin %d \n", begin);

		if(begin)
			_ctl_position_set( _SCH_CMD_TAP, 0);

		MDF_FREE(p_data);
		p_data = NULL;
	}
	
	rc = _tapsch_get(&p_tap, &p_error);
	
	if( p_error ){
		status_code = 300;
		MDF_FREE(p_tap);
		p_tap = NULL;
		p_respond = p_error;
	}else {
		status_code = 200;
		p_respond = p_tap;
	}

	rc = mevt_command_respond_creat( (char *)p_mdata->req_data, status_code, p_respond);
	
	MDF_FREE( p_tap );
	MDF_FREE( p_error );
	
	return rc;
}
int UpdateOffWhileSetting = 0;
mdf_err_t mlink_schedule_set(mlink_handle_data_t 		*p_mdata ) 
{
	UpdateOffWhileSetting = 1;
	// ESP_ERROR_CHECK( heap_trace_start(HEAP_TRACE_LEAKS) );
	mdf_err_t rc = MDF_OK;
	char *p_data = NULL, *p_tab = NULL, *p_rep = NULL, *p_rep_data = NULL;
	int status_code = 300;
	char *if_srss = NULL;
	int is_srss = 0;
	char *p_TapSchedule = NULL;
	//char * new_body = NULL;
	MDF_PARAM_CHECK(p_mdata);
	MDF_PARAM_CHECK(p_mdata->req_data);
	MDF_LOGE("sch set get %s\n", p_mdata->req_data);

	// get data
	rc = mlink_json_parse( p_mdata->req_data, "data",  &p_data);
	// MDF_LOGD("p_data %s\n", p_data);

	mlink_json_parse( p_data, "TapSchedule",  &p_TapSchedule);
	
	// MDF_LOGE("parsed TapSchedule %s",p_TapSchedule);
	mlink_json_parse( p_TapSchedule, "StartTime",  &if_srss);
	
	//if_srss = strstr(if_srss, "S");
	if(strstr(if_srss, "S"))
	{
		is_srss = strlen(strstr(if_srss, "S"));
		//if_srss = NULL;
	}
	if(is_srss > 0 )
	{
		MDF_LOGE("p_data contains sunrise/sunset");
		//p_data = change_alarm_set_1(&p_data, 1);
		char * p_data_n = NULL;
		change_alarm_set_1(p_data, 1, &p_data_n);
		
		//p_data = new_body;
		MDF_FREE(p_data);
		p_data = NULL;
		MDF_LOGE("SR(HH:MM) %d:%d\n",SrHour,SrMin);
		MDF_LOGE("SS(HH:MM) %d:%d\n",SSHour,SSMin);
		MDF_LOGE("TimeBuff%s\n",timebuf_1);
		MDF_LOGE("p_data_n after change_alarm_set => %s",p_data_n);
		
		MDF_ERROR_GOTO( ( NULL == p_data_n && ( p_rep =(char *)malloc_copy_str("Failt No data\n"), 1) ) , \
		End_Respond, "Failt No data\n");
	
		rc = mlink_json_parse( p_data_n, "TapSchedule", &p_tab);
		MDF_ERROR_GOTO( ( NULL == p_tab && ( p_rep =(char *)malloc_copy_str("Failt No TapSchedule\n"), 1) ) , \
			End_Respond, "Failt No TapSchedule\n");
		
		MDF_FREE(p_data_n);
		p_data_n = NULL;

		_tapsch_set( p_tab, &p_rep_data,  &p_rep);
		if( p_rep ){
			status_code = 300;
			MDF_FREE(p_rep_data);
			p_rep_data = NULL;
		}else {
			status_code = 200;
			p_rep = p_rep_data;
			//MDF_FREE(p_rep_data); //cnnot free
			update_tm[_SCH_CMD_TAP] = utils_get_current_time_ms()/ 1000;
			event_device_info_update();
		}
	}
	else
	{
		MDF_ERROR_GOTO( ( NULL == p_data && ( p_rep =(char *)malloc_copy_str("Failt No data\n"), 1) ) , \
		End_Respond, "Failt No data\n");
	
		rc = mlink_json_parse( p_data, "TapSchedule", &p_tab);
		MDF_ERROR_GOTO( ( NULL == p_tab && ( p_rep =(char *)malloc_copy_str("Failt No TapSchedule\n"), 1) ) , \
			End_Respond, "Failt No TapSchedule\n");
		
		// MDF_FREE(p_data);
		// p_data = NULL;

		_tapsch_set( p_tab, &p_rep_data,  &p_rep);
		if( p_rep ){
			status_code = 300;
			MDF_FREE(p_rep_data);
			p_rep_data = NULL;
		}else {
			status_code = 200;
			p_rep = p_rep_data;
			//MDF_FREE(p_rep_data); //cnnot free
			update_tm[_SCH_CMD_TAP] = utils_get_current_time_ms()/ 1000;
			event_device_info_update();
		}
		
		MDF_FREE(p_data);
		p_data = NULL;
		
	}

End_Respond:

	rc = mevt_command_respond_creat( (char *)p_mdata->req_data, status_code, p_rep);
	MDF_FREE(if_srss);
	MDF_FREE( p_tab );
	//MDF_FREE(p_data)
	MDF_FREE(p_TapSchedule);
	MDF_FREE(p_rep);
	UpdateOffWhileSetting = 0;
	// ESP_ERROR_CHECK( heap_trace_stop());
	// MDF_LOGE("====================heapTrace===================");
	// heap_trace_dump();
	// MDF_LOGE("================================================");
	return rc;
}

static uint8_t  _int2bin(uint64_t in_data){

	uint8_t data = 0, i =0;
	
	if(!in_data)
		return data;

	for(;i<8;i++){
		if( ( in_data % 10) > 0)
			data = data | (0x01 << i);
		in_data = in_data / 10;

		if( in_data == 0 )
			break;
	}
	return data;
}
static int _get_week(uint8_t b_week){
	int week = 0;
	uint8_t tmp = b_week;
	for(week=1, tmp = b_week; ( 0 == (tmp & 0x01)); tmp = b_week >> (week - 1) ){
			week++;
			if(week > 8 ){
				return 0;
			}
	}
	return week;
}


static uint64_t _max_diff_get(uint8_t b_ctime,uint8_t  b_tid, uint64_t c_second, uint64_t t_second ){
	int i = 0, w_dt = 0, count_en  = 0, week = 0, zone = 0;
	uint64_t max_diff = 0, c_week_time  = 0; // week_base_time = c_second - (c_second % 86400), c_week_time  = 0;

	zone = local_time_zone_get();

	MDF_LOGD("current zone is %d \n", zone );
	c_week_time = _WEEK_TIME_GET(c_second, zone );
	week = _get_week(b_ctime);
	
	for(i = b_ctime, w_dt =0; i >0 &&  w_dt < 8; i = b_ctime << w_dt){
			if(  b_tid & i  ){
			    if(w_dt == 0 && c_week_time >= t_second ){
			        
			    }else{
					max_diff = c_second + ((int)t_second - (int) c_week_time ) + w_dt * 86400;
					count_en = 1;
					break;
			    }
			}
			w_dt++;
		}

		if( count_en == 0 ){
			week = _get_week( b_ctime );
			w_dt = _get_week( b_tid );
			MDF_LOGD("tid %d week %d df %d \n", b_tid,week, w_dt);
			//MDF_LOGE("week %d, w_dt %   \n", week, w_dt);
			max_diff = ( 7 -  DIFF( w_dt, week ) ) * 86400   +  c_second  +  ((int)t_second - (int) c_week_time );
		}
	
	MDF_LOGD("current time  %llu  t time %llu \n", c_week_time, t_second);
	return max_diff;
}

static uint64_t _min_diff_get(uint8_t b_ctime,uint8_t  b_tid, uint64_t c_second, uint64_t t_second ){
	int i = 0, w_dt = 0, count_en  = 0, week = 0, zone = 0;
	uint8_t tmp = b_ctime;
	uint64_t min = 0, week_base_time = c_second - (c_second % 86400), c_week_time  = 0;
	
	zone = local_time_zone_get();
	//MDF_LOGD("current zone is %d \n", zone );
	c_week_time = _WEEK_TIME_GET(c_second, zone );
	week = _get_week(b_ctime);

	week = _get_week(b_ctime);

	for(i = b_ctime, w_dt =0; i >0 && w_dt < 8; i = b_ctime >> w_dt){
			if(  b_tid & i	){
				if( w_dt ==0 &&  t_second > c_week_time  ){
					
				}else{
					min = c_second + ((int)t_second - (int) c_week_time ) - w_dt * 86400;
					count_en = 1;
					break;
				}
			}
			w_dt++;
		}

		if( count_en == 0 ){
			week = _get_week(b_ctime);
			w_dt = _get_week(b_tid);
			min = c_second - ( 7 -  DIFF( w_dt, week) ) * 86400    +  ((int)t_second - (int) c_week_time );
		}
		
	return min;
}

int _sch_count_diff(uint64_t *p_next_time, uint64_t *p_min_sec, uint64_t *p_max_sec , uint64_t t_id){
	uint8_t week_ctime = 0, week_tid = 0;
	int week = utils_get_current_week(), i = 0;
	uint64_t t_time = 0, ctime = utils_get_current_time_ms() ;
	// get week 
	week_tid = _int2bin(t_id / _BYTE_SECOND);

	// get byte week
	//MDF_LOGD("current week %d \n", week);
	week_ctime =  0x01 << (week - 1);
	
	// get current time 
	ctime = ctime / 1000;
	t_time = (t_id / _BYTE_FUNCTION) % (100000);

	*p_min_sec  = _min_diff_get(week_ctime,week_tid, ctime, t_time );
	*p_max_sec  = _max_diff_get(week_ctime,week_tid, ctime, t_time );
	
	if( (*p_max_sec) > 0  )
		*p_next_time =  (*p_max_sec );

	MDF_LOGD("Min sec %llu \n", (*p_min_sec) );
	MDF_LOGD("Max sec %llu \n", (*p_max_sec) );
	
	MDF_LOGD("t_time %llu current time %llu  next sec %llu \n",  t_time,ctime, (*p_next_time) );
	
	return 0;
}
/*** schedule 获取当前生效的 alarm 或者 schedules *
**输入： type: alram 或者是 schedule  c_time 当前时间单位精确到秒.
**输出：p_curr_id 当前生效schedule,  next id, next_time
***************************************************************************/
uint64_t g_next_id = 0;//Will be checked while updating SRSS

mdf_err_t _sch_close_excel_id_get(_Sch_type_t type, uint64_t *p_next_secend, uint64_t *p_curr_id, uint64_t *p_next_id){
	uint64_t curr_id = 0, next_id = 0, old_min =0, min =0, old_max = 0, max =0, next_secend = 0;
	
	int nums = 0;
	Sch_List_t *p_sch_list = NULL;

	
	nums = _ctl_nums_get( type );
	p_sch_list = _sch_List_t_get( type );
	if(p_sch_list && nums){
		int i =0;
		
		for(i=0;i<nums;i++){
			_sch_count_diff( &next_secend, &min, &max, p_sch_list->p_list[i].sch_time_key);
			if( old_min == 0 ||  min > old_min ){
				old_min = min;
				curr_id = p_sch_list->p_list[i].sch_time_key;
			}
			if( old_max ==0 || old_max > max ){
				old_max =  max;
				*p_next_secend = next_secend;
				next_id = p_sch_list->p_list[i].sch_time_key;
			}
		}
	}else{
		if(nums)
			MDF_LOGE("Failt to get sch list! \n");
		return 0;
	}

 	MDF_LOGD( "Active current id %llu \n", curr_id );
 	MDF_LOGD( "Active next id %llu \n", next_id );
	MDF_LOGD( "nexet second %llu \n", next_secend );
	*p_curr_id = (curr_id > 0 )?curr_id:*p_curr_id;
	*p_next_id = (next_id > 0 )?next_id:*p_next_id;
	
	if(*p_curr_id == *p_next_id)
	{
		g_next_id = 0;
	}
	else
	{
		g_next_id = *p_next_id; 
	}
	MDF_LOGD( "g_next_id %llu \n", g_next_id );
	//MDF_FREE(p_sch_list); //suspect//cannot free
	return 0;
}
/***
** 接收到 
"data": {
    "method": "alarm_get",
    "from_beginning": 1
  }
*******************/
mdf_err_t mlink_current_get(mlink_handle_data_t        *p_mdata )
{
	mdf_err_t rc = MDF_FAIL;
	int status_code = 300, begin = 0;
	char  *p_respond = NULL, *p_data = NULL;
	char p_tmp[128] = {0};
	uint64_t next_secend_alarm, alarm_id_current, alarm_id_next, next_secend_sch, tap_id_current, tap_id_next;
	
	MDF_PARAM_CHECK(p_mdata);
	MDF_PARAM_CHECK(p_mdata->req_data);

	_sch_close_excel_id_get(_SCH_CMD_ALAM, &next_secend_alarm, &alarm_id_current,  &alarm_id_next);
	_sch_close_excel_id_get(_SCH_CMD_TAP, &next_secend_sch, &tap_id_current,  &tap_id_next);

	//_sch_loop_detect(NULL);

	sprintf(p_tmp, "[%llu, %llu, %llu]", next_secend[_SCH_CMD_ALAM], current_id[_SCH_CMD_ALAM], next_id[_SCH_CMD_ALAM] );
	mlink_json_pack(&p_respond, "alarm:", p_tmp);
	memset(p_tmp, 0, 128);

	sprintf(p_tmp, "[%llu, %llu, %llu]", next_secend[_SCH_CMD_TAP], current_id[_SCH_CMD_TAP], next_id[_SCH_CMD_TAP]);
	mlink_json_pack(&p_respond, "TapSchedule:", p_tmp);

	memset(p_tmp, 0, 128);
	unix_time2string( next_secend[_SCH_CMD_ALAM], p_tmp, 128);
	if(strlen(p_tmp) > 0){
		mlink_json_pack(&p_respond, "next_alarm_time", p_tmp);
	}

	memset(p_tmp, 0, 128);
	unix_time2string( next_secend[_SCH_CMD_TAP], p_tmp, 128);
	if(strlen(p_tmp) > 0){
		mlink_json_pack(&p_respond, "next_tap_time", p_tmp);
	}
	
	memset(p_tmp, 0, 128);
	unix_time2string( ( utils_get_current_time_ms()/ 1000 ), p_tmp, 128);
	if(strlen(p_tmp) > 0){
		mlink_json_pack(&p_respond, "current_time", p_tmp);
	}

	status_code = 200;

	rc = mevt_command_respond_creat( (char *)p_mdata->req_data, status_code, p_respond);

	MDF_FREE(p_respond);
	return rc;
}

static void _sch_update(_Sch_type_t type){

	next_secend[type] = 0;
	current_id[type] = 0;
	next_id[type] =0;

	_sch_close_excel_id_get(type, &next_secend[type], &current_id[type], &next_id[type] );
	
	if(type == _SCH_CMD_TAP)
		_sch_tap_setting_update(current_id[_SCH_CMD_TAP]);
}

static int _sch_additional_excel(Sch_file_t *p_sch){
	
	char *p_rq_body = NULL;
	char *p_json = NULL;
	char *p_recv = NULL;
	MDF_LOGE("Process Additional TS _sch_additional_excel here:\n");
	if(( strlen(p_sch->had.HueURL) != 0) && (strlen(p_sch->had.HueScene) == 0))
	{
		MDF_LOGE("HueURL exists and HueScene is NULL\n");
		if(p_sch->had.UseThisBrightness)
		{

			mlink_json_pack(&p_json, "on", true);
			mlink_json_pack(&p_json, "bri", p_sch->had.bri);
			mlink_json_pack(&p_json, "ct", 500);
			mlink_json_pack(&p_rq_body, "body", p_json);
			mlink_json_pack(&p_rq_body, "method", "PUT");
			mlink_json_pack(&p_rq_body, "url", p_sch->had.HueURL);
			MDF_LOGE("DATA TO BE SENT TO HUE ACTIVE AFTER PACKING INTO JSON ==>  %s \n", p_rq_body);
			 if( 0 == hue_active(p_rq_body, & p_recv ) ){
			MDF_LOGD("http receive %s\n", p_recv);
			} 
		}
	}
	else if((strlen(p_sch->had.HueURL) != 0) && (strlen(p_sch->had.HueScene) != 0))
	{
		MDF_LOGE("HueURL and HueScene exists\n");
		if(p_sch->had.bri <= 0)
		{
		mlink_json_pack(&p_json, "on", false);
		mlink_json_pack(&p_json, "bri", 0);
		mlink_json_pack(&p_json, "ct", 500);
	    }
	    else
	    {
	    mlink_json_pack(&p_json, "on", true);	
		mlink_json_pack(&p_json, "scene", p_sch->had.HueScene);
	    }
		mlink_json_pack(&p_rq_body, "body", p_json);
		mlink_json_pack(&p_rq_body, "method", "PUT");
		mlink_json_pack(&p_rq_body, "url", p_sch->had.HueURL);
		MDF_LOGE("DATA TO BE SENT TO HUE ACTIVE AFTER PACKING INTO JSON ==>  %s \n", p_rq_body);
		if( 0 == hue_active(p_rq_body, & p_recv ) )
		{
			 if(p_recv != NULL)
			 {
			  MDF_LOGD("http receive %s\n", p_recv); 
			 }
			 else
			 {
			  MDF_LOGD("Response not receieved");
			 }
		}
	}
	else if((strlen(p_sch->had.HueURL) == 0)) 
	{
		MDF_LOGE("HueURL is NULL \n");
	} 
	_sch_file_printf(p_sch);
	MDF_FREE(p_rq_body);
	MDF_FREE(p_json);
	p_rq_body =NULL;
	p_json = NULL;
	return 0;
}
/**
*** 执行 当前配置
*****/
static int sch_excel(Sch_file_t *p_sch, int fade){
	int ret = 0;
	int power_s = -1, bri_s = -1;
	float fade_s = -1;
	MDF_LOGE("Process TS sch_excel here:\n"); 

	if( NULL == p_sch){
		MDF_LOGE("NULL sch \n");
		return -1;
		}
	_sch_file_printf(p_sch);
	
	if(p_sch->had.self){
		bri_s = p_sch->had.bri;
		if(bri_s <= 0)
			power_s =  0;
		else
			power_s = 1;
		if(fade == -1){
			fade_s = p_sch->had.fade / 1000.0;
		}else{
			fade_s = fade / 1000.0;
		}
		light_change_user(power_s, bri_s, fade_s, -1);
	}

	// todo handle  addition
	if( p_sch->data_len > 0 ){
		_sch_additional_excel(p_sch);
	}
	
	return ret;
}
static void _sch_tap_setting_clean(void){
	int i  ;
	for(i =0; i < _SCH_TAP_MAX; i++){
		MDF_FREE(p_sch_tap[i])
		p_sch_tap[i] =  NULL;
	}
}
// 刷新 当前 tu td dtu dtd 配置
static void _sch_tap_setting_update(uint64_t tid){
	MDF_LOGE("in _sch_tap_setting_update"); 
	
	int i, sch_len =0;
	uint64_t id = 0;
	Sch_file_t *p_sch = NULL;

	MDF_LOGD("Active tap id %llu \n", tid); 
	
	_sch_tap_setting_clean();
	if(  (tid % 100) != _SCH_TYPE_TAP  ){
		
		MDF_LOGE("Not tap id !! \n" );
		return ;
	}
	
	for(i=0;i < _SCH_TAP_MAX;i++){
		
		id =  tid + i;
		p_sch = _sch_file_read_with_id( id,  &sch_len);
		if(p_sch){
			MDF_FREE(p_sch_tap[i]);
			p_sch_tap[i] =  p_sch;
			MDF_LOGD("Update Tap event %d \n", i);
			if(p_sch_tap[i])
			{
				_sch_file_printf(p_sch_tap[i]);
			}
		}else{
			MDF_LOGE("No tap sch in flash id = %llu \n", id);
		}
	}
	//MDF_FREE(p_sch);//suspect //can't free
}

static int _sch_additional_excel_2(Sch_file_t *p_sch, char **p_url){
	
	if(strlen(p_sch->had.HueURL) != 0)
	{
		MDF_LOGE("Getting Url \n");
		*p_url = p_sch->had.HueURL;
		return 0;
	}
 return -1;
}
/***********

Function To get Url From TS and Return Url

***********/

int hue_loop_get_data(_Sch_tap_T evt, char **p_url){
	
	if( _ctl_nums_get(_SCH_CMD_TAP) > 0 &&  evt <_SCH_TAP_MAX )
	{
		MDF_LOGE("Tap event %d \n", evt);
		return _sch_additional_excel_2(p_sch_tap[evt], p_url);
	}else
		MDF_LOGW("No Tap Schedule to Process event \n");
	
	return -1;
}

int tap_event_active(_Sch_tap_T evt, int fade){
	
	if( _ctl_nums_get(_SCH_CMD_TAP) > 0 &&  evt <_SCH_TAP_MAX ){
		MDF_LOGE("Tap event %d \n", evt);
		return sch_excel(p_sch_tap[evt], fade );
	}else
		MDF_LOGW("No Tap Schedule to Process event \n");
	
	return -1;
}
/****************************************
Manual Schedule Set Function
*****************************************/
uint64_t ScheduleSET(char *p_data ) 
{
	mdf_err_t rc = MDF_OK;
	char *p_tab = NULL, *p_rep = NULL, *p_rep_data = NULL;
	
	MDF_LOGD("ScheduleSET get %s\n", p_data);
	
	rc = mlink_json_parse( p_data, "TapSchedule", &p_tab);
	
	_tapsch_set( p_tab, &p_rep_data,  &p_rep);
	if( p_rep ){
		MDF_LOGE("ERROR : %s ",p_rep);
	}else {
		update_tm[_SCH_CMD_TAP] = utils_get_current_time_ms()/ 1000;
		event_device_info_update();
	}
	//if(p_rep_data != NULL)
	MDF_FREE( p_tab );
	MDF_FREE(p_rep);
	
	return id_update_srss;
}

/****************************************
Manual Schedule Delete Function
*****************************************/
void ScheduleDEL(char *p_data )
{
	int all = 0, id_len = 0;
	uint64_t *p_array = NULL;
	char  *p_error = NULL, *p_respond = NULL;
	
	MDF_LOGW("ScheduleDEL src %s \n",  p_data);

	// get del
	mlink_json_parse( p_data, "All", &all );
	// 
	_tapsch_get_array_id(&p_array, &id_len, p_data);

	if( all > 0 || ( p_array && id_len > 0) ){
		_tapsch_del(_SCH_CMD_TAP, all, &p_respond, p_array, id_len, &p_error);
	}else{
		p_error = (char *) malloc_copy_str("Can't find All or TapSchedule.");
	}
	
	if( p_error ){
		MDF_LOGE("ERROR: %s ",p_error);
	}else {
		update_tm[_SCH_CMD_TAP] = utils_get_current_time_ms()/ 1000;
		event_device_info_update();
		MDF_LOGE("Respond %s",p_respond);
	}
	MDF_FREE( p_respond );
	MDF_FREE( p_error );
}
/****************************************
Manual Alarm Set Function
*****************************************/
int64_t AlarmSET(char *p_data )
{
	mdf_err_t rc = MDF_OK;
	char *p_error = NULL;
	uint64_t id = 0;
	MDF_LOGD("AlarmSET get %s\n", p_data);

	rc = _alarm_set( &id, (uint8_t *)p_data, &p_error);
	
	if( MDF_OK == rc)
		_sch_update(_SCH_CMD_ALAM);

	if(  p_error ){
		MDF_LOGD("Error: %s \n", p_error);
	}else {
		MDF_LOGD("Alarm Saved with Id: %llu \n", id);
	}

	return id;
}
/****************************************
Manual Alarm Delete Function
*****************************************/
void AlarmDEL(char *p_data )
{
	mdf_err_t rc = MDF_FAIL;
	int all = 0, status_code = 300, id_len = 0;
	uint64_t *p_array = NULL;
	char  *p_error = NULL, *p_respond = NULL;

	MDF_LOGW( "AlarmDEL src %s \n", p_data);
	
	// get del
	mlink_json_parse( p_data, "All", &all );
	// 
	_tapsch_get_array_id(&p_array, &id_len, p_data);

	if( all > 0 || ( p_array && id_len > 0) ){
		_tapsch_del(_SCH_CMD_ALAM, all, &p_respond, p_array, id_len, &p_error);
	}else{
			MDF_LOGE("Can't find All or TapSchedule.");
	}

	if( p_error ){
	
		MDF_LOGE("ERROR: %s",p_error);

	}else {
		update_tm[_SCH_CMD_ALAM] = utils_get_current_time_ms()/ 1000;
		event_device_info_update();
	}
	MDF_FREE(p_error);
	MDF_FREE(p_respond);
}

/*****************************************
Function that packs Json body for Alarm Or
Tap Schedule
******************************************/

int first_sch = 0;
void JSON_body_Alarm_TS(Sch_file_t *p_sch,char ** p_body)
{
	char *p_additional = NULL;
	mlink_json_pack(&p_additional, "SwitchDeviceId", p_sch->had.SwitchDeviceId);
	mlink_json_pack(&p_additional, "PlugDeviceId", p_sch->had.PlugDeviceId);
	mlink_json_pack(&p_additional, "GroupId", p_sch->had.GroupId);
	mlink_json_pack(&p_additional, "UseThisBrightness", p_sch->had.UseThisBrightness);
	mlink_json_pack(&p_additional, "HueURL", p_sch->had.HueURL);
	mlink_json_pack(&p_additional, "HueScene", p_sch->had.HueScene);
	if(first_sch == 1)
	{
	mlink_json_pack(&p_additional, "srss", p_sch->had.srss);
	first_sch = 0;
	}
	mlink_json_pack(p_body, "Additional",p_additional); 
	if(p_sch->had.countdown_tm > 0)
	{
		uint32_t h,m,s;
		char Countdown_time[15];
		h = (p_sch->had.countdown_tm/3600); 
		m = (p_sch->had.countdown_tm -(3600*h))/60;
		s = (p_sch->had.countdown_tm -(3600*h)-(m*60));
		sprintf(Countdown_time,"%02d:%02d:%02d",h,m,s);
		mlink_json_pack(p_body, "Countdown",Countdown_time);
		
	}
	else
	{
		mlink_json_pack(p_body, "Countdown","00:00:00");
	}
	mlink_json_pack(p_body, "Brightness",p_sch->had.bri);
	mlink_json_pack(p_body, "Self",p_sch->had.self);
	mlink_json_pack(p_body, "Fade",(p_sch->had.fade/1000));
	MDF_FREE(p_additional);
	//cJSON *json = cJSON_Parse(p_json_update);
	// *ret_body = cJSON_PrintUnformatted(json);
	//cJSON_Delete(Additional);
	//cJSON_Delete(json);
	//MDF_FREE(p_json_update);
	//char * out2 = remove_white_spaces(out);
	//return out;
}
/*****************************************
Function that Adds StartTime to Json Body 
of Alarm Or Tap Schedule
******************************************/
void StartTimeSrSSToJson(Sch_file_t *p_sch, char ** p_body)
{
	char  check_P[10], check_N[10];
	int sunrise,offsetValue;
	if(strstr(p_sch->had.srss,"R"))
	{
		sunrise = 1;   //1 for SR , 0 for SS
		// MDF_LOGE("check_R = %s ",check_R); //logcomment

	}
	else
	{
		sunrise = 0;
		MDF_LOGE("SunSet");
	}

	if(strstr(p_sch->had.srss,"+"))
	{
		strcpy(check_P,strstr(p_sch->had.srss,"+"));
		// MDF_LOGE("check_P = %s ",check_P); //logcomment
		offsetValue = atoi(check_P);
		MDF_LOGE("offsetValue = %d ",offsetValue);
	}
	else if(strstr(p_sch->had.srss,"-"))
	{
		strcpy(check_N,strstr(p_sch->had.srss,"-"));
		// MDF_LOGE("check_N = %s ",check_N); //logcomment
		offsetValue = atoi(check_N);
		MDF_LOGE("offsetValue = %d ",offsetValue);
	}
	else
	{
		offsetValue = 0;  //0 for no offset
	}
	
	CalculateSrAndSs();
	CalculateTimeBuff(sunrise,offsetValue);
	mlink_json_pack(p_body,"StartTime",timebuf_1);
	// cJSON *json = cJSON_Parse(p_body);
	// cJSON_AddItemToObject(json,"StartTime", cJSON_CreateString(timebuf_1)); 
	// char *out = cJSON_PrintUnformatted(json);
	// cJSON_Delete(json);
	// //MDF_FREE(p_body);
	// //char * out2 = remove_white_spaces(out);
	// return out;
}

/*****************************************
Function that Adds Days to Json Body 
of Alarm Or Tap Schedule
******************************************/
void AddDaysToJsonBody(uint64_t tid, char ** p_body)
{
	char p_tmp[128] = {0}, *p_w  = NULL;
	uint64_t week_tm = 0;
	week_tm = tid / 10000000;
	MDF_LOGD("week %llu \n", week_tm);
	memset(p_tmp, 0, 128);
	p_w = p_tmp;
	p_w[0]='[';
	p_w++;
	for(int i =0; i< 7;i++){
		if( ( week_tm %10) > 0 ){
			if( strlen(p_tmp) > 3 ){
				*p_w = ',';
				p_w += 1;
			}

			*p_w='\"';
			p_w++;
			memcpy(p_w, _w_day[i], strlen( _w_day[i]) );
			p_w += strlen(_w_day[i]);
			*p_w='\"'; 
			p_w++;
		}
		week_tm = week_tm/10;
		if(week_tm == 0)
			break;
	}
	*p_w = ']';
	MDF_LOGD("week string %s\n ", p_tmp); //logcomment
	mlink_json_pack(p_body,"Days",p_tmp);
	//cJSON *json = cJSON_Parse(p_body);
	//cJSON_AddItemToObject(json,"Days", p_tmp); 
	//char *out = cJSON_PrintUnformatted(json);
	//cJSON_Delete(json);
	//MDF_FREE(p_body);
	//char * out2 = remove_white_spaces(out);
	//return out;
}
/**********************************************
Function that Updates Next TapSchedule Or Alarm
If It contains "SRSS"
***********************************************/
void UpdateIfNextAlarmSRSS(_Sch_type_t type)
{
	int sch_len = 0;
	if((g_next_id != 0) && (type == _SCH_CMD_ALAM))
	{
		Sch_file_t *p_sch = NULL;
		char *p_body = NULL;
		p_sch = _sch_file_read_with_id( g_next_id,  &sch_len);
		//_sch_file_printf(p_sch);
		if(p_sch->p_data)
		{
			// MDF_LOGD("Additional len = %d : %s   \n", p_sch->data_len, p_sch->p_data); 
			if((p_sch->had.srss != NULL) && (strlen(p_sch->had.srss) > 3))
			{
				MDF_LOGD("Next Alarm contains SR/SS, We need to Update it");
				first_sch = 1;
				//body
				JSON_body_Alarm_TS(p_sch, &p_body);
				//adding days to body
				AddDaysToJsonBody(g_next_id,&p_body);
				//adding startTime to body
				StartTimeSrSSToJson(p_sch, &p_body);
				MDF_LOGE("Body for setting alarm  %s ",p_body);
				
				//prev alarm delete
				char *p_del = NULL;
				char idbuff[14]={0};
				mlink_json_pack(&p_del, "All",0);
				sprintf(idbuff,"[\"%llu\"]",g_next_id);
				mlink_json_pack(&p_del, "deleteIds",idbuff);
				MDF_LOGE("Body for deleting alarm  %s ",p_del);
				AlarmDEL(p_del);
				
				//alarm set
				next_id[type] = AlarmSET(p_body);
				MDF_FREE(p_del);
			}
			// else //debugcomment
			// {
				// MDF_LOGD("Next Alarm does not contain SR/SS");
			// }
			MDF_FREE(p_body);
		}
		else
		{
			MDF_LOGD("Does not have any Additional , So NO SR/SS");
		}
		MDF_FREE(p_sch);
		g_next_id = 0;
	}
	else if((g_next_id != 0) && (type == _SCH_CMD_TAP))
	{
		Sch_file_t *p_sch_1 = NULL;
		// MDF_LOGD("Next Id Value = %llu ",g_next_id);
		Sch_file_t *p_sch_tap_1[_SCH_TAP_MAX] = {0};
		uint64_t tid = g_next_id;
		uint64_t id = 0;
		g_next_id = 0;
		for(int i = 0;i < _SCH_TAP_MAX;i++)
		{	
			id =  tid + i;
			p_sch_1 = _sch_file_read_with_id( id,  &sch_len);
			if(p_sch_1)
			{
				//MDF_FREE(p_sch_tap_1[i]);
				p_sch_tap_1[i] =  p_sch_1;
				//_sch_file_printf(p_sch_tap_1[i]);
			}
			else
			{
				MDF_LOGE("No tap sch in flash id = %llu \n", id);
			}
		}
		
		//MDF_FREE(p_sch);
		if((strlen(p_sch_tap_1[0]->had.srss) > 3))
		{
			MDF_LOGD("Next TS contains SR/SS, We need to Update it");
			char * p_body_TU = NULL, * p_body_TD = NULL, *p_body_DTU = NULL, *p_body_DTD = NULL, *p_taps = NULL;
			char * p_body = NULL;
			first_sch = 1;
			JSON_body_Alarm_TS(p_sch_tap_1[0], &p_body_TU);
			JSON_body_Alarm_TS(p_sch_tap_1[1], &p_body_TD);
			JSON_body_Alarm_TS(p_sch_tap_1[2], &p_body_DTU);
			JSON_body_Alarm_TS(p_sch_tap_1[3], &p_body_DTD);
			
			mlink_json_pack(&p_taps, "TU",p_body_TU);
			mlink_json_pack(&p_taps, "TD",p_body_TD);
			mlink_json_pack(&p_taps, "DTU",p_body_DTU);
			mlink_json_pack(&p_taps, "DTD",p_body_DTD);
			mlink_json_pack(&p_body, "Taps",p_taps);
			//adding startTime to body
			StartTimeSrSSToJson(p_sch_tap_1[0], &p_body);
			//adding days to body
			AddDaysToJsonBody(tid, &p_body);
			char * p_TapSchedule = NULL;
			mlink_json_pack( &p_TapSchedule, "TapSchedule", p_body);
			MDF_LOGE("Full Body For Setting Next TapSchedule %s ", p_TapSchedule);
			
			//Delete TapSchedule 
			char *p_del_1 = NULL;
			char idbuff[20];
			mlink_json_pack(&p_del_1, "All",0);
			sprintf(idbuff,"[\"%llu\"]",tid);
			mlink_json_pack(&p_del_1, "deleteIds",idbuff);
			MDF_LOGE("Body For Deleting Schedule %s",p_del_1);
			ScheduleDEL(p_del_1);
			
			//Set New TapSchedule
			next_id[type] = ScheduleSET(p_TapSchedule);
			MDF_LOGE("Schedule SET new id %llu",next_id[type]);
			MDF_FREE(p_TapSchedule);
			MDF_FREE(p_taps);
			MDF_FREE(p_body);
			MDF_FREE(p_del_1);
			if(p_body_TU != NULL)
			{
				MDF_FREE(p_body_TU);
			}
			if(p_body_DTU != NULL)
			{
				MDF_FREE(p_body_DTU);
			}
			if(p_body_TD != NULL)
			{
				MDF_FREE(p_body_TD);
			}
			if(p_body_DTD != NULL)
			{
				MDF_FREE(p_body_DTD);
			}
		}	
		for(int i = 0;i < _SCH_TAP_MAX;i++)
		{	
			if(p_sch_tap_1[i] != NULL)
			{
				MDF_FREE(p_sch_tap_1[i]);
			}
		}
		//MDF_FREE(p_sch);
	}
}


void _sch_active_(_Sch_type_t type, uint64_t id){
	if( 0 != id  && type == _SCH_CMD_ALAM){
		// active alarm
		Sch_file_t *p_sch = NULL;
		int sch_len = 0;

		MDF_LOGD("Excel Alarm %llu \n", id);
		
		p_sch = _sch_file_read_with_id( id,  &sch_len);
		sch_excel(p_sch, -1);
		MDF_FREE(p_sch);

	}
	
	// update 
	_sch_close_excel_id_get(type, &next_secend[type], &current_id[type], &next_id[type] );
	if(type == _SCH_CMD_ALAM)
	{
		UpdateIfNextAlarmSRSS(_SCH_CMD_ALAM);
	}
	// active schedules
	if(type == _SCH_CMD_TAP && current_id[type] != 0){
	 // todo	
		_sch_tap_setting_update(current_id[type]);
		if(UpdateOffWhileSetting != 1)
		{
			MDF_LOGD("===========================UpdateIfNextAlarmSRSS=========================== \n");
			UpdateIfNextAlarmSRSS(_SCH_CMD_TAP);
			MDF_LOGD("=========================================================================== \n");
		}
	}
	
	//Now there is no more difference in leak with SRSS or without SRSS 
	//like previosly with SrSS it was 3000-4500 and without it it was 500-2000
	//Now it seems to be identical in both cases.  ok, but it should be 200-300 right?
	//let me see by sending the command again with or without srss
	//yes without srss it was 132 and with it was 1252 
}


void  schedule_upate_time_get(char *p_str , int len, _Sch_type_t type){
	if( update_tm[type] == 0)
		update_tm[type] = next_secend[type];
	if( update_tm[type] > 0)
		unix_time2string( update_tm[type], p_str, len);
	else{
		memcpy(p_str, "0", strlen("0"));
	}
}
/*** alarm handle 
***************************/
static mdf_err_t _sch_loop_detect(void *p_arg){ 
//			MDF_LOGD("in _sch_loop_detect");

	int i  =0;
	uint64_t ctime = utils_get_current_time_ms() / 1000;	
//	MDF_LOGE("Current Time: %llu \n", ctime);
//	MDF_LOGE("_SCH_CMD_MAX: %d \n", _SCH_CMD_MAX);
	if( ctime < 1597398677)
		return 0;
	
	for(i=0; i < _SCH_CMD_MAX; i++ ){
//	MDF_LOGE("inloop for i = %d \n", i);
		
//		MDF_LOGE("Nums %d Current time %llu, next_sec %llu, time zone: %d \n", _ctl_nums_get(i),  ctime , next_secend[i], local_time_zone_get());
		if( ctime >= next_secend[i] && _ctl_nums_get(i) > 0 )
		{ //&&  local_time_zone_get() != 0 ){
//			MDF_LOGD("Current time %llu, next_sec %llu \n", ctime , next_secend[i]);
			_sch_active_(i, next_id[i]);
		}
	}
	return MDF_OK;
}
mdf_err_t schedule_init(void){
	Sch_had_t *p_had = NULL;
	int len = 0;
	esp_log_level_set(TAG, ESP_LOG_DEBUG); //ESP_LOG_WARN

	if(utlis_store_blob_get( US_SPA_SCH, "schedule_nums", (void **)&p_had, (size_t *)&len) != ESP_OK){
		_ctl.p_had[_SCH_CMD_TAP].nums = 0;
		_ctl.p_had[_SCH_CMD_ALAM].nums = 0;
		utlis_store_save( US_SPA_SCH, "schedule_nums",  _ctl.p_had , ( sizeof(Sch_had_t) *  _SCH_CMD_MAX ) );
		MDF_LOGE("Failt to get schedule_nums \n");
	}else{
		memcpy(_ctl.p_had,  p_had, len);
	}

	MDF_FREE(p_had);
	p_had = NULL;
	MDF_LOGE("alarm %d, tap %d \n", _ctl.p_had[_SCH_CMD_TAP].nums,  _ctl.p_had[_SCH_CMD_ALAM].nums);

	frtc_function_register(FRTC_CMD_SCH , _sch_loop_detect, NULL);
	
	MDF_ERROR_ASSERT( mlink_set_handle("AlarmSet", mlink_alarm_set));
	MDF_ERROR_ASSERT( mlink_set_handle("AlarmDel", mlink_alarm_del));
	MDF_ERROR_ASSERT( mlink_set_handle("AlarmGet", mlink_alarm_get));

	MDF_ERROR_ASSERT( mlink_set_handle("CurrentGet", mlink_current_get));
	MDF_ERROR_ASSERT( mlink_set_handle("ScheduleGet", mlink_schedule_get));
	MDF_ERROR_ASSERT( mlink_set_handle("ScheduleDel", mlink_schedule_del));

	MDF_ERROR_ASSERT( mlink_set_handle("ScheduleSet", mlink_schedule_set));

	return MDF_OK;
}

mdf_err_t schedule_deinit(void){

	return MDF_OK;
}

// Copyright 2017 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "mdf_common.h"
#include "mdf_info_store.h"
#include "esp_partition.h"

#include "utlis_store.h"
static const char *TAG = "utlis_info_store";

char *_p_store_space_str[US_SPA_MAX] = {
	"us_info_system",
	"us_info_sch",
	
	NULL
};
static char *_p_store_partition_str[US_SPA_MAX] = {
	"fact_nvs",
	"fact_nvs",
	NULL
};
static bool parition_flag = false;

esp_err_t utlis_store_init(Store_space_t type)
{

#if 0
	 ESP_LOGI(TAG, "----------------Iterate through partitions---------------");
	  
	  esp_partition_iterator_t it;
	
	  ESP_LOGI(TAG, "Iterating through app partitions...");
	  it = esp_partition_find(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, NULL);
	
	  // Loop through all matching partitions, in this case, all with the type 'data' until partition with desired 
	  // label is found. Verify if its the same instance as the one found before.
	  for (; it != NULL; it = esp_partition_next(it)) {
		  const esp_partition_t *part = esp_partition_get(it);
		  ESP_LOGI(TAG, "\tfound partition '%s' at offset 0x%x with size 0x%x", part->label, part->address, part->size);
	  }

#endif
    if (!parition_flag) {
		
        esp_err_t ret =  nvs_flash_init_partition( _p_store_partition_str[type] );
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            // NVS partition was truncated and needs to be erased
            // Retry nvs_flash_init
            ESP_ERROR_CHECK(nvs_flash_erase_partition( _p_store_partition_str[type] ));
            ret = nvs_flash_init_partition( _p_store_partition_str[type] );
		}
        ESP_ERROR_CHECK(ret);

        parition_flag = true;
    }

    return ESP_OK;
}

esp_err_t utlis_store_erase(Store_space_t spa_type,const char *key)
{
    //MDF_PARAM_CHECK(key);

    esp_err_t ret    = ESP_OK;
    nvs_handle handle = 0;

    /**< Initialize the default NVS partition */
    utlis_store_init(spa_type);

    /**< Open non-volatile storage with a given namespace from the default NVS partition */
    ret = nvs_open_from_partition(_p_store_partition_str[spa_type], _p_store_space_str[spa_type], NVS_READWRITE, &handle);
    MDF_ERROR_CHECK(ret != ESP_OK, ret, "Open non-volatile storage");

    /**
     * @brief If key is MDF_SPACE_NAME, erase all info in MDF_SPACE_NAME
     */
    if(key == NULL){
        ret = nvs_erase_all(handle);
	}else {
        ret = nvs_erase_key(handle, key);
    }

    /**< Write any pending changes to non-volatile storage */
    nvs_commit(handle);

    /**< Close the storage handle and free any allocated resources */
    nvs_close(handle);

    MDF_ERROR_CHECK(ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND,
                    ret, "Erase key-value pair, key: %s", key);

    return ESP_OK;
}

esp_err_t utlis_store_save(Store_space_t spa_type, const char *key, const void *value, size_t length)
{
    MDF_PARAM_CHECK(key);
    MDF_PARAM_CHECK(value);
    MDF_PARAM_CHECK(length > 0);

    esp_err_t ret     = ESP_OK;
    nvs_handle handle = 0;

    /**< Initialize the default NVS partition */
    utlis_store_init(spa_type);

    /**< Open non-volatile storage with a given namespace from the default NVS partition */
    ret = nvs_open_from_partition(_p_store_partition_str[spa_type], _p_store_space_str[spa_type], NVS_READWRITE, &handle);
    MDF_ERROR_CHECK(ret != ESP_OK, ret, "Open non-volatile storage");

	/**< set variable length binary value for given key */
	
	ret = nvs_set_blob(handle, key, value, length);
	
    /**< Write any pending changes to non-volatile storage */
    nvs_commit(handle);

    /**< Close the storage handle and free any allocated resources */
    nvs_close(handle);

    MDF_ERROR_CHECK(ret != ESP_OK, ret, "Set value for given key, key: %s", key);

    return ESP_OK;
}

extern void *utlis_malloc(size_t size);
esp_err_t utlis_store_blob_get(Store_space_t spa_type, const char *key, void **pp_value, size_t *p_len)
{
	size_t  len =0;
    esp_err_t ret     = ESP_OK;
    nvs_handle handle = 0;
	char *p_buffer = NULL;

	MDF_ERROR_CHECK( spa_type >= US_SPA_MAX, -1, "No such parition\n");
    /**< Initialize the default NVS partition */
    utlis_store_init(spa_type);

    /**< Open non-volatile storage with a given namespace from the default NVS partition */
	
	//esp_err_t nvs_open_from_partition(const char *part_name, const char* name, nvs_open_mode open_mode, nvs_handle *out_handle);
    ret = nvs_open_from_partition(_p_store_partition_str[spa_type], _p_store_space_str[spa_type], NVS_READWRITE, &handle);
    MDF_ERROR_CHECK(ret != ESP_OK, ret, "Open non-volatile storage");

	/**< set variable length binary value for given key */
	ret = nvs_get_blob(handle,  key,  NULL, &len);
	if(len == 0 ){
		MDF_LOGE("Failt to read %s len \n", key);
	}
	
	p_buffer = utlis_malloc( len + 1 );
	if(NULL == p_buffer){
		MDF_LOGE("Failt to alloc \n");
		return -1;
	}
	ret = nvs_get_blob(handle,  key,  p_buffer, &len);
	*pp_value = p_buffer;
	*p_len = len;
    /**< Write any pending changes to non-volatile storage */
    nvs_commit(handle);

    /**< Close the storage handle and free any allocated resources */
    nvs_close(handle);

    MDF_ERROR_CHECK(ret != ESP_OK, ret, "Get value for given key, key: %s", key);

    return ESP_OK;
}

esp_err_t __utlis_store_load(Store_space_t spa_type, const char *key, void *value, size_t len, uint32_t type)
{
    MDF_PARAM_CHECK(key);
    MDF_PARAM_CHECK(value);
    MDF_PARAM_CHECK(len);

    esp_err_t ret     = ESP_OK;
    nvs_handle handle = 0;
    size_t *length = NULL;

    if (type == LENGTH_TYPE_NUMBER) {
        length  = &type;
        *length = len;
    } else if (type == LENGTH_TYPE_POINTER) {
        length = (size_t *)len;
    } else {
        MDF_LOGW("The type of parameter lenght is incorrect");
        return MDF_ERR_INVALID_ARG;
    }

    MDF_PARAM_CHECK(*length > 0);

    /**< Initialize the default NVS partition */
    utlis_store_init(spa_type);

    /**< Open non-volatile storage with a given namespace from the default NVS partition */
    ret = nvs_open_from_partition(_p_store_partition_str[spa_type], _p_store_space_str[spa_type], NVS_READWRITE, &handle);
    MDF_ERROR_CHECK(ret != ESP_OK, ret, "Open non-volatile storage");


    /**< Open non-volatile storage with a given namespace from the default NVS partition */
    //ret = nvs_open(_p_store_space_str[spa_type], NVS_READWRITE, &handle);
    //MDF_ERROR_CHECK(ret != ESP_OK, ret, "Open non-volatile storage");

    /**< get variable length binary value for given key */
    //ret = nvs_get_blob(handle, key, value, length);
	ret = nvs_get_str(handle,  key,  value, length);
    /**< Close the storage handle and free any allocated resources */
    nvs_close(handle);

    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        MDF_LOGD("<ESP_ERR_NVS_NOT_FOUND> Get value for given key, key: %s", key);
        return ESP_ERR_NVS_NOT_FOUND;
    }

    MDF_ERROR_CHECK(ret != ESP_OK, ret, "Get value for given key, key: %s", key);

    return ESP_OK;
}

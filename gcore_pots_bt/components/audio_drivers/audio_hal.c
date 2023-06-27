/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2018 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */
/*
 * Shamelessly stolen from the Espressif ADF and modified to work with the gCore
 * POTS shield by Dan Julio 5/2023
 */
 
#include "audio_hal.h"
#include <string.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/semphr.h"

#include "es8388.h"

static const char *TAG = "AUDIO_HAL";

#define AUDIO_HAL_CHECK_NULL(a, format, b, ...) \
    if ((a) == 0) { \
        ESP_LOGE(TAG, format, ##__VA_ARGS__); \
        return b;\
    }
    
#define mutex_lock(m) xSemaphoreTake(m, portMAX_DELAY)
#define mutex_unlock(m) xSemaphoreGive(m)

struct audio_hal {
    esp_err_t (*audio_codec_initialize)(audio_hal_codec_config_t *codec_cfg);
    esp_err_t (*audio_codec_deinitialize)(void);
    esp_err_t (*audio_codec_ctrl)(audio_hal_codec_mode_t mode, audio_hal_ctrl_t ctrl_state);
    esp_err_t (*audio_codec_config_iface)(audio_hal_codec_mode_t mode, audio_hal_codec_i2s_iface_t *iface);
    esp_err_t (*audio_codec_set_volume)(int volume);
    esp_err_t (*audio_codec_get_volume)(int *volume);
    esp_err_t (*audio_codec_set_mic_volume)(int volume);
    esp_err_t (*audio_codec_get_mic_volume)(int *volume);
    xSemaphoreHandle audio_hal_lock;
    void *handle;
};

static audio_hal_handle_t audio_hal;

// Add codecs here (match AUDIO_CODEC_XXXXX defines)
static struct audio_hal audio_hal_codecs_default[] = {
    {
        .audio_codec_initialize = es8388_init,
        .audio_codec_deinitialize = es8388_deinit,
        .audio_codec_ctrl = es8388_ctrl_state,
        .audio_codec_config_iface = es8388_config_i2s,
        .audio_codec_set_volume = es8388_set_voice_volume,
        .audio_codec_get_volume = es8388_get_voice_volume,
        .audio_codec_set_mic_volume = es8388_set_mic_volume,
        .audio_codec_get_mic_volume = es8388_get_mic_volume
    }
};



//
// API
//
bool audio_hal_init(audio_hal_codec_config_t *audio_hal_conf, int index)
{
    esp_err_t ret  = 0;
    if (NULL != audio_hal_codecs_default[index].handle) {
        return true;
    }
    audio_hal = (audio_hal_handle_t) malloc(sizeof(struct audio_hal));
    if (audio_hal == NULL) {
    	ESP_LOGE(TAG, "Could not allocation audio_hal structure");
    	return false;
    }
    memcpy(audio_hal, &audio_hal_codecs_default[index], sizeof(struct audio_hal));
    audio_hal->audio_hal_lock = xSemaphoreCreateMutex();
    if (audio_hal->audio_hal_lock == NULL) {
    	ESP_LOGE(TAG, "Could not create audio_hal_lock semaphore");
    	return false;
    }

    mutex_lock(audio_hal->audio_hal_lock);
    ret  = audio_hal->audio_codec_initialize(audio_hal_conf);
    ret |= audio_hal->audio_codec_config_iface(AUDIO_HAL_CODEC_MODE_BOTH, &audio_hal_conf->i2s_iface);
    audio_hal->handle = audio_hal;
    audio_hal_codecs_default[index].handle = audio_hal;
    mutex_unlock(audio_hal->audio_hal_lock);
    return true;
}

esp_err_t audio_hal_deinit(int index)
{
    esp_err_t ret;
    AUDIO_HAL_CHECK_NULL(audio_hal, "audio_hal handle is null", -1);
    vSemaphoreDelete(audio_hal->audio_hal_lock);
    ret = audio_hal->audio_codec_deinitialize();
    audio_hal->audio_hal_lock = NULL;
    audio_hal->handle = NULL;
    audio_hal_codecs_default[index].handle = NULL;
    free(audio_hal);
    audio_hal = NULL;
    return ret;
}

esp_err_t audio_hal_ctrl_codec(audio_hal_codec_mode_t mode, audio_hal_ctrl_t audio_hal_state)
{
    esp_err_t ret;
    AUDIO_HAL_CHECK_NULL(audio_hal, "audio_hal handle is null", -1);
    mutex_lock(audio_hal->audio_hal_lock);
    ESP_LOGI(TAG, "Codec mode is %d, Ctrl:%d", mode, audio_hal_state);
    ret = audio_hal->audio_codec_ctrl(mode, audio_hal_state);
    mutex_unlock(audio_hal->audio_hal_lock);
    return ret;
}

esp_err_t audio_hal_config_iface(audio_hal_codec_mode_t mode, audio_hal_codec_i2s_iface_t *iface)
{
    esp_err_t ret = 0;
    AUDIO_HAL_CHECK_NULL(audio_hal, "audio_hal handle is null", -1);
    AUDIO_HAL_CHECK_NULL(iface, "Get volume para is null", -1);
    mutex_lock(audio_hal->audio_hal_lock);
    ret = audio_hal->audio_codec_config_iface(mode, iface);
    mutex_unlock(audio_hal->audio_hal_lock);
    return ret;
}

esp_err_t audio_hal_set_volume(audio_hal_volume_item_t type, int volume)
{
    esp_err_t ret;
    AUDIO_HAL_CHECK_NULL(audio_hal, "audio_hal handle is null", -1);
    mutex_lock(audio_hal->audio_hal_lock);
    if (type == AUDIO_HAL_VOLUME_MIC) {
    	ret = audio_hal->audio_codec_set_mic_volume(volume);
    } else {
    	ret = audio_hal->audio_codec_set_volume(volume);
    }
    mutex_unlock(audio_hal->audio_hal_lock);
    return ret;
}

esp_err_t audio_hal_get_volume(audio_hal_volume_item_t type, int *volume)
{
    esp_err_t ret;
    AUDIO_HAL_CHECK_NULL(audio_hal, "audio_hal handle is null", -1);
    AUDIO_HAL_CHECK_NULL(volume, "Get volume para is null", -1);
    mutex_lock(audio_hal->audio_hal_lock);
    if (type == AUDIO_HAL_VOLUME_MIC) {
    	ret = audio_hal->audio_codec_get_mic_volume(volume);
    } else {
    	ret = audio_hal->audio_codec_get_volume(volume);
    }
    mutex_unlock(audio_hal->audio_hal_lock);
    return ret;
}

/*
 * Audio sample recording - provides a mechanism to debug the I2S communication
 * RX/TX synchronization and line echo cancellation by recording audio samples
 * and then writing to files on a Micro-SD card.  This code is designed to be
 * conditionally compiled in (for debugging purposes).
 *
 * Copyright 2023 Dan Julio
 *
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * It is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software.  If not, see <https://www.gnu.org/licenses/>.
 *
 */
#if (CONFIG_AUDIO_SAMPLE_ENABLE == true)
#include "sample.h"
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"


//
// Constants
//
#define MOUNT_POINT "/sdcard"



//
// Variables
//
static const char *TAG = "sample";

// Micro-SD Card
static const char mount_point[] = MOUNT_POINT;
static esp_vfs_fat_sdmmc_mount_config_t mount_config = {
    .format_if_mount_failed = true,
    .max_files = 3,
    .allocation_unit_size = 16 * 1024
};
static sdmmc_host_t host = SDMMC_HOST_DEFAULT();
static sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
static sdmmc_card_t *card;

// Audio recording buffers
int16_t* txbuf;
int16_t* rxbuf;
int16_t* ecbuf;
int push_index;
int push_enable = 0;



//
// Forward declarations
//
void _sample_write_data(int16_t* buf, const char* fn);



//
// API
//
void sample_mem_init()
{
	// Allocate buffers in external SPIRAM
	txbuf = (int16_t*) heap_caps_malloc(SAMPLE_NUM*2, MALLOC_CAP_SPIRAM);
	if (txbuf == NULL) {
		ESP_LOGE(TAG, "malloc txbuf failed");
	}
	
	rxbuf = (int16_t*) heap_caps_malloc(SAMPLE_NUM*2, MALLOC_CAP_SPIRAM);
	if (rxbuf == NULL) {
		ESP_LOGE(TAG, "malloc rxbuf failed");
	}
	
	ecbuf = (int16_t*) heap_caps_malloc(SAMPLE_NUM*2, MALLOC_CAP_SPIRAM);
	if (ecbuf == NULL) {
		ESP_LOGE(TAG, "malloc ecbuf failed");
	}
	
	// Configure card for faster 4-bit operation since gCore supports that
	slot_config.width = 4;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
}


bool sample_start()
{
	esp_err_t ret;
	
	// Attempt to mount Micro-SD Card
    ESP_LOGI(TAG, "Mounting filesystem");
    ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. ");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). ", esp_err_to_name(ret));
        }
        return false;
    }
    sdmmc_card_print_info(stdout, card);
    
	// Setup buffers
	push_index = 0;
	push_enable = 1;
	for (int i=0; i<SAMPLE_NUM; i++) {
		txbuf[i] = 0;
		rxbuf[i] = 0;
		ecbuf[i] = 0;
	}
	
	return true;
}


// Call only after successfully starting recording
bool sample_in_progress()
{
	return (push_enable == 1);
}


void sample_end()
{
	esp_err_t ret;
	
	// Unmount card for user removal
	ret = esp_vfs_fat_sdcard_unmount(mount_point, card);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to unmount the card (%s). ", esp_err_to_name(ret));
	}
}


void sample_record(int16_t tx, int16_t rx, int16_t ec)
{
	if (push_enable == 1) {
		if (push_index < SAMPLE_NUM) {
			txbuf[push_index] = tx;
			rxbuf[push_index] = rx;
			ecbuf[push_index] = ec;
			push_index += 1;
		} else {
			push_enable = 0;
			ESP_LOGI(TAG, "Done recording");
		}
	}
}


void sample_save()
{
	char filename[32];
	static int file_num = 1;
	
	sprintf(filename, "/sdcard/test_tx%d.raw", file_num);
	_sample_write_data(txbuf, filename);
	
	sprintf(filename, "/sdcard/test_rx%d.raw", file_num);
    _sample_write_data(rxbuf, filename);
    
    sprintf(filename, "/sdcard/test_ec%d.raw", file_num);
    _sample_write_data(ecbuf, filename);
    
    file_num += 1;
    
    ESP_LOGI(TAG, "Files saved");
}



//
// Internal functions
//
void _sample_write_data(int16_t* buf, const char* fn)
{
	struct stat st;
	FILE *fp;
	
	if (stat(fn, &st) == 0) {
        // Delete it if it exists
        unlink(fn);
    }
    
    fp = fopen(fn, "w");
    
    fwrite(buf, sizeof(int16_t), push_index, fp);
    
    fclose(fp);
}

#endif /* (CONFIG_AUDIO_SAMPLE_ENABLE == true) */
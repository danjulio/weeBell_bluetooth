/*
 * gcore_pots_bt - A bluetooth handsfree peripheral that interfaces mobile phones
 * with old-school "POTS" telephones via an external audio codec and AG1171 subscriber
 * line interface circuit (SLIC - or hybrid).  Device control is provided using a
 * LVGL-based GUI running on gCore's 480x320 pixel touchscreen.  An external LiPo
 * battery allows portable operation or operation when mains power fails.  The
 * system emulates traditional central office functionality such as tone generation, 
 * dialing detection (rotary and DTMF), ringing and caller ID (CID).  It supports
 * the tone/ring combinations for many countries around the world.
 *
 * Designed to run on gCore with the gCore POTS shield as part of the weeBell project.
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
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "app_task.h"
#include "audio_task.h"
#include "bt_task.h"
#include "gcore_task.h"
#include "gui_task.h"
#include "pots_task.h"
#include "i2c.h"
#include "ps.h"
#include "sys_common.h"


//
// Constants
//
static const char* TAG = "main";

// Uncomment to display initial heap usage
//#define DISPLAY_INIT_HEAP



//
// Variables
//

// Task handles for use by various tasks to send notifications
TaskHandle_t task_handle_app;
TaskHandle_t task_handle_audio;
TaskHandle_t task_handle_bt;
TaskHandle_t task_handle_gcore;
TaskHandle_t task_handle_gui;
TaskHandle_t task_handle_pots;


//
// Application entry
//
void app_main(void)
{
	ESP_LOGI(TAG, "gcore_pots_bt startup");
	
	// Start by initializing the shared (between tasks) I2C interface so we
	// can get device setup information from persistent storage
	if (i2c_master_init() != ESP_OK) {
		ESP_LOGE(TAG, "I2C initialization failed");
		gui_set_fatal_error("I2C initialization failed");
	}
	if (!ps_init()) {
		// ps_init() prints its own error messages so we only need try to display
		// an error message
		ESP_LOGE(TAG, "Initialize Persistant Storage failed");
		gui_set_fatal_error("Initialize Persistant Storage failed");
	}
	
	// Start the tasks that actually comprise the application
	//   Core 0 : PRO
    //   Core 1 : APP
    //
    xTaskCreatePinnedToCore(&app_task,  "app_task",  3072, NULL, 2,  &task_handle_app,  0);
    xTaskCreatePinnedToCore(&audio_task, "audio_task", 3072, NULL, 3, &task_handle_audio, 1);
    xTaskCreatePinnedToCore(&bt_task, "bt_task", 3072, NULL, 2, &task_handle_bt, 0);
	xTaskCreatePinnedToCore(&gcore_task,  "gcore_task",  3072, NULL, 2,  &task_handle_gcore,  0);
	xTaskCreatePinnedToCore(&gui_task,  "gui_task",  3072, NULL, 2,  &task_handle_gui,  0);
	xTaskCreatePinnedToCore(&pots_task, "pots_task", 3072, NULL, 3, &task_handle_pots, 0);
	
#ifdef DISPLAY_INIT_HEAP
	// Let the tasks get started and display the memory state after boot
	vTaskDelay(pdMS_TO_TICKS(1000));
	heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);
#endif
}

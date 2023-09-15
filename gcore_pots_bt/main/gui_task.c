/*
 * GUI Task
 *
 * Contains functions to initialize the LVGL GUI system and a task
 * to evaluate its display related sub-tasks.  The GUI Task is responsible
 * for all access (updating) of the GUI managed by LVGL.
 *
 * Copyright 2020-2023 Dan Julio
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
#include "app_task.h"
#include "bt_task.h"
#include "gcore_task.h"
#include "gui_task.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_gap_bt_api.h"
#include "esp_freertos_hooks.h"
#include "sys_common.h"
#include "disp_spi.h"
#include "disp_driver.h"
#include "touch_driver.h"
#include "gui_screen_main.h"
#include "gui_screen_settings.h"
#include "gui_screen_time.h"
#include "gui_utilities.h"
#if (CONFIG_SCREENDUMP_ENABLE == true)
#include "mem_fb.h"
#endif



//
// GUI Task internal constants
//


//
// GUI Task variables
//

static const char* TAG = "gui_task";

// Dual display update buffers to allow DMA/SPI transfer of one while the other is updated
static lv_color_t lvgl_disp_buf1[DISP_BUF_SIZE];
static lv_color_t lvgl_disp_buf2[DISP_BUF_SIZE];
static lv_disp_buf_t lvgl_disp_buf;

// Display driver
static lv_disp_drv_t lvgl_disp_drv;

// Touchscreen driver
static lv_indev_drv_t lvgl_indev_drv;

// Screen object array and current screen index
static lv_obj_t* gui_screens[GUI_NUM_SCREENS];
static int gui_cur_screen_index = -1;

// Event handling sub-task
static lv_task_t* gui_event_subtask;
static lv_task_t* gui_activity_subtask;
static lv_task_t* gui_messagebox_subtask;

// Request to display message box
static bool req_message_box = false;

// Values updated asynchronously by another task
static float gui_new_mic_gain;
static float gui_new_spk_gain;
static uint32_t gui_new_ssp_pin;
static uint8_t gui_new_peer_addr[6];
static char gui_new_peer_name[ESP_BT_GAP_MAX_BDNAME_LEN+1];

// Buffer for composing message box strings
static char msgbox_buf[64];



//
// GUI Task internal function forward declarations
//
static void _gui_lvgl_init();
static void _gui_screen_init();
static void _gui_add_subtasks();
static void _gui_event_handler_task(lv_task_t* task);
static void _gui_activity_handler_task(lv_task_t* task);
static void _gui_task_messagebox_handler_task(lv_task_t * task);
static void IRAM_ATTR _lv_tick_callback();
#if (CONFIG_SCREENDUMP_ENABLE == true)
static void _gui_do_screendump();
#endif



//
// GUI Task API
//

void gui_task()
{
	ESP_LOGI(TAG, "Start task");

	// Initialize
	_gui_lvgl_init();
	_gui_screen_init();
	_gui_add_subtasks();
	
	// Set the initially displayed screen
	gui_set_screen(GUI_SCREEN_MAIN);
	
	while (1) {
		vTaskDelay(pdMS_TO_TICKS(GUI_TASK_EVAL_MSEC));
		lv_task_handler();
	}
}


void gui_set_screen(int n)
{
	if ((n < GUI_NUM_SCREENS) && (n != gui_cur_screen_index)) {
		gui_cur_screen_index = n;
		
		gui_screen_main_set_active(n == GUI_SCREEN_MAIN);
		gui_screen_settings_set_active(n == GUI_SCREEN_SETTINGS);
		gui_screen_time_set_active(n == GUI_SCREEN_TIME);
		
		lv_scr_load(gui_screens[n]);
	}
}


void gui_set_msgbox_btn(int id, uint16_t btn)
{
	switch (id) {
		case GUI_MSGBOX_INT_ERR:
			if (btn == GUI_MSG_BOX_BTN_DISMSS) {
				xTaskNotify(task_handle_gcore, GCORE_NOTIFY_SHUTOFF_MASK, eSetBits);
			}
			break;
		
		case GUI_MSGBOX_BT_SSP:
			if (btn == GUI_MSG_BOX_BTN_AFFIRM) {
				xTaskNotify(task_handle_bt, BT_NOTIFY_CONFIRM_PIN_MASK, eSetBits);
			} else if (btn == GUI_MSG_BOX_BTN_DISMSS) {
				xTaskNotify(task_handle_bt, BT_NOTIFY_DENY_PIN_MASK, eSetBits);
			}
			break;
		
		case GUI_MSGBOX_BT_AUTH_FAIL:
			// Nothing to do when user dismisses message box
			break;
			
		case GUI_MSGBOX_CLR_PAIRING:
			if (btn == GUI_MSG_BOX_BTN_AFFIRM) {
				gui_screen_settings_forget_peer_info();
				xTaskNotify(task_handle_bt, BT_NOTIFY_FORGET_PAIR_MASK, eSetBits);
			}
			break;
			
#if (CONFIG_AUDIO_SAMPLE_ENABLE == true)
		case GUI_MSGBOX_SMPL_FAIL:
			// Nothing to do when user dimisses message box
			break;
			
		case GUI_MSGBOX_SMPL_DONE:
			// Nothing to do when user dimisses message box
			break;
#endif
	}
}


void gui_set_new_mic_gain(float g)
{
	gui_new_mic_gain = g;
}


void gui_set_new_spk_gain(float g)
{
	gui_new_spk_gain = g;
}


void gui_set_new_pair_ssp_pin(uint32_t ssp_pin)
{
	gui_new_ssp_pin = ssp_pin;
}


void gui_set_new_pair_info(uint8_t* addr, char* name)
{
	for (int i=0; i<6; i++) gui_new_peer_addr[i] = addr[i];
	strcpy(gui_new_peer_name, name);
}


void gui_set_fatal_error(const char* msg)
{
	char* full_msg;
	static bool first_message = true;
	
	// Only log the first error reported (since failures like I2C will cascade...)
	if (first_message) {
		first_message = false;
		full_msg = malloc(GUI_MSG_BOX_MAX_LEN);
		
		sprintf(full_msg, "Internal Error Occurred: %s.  Click OK to shut down.", msg);
		
		gui_preset_message_box_string(full_msg, false, GUI_MSGBOX_INT_ERR);
		req_message_box = true;
		
		free(full_msg);
	}
}



//
// GUI Task Internal functions
//

static void _gui_lvgl_init()
{
	// Initialize lvgl
	lv_init();
	
	// Interface and driver initialization
	disp_driver_init(true);
	touch_driver_init();
	
	// Install the display driver
	lv_disp_buf_init(&lvgl_disp_buf, lvgl_disp_buf1, lvgl_disp_buf2, DISP_BUF_SIZE);
	lv_disp_drv_init(&lvgl_disp_drv);
	lvgl_disp_drv.flush_cb = disp_driver_flush;
	lvgl_disp_drv.buffer = &lvgl_disp_buf;
	lv_disp_drv_register(&lvgl_disp_drv);
	
	// Install the touchscreen driver
    lv_indev_drv_init(&lvgl_indev_drv);
    lvgl_indev_drv.read_cb = touch_driver_read;
    lvgl_indev_drv.type = LV_INDEV_TYPE_POINTER;
    lv_indev_drv_register(&lvgl_indev_drv);
	
    // Hook LVGL's timebase to the CPU system tick so it can keep track of time
    esp_register_freertos_tick_hook(_lv_tick_callback);
}


static void _gui_screen_init()
{
	// Create the various screens
	gui_screens[GUI_SCREEN_MAIN] = gui_screen_main_create();
	gui_screens[GUI_SCREEN_SETTINGS] = gui_screen_settings_create();
	gui_screens[GUI_SCREEN_TIME] = gui_screen_time_create();
}


static void _gui_add_subtasks()
{
	// Event handler sub-task runs every GUI_TASK_EVAL_MSEC mSec
	gui_event_subtask = lv_task_create(_gui_event_handler_task, GUI_TASK_EVAL_MSEC, LV_TASK_PRIO_MID, NULL);
	
	// Touch activity detection runs twice per second
	gui_activity_subtask = lv_task_create(_gui_activity_handler_task, 500, LV_TASK_PRIO_LOW, NULL);
	
	// Message box display sub-task runs every GUI_EVAL_MSEC mSec
	gui_messagebox_subtask = lv_task_create(_gui_task_messagebox_handler_task, GUI_TASK_EVAL_MSEC,
		LV_TASK_PRIO_LOW, NULL);
}


static void _gui_event_handler_task(lv_task_t * task)
{
	uint32_t notification_value;
	
	// Look for incoming notifications (clear them upon reading)
	if (xTaskNotifyWait(0x00, 0xFFFFFFFF, &notification_value, 0)) {
		if (Notification(notification_value, GUI_NOTIFY_POWER_UPDATE_MASK)) {
			gui_screen_main_update_power_state();
		}
		
		if (Notification(notification_value, GUI_NOTIFY_STATUS_UPDATE_MASK)) {
			gui_screen_main_update_status();
		}
		
		if (Notification(notification_value, GUI_NOTIFY_PH_NUM_UPDATE_MASK)) {
			gui_screen_main_update_ph_num();
		}
		
		if (Notification(notification_value, GUI_NOTIFY_CID_NUM_UPDATE_MASK)) {
			gui_screen_main_update_cid_num();
		}
		
		if (Notification(notification_value, GUI_NOTIFY_UPDATE_MIC_GAIN_MASK)) {
			gui_screen_settings_update_mic_gain(gui_new_mic_gain);
		}
		
		if (Notification(notification_value, GUI_NOTIFY_UPDATE_SPK_GAIN_MASK)) {
			gui_screen_settings_update_spk_gain(gui_new_spk_gain);
		}
		
		if (Notification(notification_value, GUI_NOTIFY_NEW_SSP_PIN_MASK)) {
			sprintf(msgbox_buf, "Confirm %d is displayed on the cellphone", gui_new_ssp_pin);
			gui_preset_message_box_string(msgbox_buf, true, GUI_MSGBOX_BT_SSP);
			req_message_box = true;
		}
		
		if (Notification(notification_value, GUI_NOTIFY_NEW_PAIR_INFO_MASK)) {
			gui_screen_settings_update_peer_info(gui_new_peer_addr, gui_new_peer_name);
		}
		
		if (Notification(notification_value, GUI_NOTIFY_FORGET_PAIRING_MASK)) {
			gui_screen_settings_forget_peer_info();
		}
		
		if (Notification(notification_value, GUI_NOTIFY_BT_AUTH_FAIL_MASK)) {
			sprintf(msgbox_buf, "Bluetooth authentication failed");
			gui_preset_message_box_string(msgbox_buf, false, GUI_MSGBOX_BT_AUTH_FAIL);
			req_message_box = true;
		}
		
		if (Notification(notification_value, GUI_NOTIFY_MESSAGEBOX_MASK)) {
			req_message_box = true;
		}

#if (CONFIG_SCREENDUMP_ENABLE == true)
		if (Notification(notification_value, GUI_NOTIFY_SCREENDUMP_MASK)) {
			_gui_do_screendump();
		}
#endif
	}
}


static void _gui_activity_handler_task(lv_task_t* task)
{
	// Notify gcore_task if we've seen any touch recently
	if (touch_driver_saw_touch()) {
		xTaskNotify(task_handle_gcore, GCORE_NOTIFY_ACTIVITY_MASK, eSetBits);
	}
}


// LVGL sub-task to handle events to handle requests to display the messagebox.
// Since the messagebox has an animation to close we have to make sure it is fully
// closed from a previous message before triggering it again.
 static void _gui_task_messagebox_handler_task(lv_task_t * task)
 {
 	if (req_message_box && !gui_message_box_displayed()) {
 		req_message_box = false;
 		gui_preset_message_box(gui_screens[gui_cur_screen_index]);
 	}
 }


static void IRAM_ATTR _lv_tick_callback()
{
	lv_tick_inc(portTICK_RATE_MS);
}


#if (CONFIG_SCREENDUMP_ENABLE == true)
// This task blocks gui_task
void _gui_do_screendump()
{
	char line_buf[161];   // Large enough for 32 16-bit hex values with a space between them
	int i, j, n;
	int len = MEM_FB_W * MEM_FB_H;
	uint16_t* fb;
	
	// Configure the display driver to render to the screendump frame buffer
	disp_driver_en_dump(true);
	
	// Force LVGL to redraw the entire screen (to the screendump frame buffer)
	lv_obj_invalidate(lv_scr_act());
	lv_refr_now(lv_disp_get_default());
	
	// Reconfigure the driver back to the LCD
	disp_driver_en_dump(false);
	
	// Dump the fb
	fb = (uint16_t*) mem_fb_get_buffer();
	i = 0;
	while (i < len) {
		n = 0;
		for (j=0; j<32; j++) {
			sprintf(line_buf + n, "%x ", *fb++);
			n = strlen(line_buf);
		}
		i += j;
		printf("%s: FB: %s\n", TAG, line_buf);
		vTaskDelay(pdMS_TO_TICKS(20));
	}
}
#endif

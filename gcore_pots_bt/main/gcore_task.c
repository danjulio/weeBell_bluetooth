/**
 *
 * gcore_task.c
 *  - Battery voltage and charge state monitoring
 *  - Critical battery voltage monitoring and auto shut down (with re-power on charge start)
 *  - Power button monitoring for manual power off
 *  - Backlight control
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
 */
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "bt_task.h"
#include "gcore_task.h"
#include "gui_task.h"
#include "gcore.h"
#include "ps.h"
#include "sys_common.h"

//
// Constants
//

// Power state update count
#define GCORE_UPD_STEPS (GCORE_PWR_UPDATE_MSEC / GCORE_EVAL_MSEC)

// Dim steps
#define GCORE_DIM_STEPS (GUI_DIM_MSEC / GCORE_EVAL_MSEC)
#define GCORE_BRT_STEPS (GUI_BRT_MSEC / GCORE_EVAL_MSEC)

// Backlight control state
#define GCORE_BL_NORMAL 0
#define GCORE_BL_DIMUP  1
#define GCORE_BL_DIMDN  2
#define GCORE_BL_DIM    3

// Voltage/Current level logging steps
#define GCORE_LOG_IV_INFO_STEPS (GCORE_LOG_IV_INFO_MSEC / GCORE_EVAL_MSEC)




//
// Variables
//

static const char* TAG = "gcore_task";

// Power state
static int gui_update_count = 0;
static int iv_log_update_count = 0;
static enum BATT_STATE_t upd_batt_state = BATT_0;
static enum CHARGE_STATE_t upd_charge_state = CHARGE_OFF;
static SemaphoreHandle_t power_state_mutex;

// Backlight state
static bool saw_activity = false;
static bool en_auto_dim;
static uint8_t backlight_percent;

// Notification flags - set by a notification and consumed/cleared by state evaluation
static bool notify_poweroff = false;



//
// Forward declarations for internal functions
//
static void _gcoreHandleNotifications();
static void _gcoreEvalBacklight();



//
// API Routines
//
void gcore_task()
{
	batt_status_t cur_batt_status;
	
	ESP_LOGI(TAG, "Start task");
	
	// Power state semaphore
	power_state_mutex = xSemaphoreCreateMutex();
	
	if (!power_init()) {
		ESP_LOGE(TAG, "Power monitoring init failed");
		gui_set_fatal_error("Power monitoring init failed");
		
		// We need to process notifications to detect attempt to power off
		while (true) {
			_gcoreHandleNotifications();
			
			if (notify_poweroff) {
				// Try to disable auto-wakeup
				(void) gcore_set_reg8(GCORE_REG_WK_CTRL, 0);
			
				// Try to power off
				power_off();
			}
			
			vTaskDelay(pdMS_TO_TICKS(GCORE_EVAL_MSEC));
		}
	}
	
	// Set the button detection threshold
	(void) gcore_set_reg8(GCORE_REG_PWR_TM, GCORE_BTN_THRESH_MSEC / 10);
	
	// Get initial screen brightness information
	ps_get_brightness_info(&backlight_percent, &en_auto_dim);
	
	while (true) {
		// Get any new notifications
		_gcoreHandleNotifications();
		
		// Backlight intensity update
		_gcoreEvalBacklight();
		
		// Update battery values
		power_batt_update();
			
		// Look for power-off button press
		if (power_button_pressed() || notify_poweroff) {
			if (notify_poweroff) {
				ESP_LOGI(TAG, "Power off requested");
			} else {
				ESP_LOGI(TAG, "Power button press detected");
			}
			
			// Notify bluetooth to disconnect as a courtesy to the remote devcie
			xTaskNotify(task_handle_bt, BT_NOFITY_DISCONNECT_MASK, eSetBits);
			
			// Disable auto-wakeup
			(void) gcore_set_reg8(GCORE_REG_WK_CTRL, 0);
			
			// Delay for message and power down
			vTaskDelay(pdMS_TO_TICKS(100));
			power_off();
		}
		
		// Look for critical battery shutdown
		power_get_batt(&cur_batt_status);
				
		if (cur_batt_status.batt_state == BATT_CRIT) {
			ESP_LOGI(TAG, "Critical battery voltage detected");
			
			// Notify bluetooth to disconnect as a courtesy to the remote devcie
			xTaskNotify(task_handle_bt, BT_NOFITY_DISCONNECT_MASK, eSetBits);
			
			// Enable auto-wakeup on charge
			(void) gcore_set_reg8(GCORE_REG_WK_CTRL, GCORE_WK_CHRG_START_MASK);
			
			// Delay for message and power down
			vTaskDelay(pdMS_TO_TICKS(100));
			power_off();
		}
		
		// Look for timeout to notify GUI
		if (++gui_update_count >= GCORE_UPD_STEPS) {
			gui_update_count = 0;
					
			xSemaphoreTake(power_state_mutex, portMAX_DELAY);
			upd_batt_state = cur_batt_status.batt_state;
			upd_charge_state = cur_batt_status.charge_state;
			xSemaphoreGive(power_state_mutex);
			
			xTaskNotify(task_handle_gui, GUI_NOTIFY_POWER_UPDATE_MASK, eSetBits);
		}
		
		// Occasionally log power info
		if (++iv_log_update_count >= GCORE_LOG_IV_INFO_STEPS) {
			iv_log_update_count = 0;
			ESP_LOGI(TAG, "Vusb: %1.2fv, Iusb: %dmA, Vbatt: %1.2fv, Iload: %dmA, Chg: %d",
					 cur_batt_status.usb_voltage, cur_batt_status.usb_ma,
					 cur_batt_status.batt_voltage, cur_batt_status.load_ma, 
					 (int) cur_batt_status.charge_state);
		}
		
		vTaskDelay(pdMS_TO_TICKS(GCORE_EVAL_MSEC));
	}
}


void gcore_get_power_state(enum BATT_STATE_t* bs, enum CHARGE_STATE_t* cs)
{
	xSemaphoreTake(power_state_mutex, portMAX_DELAY);
	*bs = upd_batt_state;
	*cs = upd_charge_state;
	xSemaphoreGive(power_state_mutex);
}



//
// Internal functions
//
static void _gcoreHandleNotifications()
{
	uint32_t notification_value = 0;
	
	// Clear notification flags
	notify_poweroff = false;
	
	// Handle notifications (clear them upon reading)
	if (xTaskNotifyWait(0x00, 0xFFFFFFFF, &notification_value, 0)) {
		if (Notification(notification_value, GCORE_NOTIFY_SHUTOFF_MASK)) {
			notify_poweroff = true;
		}
		
		if (Notification(notification_value, GCORE_NOTIFY_ACTIVITY_MASK)) {
			saw_activity = true;
		}
		
		if (Notification(notification_value, GCORE_NOTIFY_BRGHT_UPD_MASK)) {
			ps_get_brightness_info(&backlight_percent, &en_auto_dim);
		}
	}
}


static void _gcoreEvalBacklight()
{
	static int bl_state = GCORE_BL_NORMAL;
	static int auto_dim_timer = 0;
	static float animate_val;
	static float animate_delta;
	static uint8_t cur_bl_val = 0;
	
	switch (bl_state) {
		case GCORE_BL_NORMAL:
			// Look for intensity changes
			if (cur_bl_val != backlight_percent) {
				power_set_brightness(backlight_percent);
				cur_bl_val = backlight_percent;
			}
			
			// Evaluate transition to dim if enabled
			if (en_auto_dim) {
				if (saw_activity) {
					// Hold timer in reset
					saw_activity = false;
					auto_dim_timer = 0;
				} else {
					auto_dim_timer += GCORE_EVAL_MSEC;
					if (auto_dim_timer >= GUI_INACTIVITY_TO_MSEC) {
						// Setup to dim
						bl_state = GCORE_BL_DIMDN;
						animate_val = (float) backlight_percent;
						animate_delta = (float) ((GUI_BL_DIM_PERCENT - backlight_percent) / GCORE_DIM_STEPS);
					}
				}
			}
			break;
		
		case GCORE_BL_DIMUP:
			animate_val += animate_delta;
			cur_bl_val = (uint8_t) animate_val;
			if (cur_bl_val >= backlight_percent) {
				bl_state = GCORE_BL_NORMAL;
				auto_dim_timer = 0;
				if (cur_bl_val != backlight_percent) cur_bl_val = backlight_percent;
			}
			power_set_brightness(cur_bl_val);
			break;
		
		case GCORE_BL_DIMDN:
			animate_val += animate_delta;
			cur_bl_val = (uint8_t) animate_val;
			if (cur_bl_val <= GUI_BL_DIM_PERCENT) {
				bl_state = GCORE_BL_DIM;
				if (cur_bl_val < GUI_BL_DIM_PERCENT) cur_bl_val = GUI_BL_DIM_PERCENT;
			}
			power_set_brightness(cur_bl_val);
			break;
		
		case GCORE_BL_DIM:
			if (saw_activity) {
				// Setup to brighten
				saw_activity = false;
				bl_state = GCORE_BL_DIMUP;
				animate_val = GUI_BL_DIM_PERCENT;
				animate_delta = (float) ((backlight_percent - GUI_BL_DIM_PERCENT) / GCORE_BRT_STEPS);
			}
			break;
		
		default:
			bl_state = GCORE_BL_NORMAL;
			power_set_brightness(backlight_percent);
	}
}

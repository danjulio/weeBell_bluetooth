/*
 * Common items for all tasks
 *
 * Copyright (c) 2023 Dan Julio
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#ifndef _SYS_COMMON_
#define _SYS_COMMON_

#include "esp_gap_bt_api.h"
#include "freertos/task.h"


//
// Shared items for all tasks
//

// State transition diagnostic macro
#define STATE_CHANGE_PRINT(s1, s2, nameStr) {if ((int) s1 != (int) s2) {ESP_LOGI(TAG, "%s->%s", nameStr[s1], nameStr[s2]);}}

// Notification decode macro
#define Notification(var, mask) ((var & mask) == mask)

// Traditional Bluetooth pairing pin (used when SSP is not enabled)
// Note:
//  1. The first 4 digits are used for both 4- and 16-character pins
//  2. Make sure the string matches the first 4 digits of the pin (it's used when printing the value)
#define BLUETOOTH_PIN_ARRAY {'2', '1', '4', '3', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0'}
#define BLUETOOTH_PIN_STRING "2143"
extern const esp_bt_pin_code_t bt_trad_pin;   /* owned by bt_task */

// Caller ID String to use for GUI and Caller ID transmission for no number
#define UNKNOWN_CID_STRING "Unknown"

// Task handles used to send notifications between tasks (owned by app_task)
extern TaskHandle_t task_handle_app;
extern TaskHandle_t task_handle_audio;
extern TaskHandle_t task_handle_bt;
extern TaskHandle_t task_handle_gcore;
extern TaskHandle_t task_handle_gui;
extern TaskHandle_t task_handle_pots;

#endif /* _SYS_COMMON_ */
/*
 * App Task
 *
 * Contains the top-level logic involved in managing phone calls, coordinating
 * between the sub-tasks.  It also manages updating gain since we don't want to
 * run [slow] I2C cycles in audio_task and we get gain levels from both gui_task
 * and bt_task.
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
#ifndef APP_TASK_H
#define APP_TASK_H

#include <stdbool.h>
#include <stdint.h>

//
// App task constants
//

// Task evaluation period (mSec)
#define APP_EVAL_MSEC                        50

// Period to wait after each ring notification to determine unanswered incoming call is over
// (should be longer than longest period between two ring notifications from BT)
#define APP_LAST_RING_DETECT_MSEC            7000

// Period to wait after digit dialed on phone (but not GUI) before initiating a call
#define APP_LAST_DIGIT_2_DIAL_MSEC           4000

// Maximum number of dialed digits
//   Includes phone number and any subsequent keypresses (e.g. entering info)
#define APP_MAX_DIALED_DIGITS                256

// Notifications
#define APP_NOTIFY_POTS_ON_HOOK_MASK         0x00000001
#define APP_NOTIFY_POTS_OFF_HOOK_MASK        0x00000002
#define APP_NOTIFY_POTS_DIGIT_DIALED_MASK    0x00000010
#define APP_NOTIFY_GUI_DIGIT_DIALED_MASK     0x00000020
#define APP_NOTIFY_GUI_DIGIT_DELETED_MASK    0x00000040
#define APP_NOTIFY_GUI_DIAL_BTN_PRESSED_MASK 0x00000100

#define APP_NOTIFY_BT_IN_SERVICE_MASK        0x00001000
#define APP_NOTIFY_BT_OUT_OF_SERVICE_MASK    0x00002000
#define APP_NOTIFY_BT_RING_MASK              0x00010000
#define APP_NOTIFY_BT_CALL_STARTED_MASK      0x00020000
#define APP_NOTIFY_BT_CALL_ENDED_MASK        0x00040000
#define APP_NOTIFY_BT_CID_AVAILABLE_MASK     0x00080000
#define APP_NOTIFY_BT_AUDIO_START_MASK       0x00100000
#define APP_NOTIFY_BT_AUDIO_ENDED_MASK       0x00200000

#define APP_NOTIFY_NEW_GUI_MIC_GAIN_MASK     0x01000000
#define APP_NOTIFY_NEW_GUI_SPK_GAIN_MASK     0x02000000
#define APP_NOTIFY_NEW_BT_MIC_GAIN_MASK      0x04000000
#define APP_NOTIFY_NEW_BT_SPK_GAIN_MASK      0x08000000

#define APP_NOTIFY_START_AUDIO_SMPL_MASK     0x80000000

// App state
typedef enum {DISCONNECTED, CONNECTED_IDLE, CALL_RECEIVED, CALL_WAIT_ACTIVE, DIALING, CALL_INITIATED, 
			  CALL_ACTIVE, CALL_ACTIVE_VOICE, CALL_WAIT_END, CALL_WAIT_ONHOOK} app_state_t;



//
// App Task API
//
void app_task();
void app_set_gui_digit(char c);                      // Set by gui_task before sending notification
void app_set_pots_digit(char c);                     // Set by pots_task before sending notification
void app_set_cid_number(const char* pn);             // Set by bt_task before sending notification
int app_get_cur_number(char* pn, bool* is_dialed);   // Called to get the current phone number (dialed or CID)
app_state_t app_get_state();
void app_set_new_mic_gain(float g);                  // Called before sending BT_MIC_NEW_GAIN
void app_set_new_spk_gain(float g);                  // Called before sending BT_SPK_NEW_GAIN

#endif /* APP_TASK_H */
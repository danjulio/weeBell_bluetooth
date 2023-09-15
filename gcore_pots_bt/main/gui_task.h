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
#ifndef GUI_TASK_H
#define GUI_TASK_H

#include <stdbool.h>
#include <stdint.h>


//
// GUI Task Constants
//

// LVGL evaluation rate (mSec)
#define GUI_LVGL_TICK_MSEC         1
#define GUI_TASK_EVAL_MSEC         20

// Screen indicies
#define GUI_SCREEN_MAIN            0
#define GUI_SCREEN_SETTINGS        1
#define GUI_SCREEN_TIME            2

#define GUI_NUM_SCREENS            3

// Screen brightness values (integer percent)
//   MIN_PERCENT must be greater than DIM_PERCENT
#define GUI_BL_MAX_PERCENT         100
#define GUI_BL_MIN_PERCENT         50
#define GUI_BL_DIM_PERCENT         10

// Inactivity timeout (auto-dim timeout when enabled)
#define GUI_INACTIVITY_TO_MSEC     20000

// Dim animation time
#define GUI_DIM_MSEC               1000
#define GUI_BRT_MSEC               400

// Background color (should match theme background - A kludge, I know.  Specified here because
// themes don't allow direct access to it and IMHO the LVGL theme system is incredibly hard to use)
#define GUI_THEME_BG_COLOR         lv_color_hex(0x444b5a)
#define GUI_THEME_SLD_BG_COLOR     lv_color_hex(0x35393d)

// Maximum time to allow for pairing
#define GUI_MAX_PAIR_MSEC          60000

// Messagebox indicies
#define GUI_MSGBOX_INT_ERR         1
#define GUI_MSGBOX_BT_SSP          2
#define GUI_MSGBOX_BT_AUTH_FAIL    3
#define GUI_MSGBOX_CLR_PAIRING     4
#define GUI_MSGBOX_SMPL_FAIL       5
#define GUI_MSGBOX_SMPL_DONE       6

// Notifications
#define GUI_NOTIFY_POWER_UPDATE_MASK         0x00000001
#define GUI_NOTIFY_STATUS_UPDATE_MASK        0x00000002
#define GUI_NOTIFY_PH_NUM_UPDATE_MASK        0x00000004
#define GUI_NOTIFY_CID_NUM_UPDATE_MASK       0x00000008
#define GUI_NOTIFY_UPDATE_MIC_GAIN_MASK      0x00000010
#define GUI_NOTIFY_UPDATE_SPK_GAIN_MASK      0x00000020
#define GUI_NOTIFY_NEW_SSP_PIN_MASK          0x00000100
#define GUI_NOTIFY_NEW_PAIR_INFO_MASK        0x00000200
#define GUI_NOTIFY_FORGET_PAIRING_MASK       0x00000400
#define GUI_NOTIFY_BT_AUTH_FAIL_MASK         0x00001000
#define GUI_NOTIFY_MESSAGEBOX_MASK           0x10000000
#define GUI_NOTIFY_SCREENDUMP_MASK           0x80000000



//
// GUI Task API
//

// API calls for other tasks
void gui_task();
void gui_set_screen(int n);                             // Used by GUI screens to switch between themselves
void gui_set_msgbox_btn(int id, uint16_t btn);          // Used by a GUI messagebox to report back which button was pressed
void gui_set_new_mic_gain(float g);                     // Used by another task before notification to update controls/ps
void gui_set_new_spk_gain(float g);
void gui_set_new_pair_ssp_pin(uint32_t ssp_pin);        // Used by bt_task to inform us of a new pin for user to compare with cellphone
void gui_set_new_pair_info(uint8_t* addr, char* name);  // Used by bt_task to inform us of a new pairing
void gui_set_fatal_error(const char* msg);              // Used by any code to log a fatal error that will display a pop-up
                                                        //   message and then shut down the device

#endif /* GUI_TASK_H */
/*
 * Set Time GUI screen related functions, callbacks and event handlers
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
#ifndef GUI_SCREEN_TIME_H_
#define GUI_SCREEN_TIME_H_

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"


//
// Set Time GUI Screen Constants
//

// Back control
#define TIME_BCK_BTN_LEFT_X    10
#define TIME_BCK_BTN_TOP_Y     5
#define TIME_BCK_BTN_W         50
#define TIME_BCK_BTN_H         50

// Screen label (centered)
#define TIME_SCR_LBL_LEFT_X    60
#define TIME_SCR_LBL_TOP_Y     20
#define TIME_SCR_LBL_W         200

// Time/Date label (centered in X)
#define TIME_TD_LEFT_X         0
#define TIME_TD_TOP_Y          70
#define TIME_TD_W              320

// Button Matrix
#define TIME_BTN_MATRIX_LEFT_X 20
#define TIME_BTN_MATRIX_TOP_Y  110
#define TIME_BTN_MATRIX_W      280
#define TIME_BTN_MATRIX_H      320

// Save Button
#define TIME_SAVE_BTN_LEFT_X   120
#define TIME_SAVE_BTN_TOP_Y    420
#define TIME_SAVE_BTN_W        80
#define TIME_SAVE_BTN_H        50

//
// Set Time GUI Screen API
//
lv_obj_t* gui_screen_time_create();
void gui_screen_time_set_active(bool en);

#endif /* GUI_SCREEN_TIME_H_ */
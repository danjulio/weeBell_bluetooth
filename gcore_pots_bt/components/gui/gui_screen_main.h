/*
 * Main GUI screen related functions, callbacks and event handlers
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
 * along with life.  If not, see <https://www.gnu.org/licenses/>.
 *
 */
#ifndef GUI_SCREEN_MAIN_H_
#define GUI_SCREEN_MAIN_H_

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

//
// Main Screen Constants
//   (0, 0) is upper-left
//

// Battery/Charge status label
#define MAIN_BATT_LEFT_X     20
#define MAIN_BATT_TOP_Y      10

// Status label (centered in X)
#define MAIN_STAT_LEFT_X     30
#define MAIN_STAT_TOP_Y      10
#define MAIN_STAT_W          260

// Bluetooth connection status label
#define MAIN_BT_LEFT_X       285
#define MAIN_BT_TOP_Y        10

// Phone number label (centered in X)
#define MAIN_PH_NUM_LEFT_X   0
#define MAIN_PH_NUM_TOP_Y    40
#define MAIN_PH_NUM_W        320
#define MAIN_PH_NUM_H        50

// Mute button
#define MAIN_MUTE_LEFT_X     10
#define MAIN_MUTE_TOP_Y      90
#define MAIN_MUTE_W          80
#define MAIN_MUTE_H          40

// Do Not Disturb button
#define MAIN_DND_LEFT_X      160
#define MAIN_DND_TOP_Y       90
#define MAIN_DND_W           140
#define MAIN_DND_H           40

// Dialing keypad
#define MAIN_KEYP_LEFT_X     10
#define MAIN_KEYP_TOP_Y      135
#define MAIN_KEYP_W          300
#define MAIN_KEYP_H          256

// Settings button
#define MAIN_SETTINGS_LEFT_X 40
#define MAIN_SETTINGS_TOP_Y  406
#define MAIN_SETTINGS_W      50
#define MAIN_SETTINGS_H      50


// Dial button
#define MAIN_DIAL_LEFT_X     130
#define MAIN_DIAL_TOP_Y      400
#define MAIN_DIAL_W          60
#define MAIN_DIAL_H          60

// Backspace button
#define MAIN_BCKSP_LEFT_X    215
#define MAIN_BCKSP_TOP_Y     406
#define MAIN_BCKSP_W         80
#define MAIN_BCKSP_H         50



//
// Main Screen API
//
lv_obj_t* gui_screen_main_create();
void gui_screen_main_set_active(bool en);
void gui_screen_main_update_power_state();
void gui_screen_main_update_status();
void gui_screen_main_update_ph_num();
void gui_screen_main_update_cid_num();

#endif /* GUI_SCREEN_MAIN_H_ */
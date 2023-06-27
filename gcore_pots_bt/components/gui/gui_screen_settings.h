/*
 * Settings GUI screen related functions, callbacks and event handlers
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
#ifndef GUI_SCREEN_SETTINGS_H_
#define GUI_SCREEN_SETTINGS_H_

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

//
// Settings Screen Constants
//   (0, 0) is upper-left
//

// Back control
#define SETTINGS_BCK_BTN_LEFT_X 10
#define SETTINGS_BCK_BTN_TOP_Y  5
#define SETTINGS_BCK_BTN_W      50
#define SETTINGS_BCK_BTN_H      50

// Screen label (centered)
#define SETTINGS_SCR_LBL_LEFT_X 60
#define SETTINGS_SCR_LBL_TOP_Y  20
#define SETTINGS_SCR_LBL_W      200

// Bluetooth controls
#define SETTINGS_BT_LBL_LEFT_X  20
#define SETTINGS_BT_LBL_TOP_Y   70

#define SETTINGS_BT_BTN_LEFT_X  210
#define SETTINGS_BT_BTN_TOP_Y   70
#define SETTINGS_BT_BTN_W       90
#define SETTINGS_BT_BTN_H       30

#define SETTINGS_BT_STAT_LEFT_X 20
#define SETTINGS_BT_STAT_TOP_Y  90

// Backlight controls
#define SETTINGS_BL_LBL_LEFT_X  20
#define SETTINGS_BL_LBL_TOP_Y   130

#define SETTINGS_BL_SLD_LEFT_X  120
#define SETTINGS_BL_SLD_TOP_Y   130
#define SETTINGS_BL_SLD_W       180
#define SETTINGS_BL_SLD_H       20

#define SETTINGS_AD_LBL_LEFT_X  20
#define SETTINGS_AD_LBL_TOP_Y   170

#define SETTINGS_AD_SW_LEFT_X   220
#define SETTINGS_AD_SW_TOP_Y    170
#define SETTINGS_AD_SW_W        70
#define SETTINGS_AD_SW_H        25

// Country selection control
#define SETTINGS_CN_LBL_LEFT_X  20
#define SETTINGS_CN_LBL_TOP_Y   240

#define SETTINGS_CN_DD_LEFT_X   120
#define SETTINGS_CN_DD_TOP_Y    230
#define SETTINGS_CN_DD_W        180
#define SETTINGS_CN_DD_H        40

// Audio gain controls
#define SETTINGS_MIC_LBL_LEFT_X 20
#define SETTINGS_MIC_LBL_TOP_Y  310

#define SETTINGS_MIC_SLD_LEFT_X 120
#define SETTINGS_MIC_SLD_TOP_Y  310
#define SETTINGS_MIC_SLD_W      180
#define SETTINGS_MIC_SLD_H      20

#define SETTINGS_SPK_LBL_LEFT_X 20
#define SETTINGS_SPK_LBL_TOP_Y  370

#define SETTINGS_SPK_SLD_LEFT_X 120
#define SETTINGS_SPK_SLD_TOP_Y  370
#define SETTINGS_SPK_SLD_W      180
#define SETTINGS_SPK_SLD_H      20

// Version info (right justified)
#define SETTINGS_VER_LBL_LEFT_X 230
#define SETTINGS_VER_LBL_TOP_Y  450
#define SETTINGS_VER_LBL_W      70

// Audio sample trigger button (only displayed when CONFIG_AUDIO_SAMPLE_ENABLE defined)
#define SETTINGS_SMPL_BTN_LEFT_X 20
#define SETTINGS_SMPL_BTN_TOP_Y  450
#define SETTINGS_SMPL_BTN_W      40
#define SETTINGS_SMPL_BTN_H      30



//
// Settings Screen API
//
lv_obj_t* gui_screen_settings_create();
void gui_screen_settings_set_active(bool en);
void gui_screen_settings_update_mic_gain(float g);
void gui_screen_settings_update_spk_gain(float g);
void gui_screen_settings_update_peer_info(uint8_t* addr, char* name);
void gui_screen_settings_forget_peer_info();

#endif /* GUI_SCREEN_SETTINGS_H_ */
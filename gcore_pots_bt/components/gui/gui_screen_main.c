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
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gui_screen_main.h"
#include "app_task.h"
#include "audio_task.h"
#include "gcore_task.h"
#include "gui_task.h"
#include "pots_task.h"
#include "power_utilities.h"
#include "sys_common.h"
#include <stdio.h>


//
// Constants
//


// Mute / DND active color
#define BTN_ACTIVE_COLOR LV_COLOR_RED



//
// Static images
//
LV_IMG_DECLARE(phone_dial_60_60);
LV_IMG_DECLARE(phone_dial_60_60_pressed);
LV_IMG_DECLARE(phone_hangup_60_60);
LV_IMG_DECLARE(phone_hangup_60_60_pressed);

//
// Variables
//

// LVGL widget objects
static lv_obj_t* screen;
static lv_obj_t* lbl_batt_info;
static lv_obj_t* lbl_status;
static lv_obj_t* lbl_bt_info;
static lv_obj_t* lbl_phone_num;
static lv_obj_t* btn_mute;
static lv_obj_t* lbl_btn_mute;
static lv_obj_t* btn_dnd;
static lv_obj_t* lbl_btn_dnd;
static lv_obj_t* kbd_dial;
static lv_obj_t* btn_settings;
static lv_obj_t* lbl_btn_settings;
static lv_obj_t* btn_dial;
static lv_obj_t* btn_backspace;
static lv_obj_t* lbl_btn_backspace;

// Dial keypad array
static const char* keyp_map[] = {"1", "2", "3", "\n",
                                 "4", "5", "6", "\n",
                                 "7", "8", "9", "\n",
                                 "*", "0", "#", ""};

static const char keyp_vals[] = {'1', '2', '3',
                                 '4', '5', '6',
                                 '7', '8', '9',
                                 '*', '0', '#'};

// Screen state
static bool enable_mute = false;
static bool enable_dnd = false;

static char phone_num[APP_MAX_DIALED_DIGITS+1];   // Statically allocated phone number buffer


//
// Forward declarations for internal functions
//
static void _cb_mute_btn(lv_obj_t* obj, lv_event_t event);
static void _cb_dnd_btn(lv_obj_t* obj, lv_event_t event);
static void _cb_keyp(lv_obj_t* obj, lv_event_t event);
static void _cb_settings_btn(lv_obj_t* obj, lv_event_t event);
static void _cb_dial_btn(lv_obj_t* obj, lv_event_t event);
static void _cb_bcksp_btn(lv_obj_t* obj, lv_event_t event);
#if (CONFIG_SCREENDUMP_ENABLE == true)
static void _cb_batt_info_btn(lv_obj_t* obj, lv_event_t event);
#endif



//
// API
//
lv_obj_t* gui_screen_main_create()
{
	// Create screen object
	screen = lv_obj_create(NULL, NULL);
	
	// Create the widgets for this screen
	//
	
	// Battery & Charge status label
	lbl_batt_info = lv_label_create(screen, NULL);
	lv_obj_set_pos(lbl_batt_info, MAIN_BATT_LEFT_X, MAIN_BATT_TOP_Y);
	lv_label_set_static_text(lbl_batt_info, LV_SYMBOL_BATTERY_EMPTY);
#if (CONFIG_SCREENDUMP_ENABLE == true)
	// Touching label initiates screendump
	lv_obj_set_click(lbl_batt_info, true);
	lv_obj_set_event_cb(lbl_batt_info, _cb_batt_info_btn);
#endif
	
	// Status label
	lbl_status = lv_label_create(screen, NULL);
	lv_label_set_long_mode(lbl_status, LV_LABEL_LONG_BREAK);
	lv_label_set_align(lbl_status, LV_LABEL_ALIGN_CENTER);
	lv_obj_set_pos(lbl_status, MAIN_STAT_LEFT_X, MAIN_STAT_TOP_Y);
	lv_obj_set_width(lbl_status, MAIN_STAT_W);
	lv_label_set_static_text(lbl_status, "No Service");
	
	// Bluetooth connection status label
	lbl_bt_info = lv_label_create(screen, NULL);
	lv_obj_set_pos(lbl_bt_info, MAIN_BT_LEFT_X, MAIN_BT_TOP_Y);
	lv_obj_set_style_local_text_color(lbl_bt_info, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLUE);
	lv_label_set_static_text(lbl_bt_info, "");
	
	// Phone number display label
	lbl_phone_num = lv_label_create(screen, NULL);
	lv_label_set_long_mode(lbl_phone_num, LV_LABEL_LONG_SROLL_CIRC);
	lv_label_set_align(lbl_phone_num, LV_LABEL_ALIGN_CENTER);
	lv_obj_set_pos(lbl_phone_num, MAIN_PH_NUM_LEFT_X, MAIN_PH_NUM_TOP_Y);
	lv_obj_set_size(lbl_phone_num, MAIN_PH_NUM_W, MAIN_PH_NUM_H);
	lv_obj_set_style_local_text_font(lbl_phone_num, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &lv_font_montserrat_38);
	lv_obj_set_style_local_text_color(lbl_phone_num, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_CYAN);
	lv_label_set_static_text(lbl_phone_num, "");
	
	// Mute button
	btn_mute = lv_btn_create(screen, NULL);
	lv_obj_set_pos(btn_mute, MAIN_MUTE_LEFT_X, MAIN_MUTE_TOP_Y);
	lv_obj_set_size(btn_mute, MAIN_MUTE_W, MAIN_MUTE_H);
	lv_obj_set_style_local_bg_color(btn_mute, LV_BTN_PART_MAIN, LV_STATE_PRESSED, GUI_THEME_BG_COLOR);
	lv_obj_set_style_local_border_color(btn_mute, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, GUI_THEME_BG_COLOR);
	lv_obj_set_style_local_bg_color(btn_mute, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, GUI_THEME_BG_COLOR);
	lv_obj_set_style_local_border_color(btn_mute, LV_BTN_PART_MAIN, LV_STATE_PRESSED, lv_theme_get_color_secondary());
	lv_obj_set_event_cb(btn_mute, _cb_mute_btn);
	
	lbl_btn_mute = lv_label_create(btn_mute, NULL);
	lv_label_set_static_text(lbl_btn_mute, "Mute");
	
	// Do Not Disturb button
	btn_dnd = lv_btn_create(screen, NULL);
	lv_obj_set_pos(btn_dnd, MAIN_DND_LEFT_X, MAIN_DND_TOP_Y);
	lv_obj_set_size(btn_dnd, MAIN_DND_W, MAIN_DND_H);
	lv_obj_set_style_local_bg_color(btn_dnd, LV_BTN_PART_MAIN, LV_STATE_PRESSED, GUI_THEME_BG_COLOR);
	lv_obj_set_style_local_border_color(btn_dnd, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, GUI_THEME_BG_COLOR);
	lv_obj_set_style_local_bg_color(btn_dnd, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, GUI_THEME_BG_COLOR);
	lv_obj_set_style_local_border_color(btn_dnd, LV_BTN_PART_MAIN, LV_STATE_PRESSED, lv_theme_get_color_secondary());
	lv_obj_set_event_cb(btn_dnd, _cb_dnd_btn);
	
	lbl_btn_dnd = lv_label_create(btn_dnd, NULL);
	lv_label_set_static_text(lbl_btn_dnd, "Do Not Disturb");
	
	// Dialing keypad
	kbd_dial = lv_btnmatrix_create(screen, NULL);
	lv_obj_set_pos(kbd_dial, MAIN_KEYP_LEFT_X, MAIN_KEYP_TOP_Y);
	lv_obj_set_size(kbd_dial, MAIN_KEYP_W, MAIN_KEYP_H);
	lv_btnmatrix_set_map(kbd_dial, keyp_map);
	lv_obj_set_style_local_text_font(kbd_dial, LV_BTNMATRIX_PART_BTN, LV_STATE_DEFAULT, &lv_font_montserrat_34);
	lv_obj_set_style_local_border_color(kbd_dial, LV_BTNMATRIX_PART_BTN, LV_STATE_DEFAULT, GUI_THEME_BG_COLOR);
	lv_obj_set_style_local_bg_color(kbd_dial, LV_BTNMATRIX_PART_BTN, LV_STATE_DEFAULT, GUI_THEME_BG_COLOR);
	lv_obj_set_style_local_bg_color(kbd_dial, LV_BTNMATRIX_PART_BTN, LV_STATE_PRESSED, GUI_THEME_BG_COLOR);
	lv_obj_set_style_local_border_color(kbd_dial, LV_BTNMATRIX_PART_BG, LV_STATE_DEFAULT, GUI_THEME_BG_COLOR);
	lv_obj_set_style_local_bg_color(kbd_dial, LV_BTNMATRIX_PART_BG, LV_STATE_DEFAULT, GUI_THEME_BG_COLOR);
	lv_obj_set_event_cb(kbd_dial, _cb_keyp);
	
	// Settings button
	btn_settings = lv_btn_create(screen, NULL);
	lv_obj_set_pos(btn_settings, MAIN_SETTINGS_LEFT_X, MAIN_SETTINGS_TOP_Y);
	lv_obj_set_size(btn_settings, MAIN_SETTINGS_W, MAIN_SETTINGS_H);
	lv_obj_set_style_local_text_font(btn_settings, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &lv_font_montserrat_34);
	lv_obj_set_style_local_bg_color(btn_settings, LV_BTN_PART_MAIN, LV_STATE_PRESSED, GUI_THEME_BG_COLOR);
	lv_obj_set_style_local_border_color(btn_settings, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, GUI_THEME_BG_COLOR);
	lv_obj_set_style_local_bg_color(btn_settings, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, GUI_THEME_BG_COLOR);
	lv_obj_set_style_local_border_color(btn_settings, LV_BTN_PART_MAIN, LV_STATE_PRESSED, lv_theme_get_color_secondary());
	lv_obj_set_event_cb(btn_settings, _cb_settings_btn);
	
	lbl_btn_settings = lv_label_create(btn_settings, NULL);
	lv_label_set_static_text(lbl_btn_settings, LV_SYMBOL_SETTINGS);
	
	// Dial button
	btn_dial = lv_imgbtn_create(screen, NULL);
	lv_obj_set_pos(btn_dial, MAIN_DIAL_LEFT_X, MAIN_DIAL_TOP_Y);
	lv_obj_set_size(btn_dial, MAIN_DIAL_W, MAIN_DIAL_H);
	lv_imgbtn_set_src(btn_dial, LV_BTN_STATE_PRESSED, &phone_dial_60_60_pressed);
	lv_imgbtn_set_src(btn_dial, LV_BTN_STATE_RELEASED, &phone_dial_60_60);
	lv_obj_set_event_cb(btn_dial, _cb_dial_btn);
	
	// Backspace button
	btn_backspace = lv_btn_create(screen, NULL);
	lv_obj_set_pos(btn_backspace, MAIN_BCKSP_LEFT_X, MAIN_BCKSP_TOP_Y);
	lv_obj_set_size(btn_backspace, MAIN_BCKSP_W, MAIN_BCKSP_H);
	lv_obj_set_style_local_text_font(btn_backspace, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &lv_font_montserrat_34);
	lv_obj_set_style_local_bg_color(btn_backspace, LV_BTN_PART_MAIN, LV_STATE_PRESSED, GUI_THEME_BG_COLOR);
	lv_obj_set_style_local_border_color(btn_backspace, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, GUI_THEME_BG_COLOR);
	lv_obj_set_style_local_bg_color(btn_backspace, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, GUI_THEME_BG_COLOR);
	lv_obj_set_style_local_border_color(btn_backspace, LV_BTN_PART_MAIN, LV_STATE_PRESSED, lv_theme_get_color_secondary());
	lv_obj_set_event_cb(btn_backspace, _cb_bcksp_btn);
	
	lbl_btn_backspace = lv_label_create(btn_backspace, NULL);
	lv_label_set_static_text(lbl_btn_backspace, LV_SYMBOL_BACKSPACE);
	
	return screen;
}


void gui_screen_main_set_active(bool en)
{
	// Control our visibility
	lv_obj_set_hidden(screen, !en);
}


void gui_screen_main_update_power_state()
{
	static char batt_buf[8];    // batt icon (3) + ' ' + charge icon (3) + null
	enum BATT_STATE_t batt_state;
	enum CHARGE_STATE_t charge_state;
	static enum BATT_STATE_t prev_batt_state = BATT_0;
	static enum CHARGE_STATE_t prev_charge_state = CHARGE_OFF;
	
	gcore_get_power_state(&batt_state, &charge_state);
		
	if ((batt_state != prev_batt_state) || (charge_state != prev_charge_state)) {
		memset(batt_buf, 0, sizeof(batt_buf));
		
		switch (batt_state) {
			case BATT_100:
				strcpy(&batt_buf[0], LV_SYMBOL_BATTERY_FULL);
				break;
			case BATT_75:
				strcpy(&batt_buf[0], LV_SYMBOL_BATTERY_3);
				break;
			case BATT_50:
				strcpy(&batt_buf[0], LV_SYMBOL_BATTERY_2);
				break;
			case BATT_25:
				strcpy(&batt_buf[0], LV_SYMBOL_BATTERY_1);
				break;
			default:
				strcpy(&batt_buf[0], LV_SYMBOL_BATTERY_EMPTY);
		}
		
		batt_buf[3] = ' ';
		
		if (charge_state == CHARGE_ON) {
			strcpy(&batt_buf[4], LV_SYMBOL_CHARGE);
		} else if (charge_state == CHARGE_FAULT) {
			strcpy(&batt_buf[4], LV_SYMBOL_WARNING);
		}
		
		lv_label_set_static_text(lbl_batt_info, batt_buf);
		
		prev_batt_state = batt_state;
		prev_charge_state = charge_state;
	}
}


void gui_screen_main_update_status()
{
	app_state_t cur_state;
	bool disp_bt_icon = false;
	bool disp_hu_icon = false;
	static bool prev_disp_bt_icon = false;
	static bool prev_disp_hu_icon = false;
	static char status_buf[17];     // Sized for largest string below + null
	
	cur_state = app_get_state();
	
	memset(status_buf, 0, sizeof(status_buf));
	
	switch (cur_state) {
		case DISCONNECTED:
			disp_bt_icon = false;
			disp_hu_icon = false;
			sprintf(status_buf, "No Service");
			break;
		case CONNECTED_IDLE:
			// No text displayed
			disp_bt_icon = true;
			disp_hu_icon = false;
			break;
		case CALL_RECEIVED:
			disp_bt_icon = true;
			disp_hu_icon = true;
			sprintf(status_buf, "Incoming Call");
			break;
		case CALL_WAIT_ACTIVE:
			disp_bt_icon = true;
			disp_hu_icon = true;
			sprintf(status_buf, "Incoming Call");
			break;
		case DIALING:
			disp_bt_icon = true;
			disp_hu_icon = false;
			sprintf(status_buf, "Dial Number");
			break;
		case CALL_INITIATED:
			disp_bt_icon = true;
			disp_hu_icon = true;
			sprintf(status_buf, "Calling...");
			break;
		case CALL_ACTIVE:
		case CALL_ACTIVE_VOICE:
			disp_bt_icon = true;
			disp_hu_icon = true;
			sprintf(status_buf, "Call in Progress");
			break;
		case CALL_WAIT_END:
			disp_bt_icon = true;
			disp_hu_icon = true;
			sprintf(status_buf, "Call Ending...");
			break;
		case CALL_WAIT_ONHOOK:
			disp_bt_icon = true;
			disp_hu_icon = true;
			sprintf(status_buf, "Call Ended");
			break;
	}
	lv_label_set_static_text(lbl_status, status_buf);
	
	if (disp_bt_icon != prev_disp_bt_icon) {
		lv_label_set_static_text(lbl_bt_info, disp_bt_icon ? LV_SYMBOL_BLUETOOTH : "");
		prev_disp_bt_icon = disp_bt_icon;
	}
	
	if (disp_hu_icon != prev_disp_hu_icon) {
		if (disp_hu_icon) {
			lv_imgbtn_set_src(btn_dial, LV_BTN_STATE_PRESSED, &phone_hangup_60_60_pressed);
			lv_imgbtn_set_src(btn_dial, LV_BTN_STATE_RELEASED, &phone_hangup_60_60);
		} else {
			lv_imgbtn_set_src(btn_dial, LV_BTN_STATE_PRESSED, &phone_dial_60_60_pressed);
			lv_imgbtn_set_src(btn_dial, LV_BTN_STATE_RELEASED, &phone_dial_60_60);
		}
		prev_disp_hu_icon = disp_hu_icon;
	}
}


void gui_screen_main_update_ph_num()
{
	bool is_dialed;
	int n;
	
	n = app_get_cur_number(phone_num, &is_dialed);
	
	if (n == 0) {
		lv_label_set_static_text(lbl_phone_num, "");
	} else {
		if (is_dialed) {
			lv_obj_set_style_local_text_color(lbl_phone_num, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_CYAN);
		} else {
			lv_obj_set_style_local_text_color(lbl_phone_num, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_YELLOW);
		}
		lv_label_set_static_text(lbl_phone_num, phone_num);
	}
}



//
// Internal functions
//
static void _cb_mute_btn(lv_obj_t* obj, lv_event_t event)
{
	if (event == LV_EVENT_CLICKED) {
		enable_mute = !enable_mute;
		if (enable_mute) {
			lv_obj_set_style_local_text_color(lbl_btn_mute, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_RED);
			xTaskNotify(task_handle_audio, AUDIO_NOTIFY_MUTE_MIC_MASK, eSetBits);
		} else {
			lv_obj_set_style_local_text_color(lbl_btn_mute, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
			xTaskNotify(task_handle_audio, AUDIO_NOTIFY_UNMUTE_MIC_MASK, eSetBits);
		}
	}
}


static void _cb_dnd_btn(lv_obj_t* obj, lv_event_t event)
{
	if (event == LV_EVENT_CLICKED) {
		enable_dnd = !enable_dnd;
		if (enable_dnd) {
			lv_obj_set_style_local_text_color(lbl_btn_dnd, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_RED);
			xTaskNotify(task_handle_pots, POTS_NOTIFY_MUTE_RING_MASK, eSetBits);
		} else {
			lv_obj_set_style_local_text_color(lbl_btn_dnd, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
			xTaskNotify(task_handle_pots, POTS_NOTIFY_UNMUTE_RING_MASK, eSetBits);
		}
	}
}


static void _cb_keyp(lv_obj_t* obj, lv_event_t event)
{
	uint16_t n;
	
	if (event == LV_EVENT_VALUE_CHANGED) {
		n = lv_btnmatrix_get_active_btn(obj);
		
		if (n != LV_BTNMATRIX_BTN_NONE) {
			app_set_gui_digit(keyp_vals[n]);
			xTaskNotify(task_handle_app, APP_NOTIFY_GUI_DIGIT_DIALED_MASK, eSetBits);
		}
	}
}


static void _cb_settings_btn(lv_obj_t* obj, lv_event_t event)
{
	if (event == LV_EVENT_CLICKED) {
		gui_set_screen(GUI_SCREEN_SETTINGS);
	}
}


static void _cb_dial_btn(lv_obj_t* obj, lv_event_t event)
{
	if (event == LV_EVENT_CLICKED) {
		xTaskNotify(task_handle_app, APP_NOTIFY_GUI_DIAL_BTN_PRESSED_MASK, eSetBits);
	}
}


static void _cb_bcksp_btn(lv_obj_t* obj, lv_event_t event)
{
	if (event == LV_EVENT_CLICKED) {
		xTaskNotify(task_handle_app, APP_NOTIFY_GUI_DIGIT_DELETED_MASK, eSetBits);
	}
}


#if (CONFIG_SCREENDUMP_ENABLE == true)
static void _cb_batt_info_btn(lv_obj_t* obj, lv_event_t event)
{
	if (event == LV_EVENT_CLICKED) {
		xTaskNotify(task_handle_gui, GUI_NOTIFY_SCREENDUMP_MASK, eSetBits);
	}
}
#endif

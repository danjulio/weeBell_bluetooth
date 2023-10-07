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
#include "esp_system.h"
#include "esp_app_format.h"
#include "esp_gap_bt_api.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gui_screen_settings.h"
#include "app_task.h"
#include "bt_task.h"
#include "gcore_task.h"
#include "gui_task.h"
#include "pots_task.h"
#include "gain.h"
#include "gui_utilities.h"
#include "international.h"
#include "power_utilities.h"
#include "ps.h"
#include "sys_common.h"
#include <math.h>
#include <stdio.h>



//
// Constants
//


//
// Variables
//

static const char* TAG = "gui_screen_settings";

// LVGL widget objects
static lv_obj_t* screen;
static lv_obj_t* btn_bck;
static lv_obj_t* btn_bck_lbl;
static lv_obj_t* lbl_screen;
static lv_obj_t* lbl_ver;
static lv_obj_t* lbl_bt;
static lv_obj_t* btn_bt;
static lv_obj_t* btn_bt_lbl;
static lv_obj_t* lbl_bt_status;
static lv_obj_t* lbl_bl;
static lv_obj_t* sld_bl;
static lv_obj_t* lbl_ad;
static lv_obj_t* sw_ad;
static lv_obj_t* lbl_cn;
static lv_obj_t* dd_cn;
static lv_obj_t* lbl_mic;
static lv_obj_t* sld_mic;
static lv_obj_t* lbl_spk;
static lv_obj_t* sld_spk;
static lv_obj_t* lbl_time;
static lv_obj_t* btn_time;
static lv_obj_t* btn_time_lbl;

#if (CONFIG_AUDIO_SAMPLE_ENABLE == true)
static lv_obj_t* btn_smpl;
static lv_obj_t* btn_smpl_lbl;
#endif

// LVGL timers
static lv_task_t* pair_timer_task;

// Screen state
static bool screen_is_active;
static bool cur_is_paired;
static bool cur_auto_dim;
static bool pairing_in_process = false;
static char cur_paired_name[ESP_BT_GAP_MAX_BDNAME_LEN+1];
static uint8_t cur_brightness;
static uint8_t cur_country_code;
static float cur_mic_gain;
static float cur_spk_gain;
// Note about gain and gain sliders: Since gain is a floating point number but with a fairly restricted range,
// and the slider control is integer based we scale the conversion from float to int by 10,

static bool update_ps_ram = false;    // Set when any PS value is changed so we can
                                      // tell the ps module to write the changes to battery
                                      // backed RAM when we leave the screen

// Country List array for drop down
static char* country_list = NULL;


//
// Forward declarations for internal functions
//
static void _get_country_list();
static int16_t _gain_to_sld_int(int gain_type, float g);
static void _cb_bck_btn(lv_obj_t* obj, lv_event_t event);
static void _cb_bt_btn(lv_obj_t* obj, lv_event_t event);
static void _cb_bl_sld(lv_obj_t* obj, lv_event_t event);
static void _sw_ad_cb(lv_obj_t* obj, lv_event_t event);
static void _cb_cn_dd(lv_obj_t* obj, lv_event_t event);
static void _cb_gain_sld(lv_obj_t* obj, lv_event_t event);
static void _cb_set_time(lv_obj_t* obj, lv_event_t event);
static void _cb_pair_timer_task(lv_task_t* task);
static void _start_pairing();
static void _stop_pairing();
#if (CONFIG_AUDIO_SAMPLE_ENABLE == true)
static void _cb_smpl_btn(lv_obj_t* obj, lv_event_t event);
#endif


//
// API
//
lv_obj_t* gui_screen_settings_create()
{
	static const esp_app_desc_t* app_desc;
	static char ver_buf[34];                  // 'v' + app_desc->version[32] + null
	
	screen = lv_obj_create(NULL, NULL);
	
	// Create the widgets for this screen
	//
	
	// Back control button
	btn_bck = lv_btn_create(screen, NULL);
	lv_obj_set_pos(btn_bck, SETTINGS_BCK_BTN_LEFT_X, SETTINGS_BCK_BTN_TOP_Y);
	lv_obj_set_size(btn_bck, SETTINGS_BCK_BTN_W, SETTINGS_BCK_BTN_H);
	lv_obj_set_style_local_bg_color(btn_bck, LV_BTN_PART_MAIN, LV_STATE_PRESSED, GUI_THEME_BG_COLOR);
	lv_obj_set_style_local_border_color(btn_bck, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, GUI_THEME_BG_COLOR);
	lv_obj_set_style_local_bg_color(btn_bck, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, GUI_THEME_BG_COLOR);
	lv_obj_set_style_local_border_color(btn_bck, LV_BTN_PART_MAIN, LV_STATE_PRESSED, lv_theme_get_color_secondary());
	lv_obj_set_event_cb(btn_bck, _cb_bck_btn);
	
	btn_bck_lbl = lv_label_create(btn_bck, NULL);
	lv_obj_set_style_local_text_font(btn_bck_lbl, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &lv_font_montserrat_34);
	lv_label_set_static_text(btn_bck_lbl, LV_SYMBOL_LEFT);
	
	// Screen label
	lbl_screen = lv_label_create(screen, NULL);
	lv_label_set_long_mode(lbl_screen, LV_LABEL_LONG_BREAK);
	lv_label_set_align(lbl_screen, LV_LABEL_ALIGN_CENTER);
	lv_obj_set_pos(lbl_screen, SETTINGS_SCR_LBL_LEFT_X, SETTINGS_SCR_LBL_TOP_Y);
	lv_obj_set_width(lbl_screen, SETTINGS_SCR_LBL_W);
	lv_obj_set_style_local_text_font(lbl_screen, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &lv_font_montserrat_20);
	lv_label_set_static_text(lbl_screen, "Settings");
		
	// Version label
	lbl_ver = lv_label_create(screen, NULL);
	lv_obj_set_pos(lbl_ver, SETTINGS_VER_LBL_LEFT_X, SETTINGS_VER_LBL_TOP_Y);
	lv_label_set_long_mode(lbl_ver, LV_LABEL_LONG_BREAK);
	lv_label_set_align(lbl_ver, LV_LABEL_ALIGN_RIGHT);
	lv_obj_set_width(lbl_ver, SETTINGS_VER_LBL_W);
	app_desc = esp_ota_get_app_description();
	sprintf(ver_buf, "v%s", app_desc->version);
	lv_label_set_static_text(lbl_ver, ver_buf);
	
	// Bluetooth controls label
	lbl_bt = lv_label_create(screen, NULL);
	lv_obj_set_pos(lbl_bt, SETTINGS_BT_LBL_LEFT_X, SETTINGS_BT_LBL_TOP_Y);
	lv_label_set_static_text(lbl_bt, "Bluetooth");
	
	// Bluetooth control button
	btn_bt = lv_btn_create(screen, NULL);
	lv_obj_set_pos(btn_bt, SETTINGS_BT_BTN_LEFT_X, SETTINGS_BT_BTN_TOP_Y);
	lv_obj_set_size(btn_bt, SETTINGS_BT_BTN_W, SETTINGS_BT_BTN_H);
	lv_obj_set_style_local_bg_color(btn_bt, LV_BTN_PART_MAIN, LV_STATE_PRESSED, GUI_THEME_BG_COLOR);
	lv_obj_set_style_local_border_color(btn_bt, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, GUI_THEME_BG_COLOR);
	lv_obj_set_style_local_bg_color(btn_bt, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, GUI_THEME_BG_COLOR);
	lv_obj_set_style_local_border_color(btn_bt, LV_BTN_PART_MAIN, LV_STATE_PRESSED, lv_theme_get_color_secondary());
	lv_obj_set_event_cb(btn_bt, _cb_bt_btn);
	
	btn_bt_lbl = lv_label_create(btn_bt, NULL);
	lv_label_set_static_text(btn_bt_lbl, "Pair");
	
	// Bluetooth pair status
	lbl_bt_status = lv_label_create(screen, NULL);
	lv_obj_set_pos(lbl_bt_status, SETTINGS_BT_STAT_LEFT_X, SETTINGS_BT_STAT_TOP_Y);
	lv_obj_set_style_local_text_font(lbl_bt_status, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &lv_font_montserrat_14);
	lv_label_set_static_text(lbl_bt_status, "");
	
	// Backlight dimmer control label
	lbl_bl = lv_label_create(screen, NULL);
	lv_obj_set_pos(lbl_bl, SETTINGS_BL_LBL_LEFT_X, SETTINGS_BL_LBL_TOP_Y);
	lv_label_set_static_text(lbl_bl, "Backlight");
	
	// Backlight dimmer slider control
	sld_bl = lv_slider_create(screen, NULL);
	lv_obj_set_pos(sld_bl, SETTINGS_BL_SLD_LEFT_X, SETTINGS_BL_SLD_TOP_Y);
	lv_obj_set_size(sld_bl, SETTINGS_BL_SLD_W, SETTINGS_BL_SLD_H);
	lv_obj_set_style_local_bg_color(sld_bl, LV_SLIDER_PART_BG, LV_STATE_DEFAULT, GUI_THEME_SLD_BG_COLOR);
	lv_obj_set_style_local_bg_color(sld_bl, LV_SLIDER_PART_INDIC, LV_STATE_DEFAULT, GUI_THEME_SLD_BG_COLOR);
	lv_slider_set_range(sld_bl, GUI_BL_MIN_PERCENT, GUI_BL_MAX_PERCENT);
	lv_obj_set_event_cb(sld_bl, _cb_bl_sld);
	
	// Backlight auto-dim control label
	lbl_ad = lv_label_create(screen, NULL);
	lv_obj_set_pos(lbl_ad, SETTINGS_AD_LBL_LEFT_X, SETTINGS_AD_LBL_TOP_Y);
	lv_label_set_static_text(lbl_ad, "Auto Dim");
	
	// Backlight auto-dim switch
	sw_ad = lv_switch_create(screen, NULL);
	lv_obj_set_pos(sw_ad, SETTINGS_AD_SW_LEFT_X, SETTINGS_AD_SW_TOP_Y);
	lv_obj_set_size(sw_ad, SETTINGS_AD_SW_W, SETTINGS_AD_SW_H);
	lv_obj_set_style_local_bg_color(sw_ad, LV_SWITCH_PART_BG, LV_STATE_DEFAULT, GUI_THEME_SLD_BG_COLOR);
	lv_obj_set_style_local_bg_color(sw_ad, LV_SWITCH_PART_INDIC, LV_STATE_DEFAULT, GUI_THEME_SLD_BG_COLOR);
	lv_obj_set_event_cb(sw_ad, _sw_ad_cb);
	
	// Country control label
	lbl_cn = lv_label_create(screen, NULL);
	lv_obj_set_pos(lbl_cn, SETTINGS_CN_LBL_LEFT_X, SETTINGS_CN_LBL_TOP_Y);
	lv_label_set_static_text(lbl_cn, "Country");
	
	// Country control dropdown
	_get_country_list();
	
	dd_cn = lv_dropdown_create(screen, NULL);
    lv_dropdown_set_options(dd_cn, country_list);
    lv_obj_set_pos(dd_cn, SETTINGS_CN_DD_LEFT_X, SETTINGS_CN_DD_TOP_Y);
	lv_obj_set_size(dd_cn, SETTINGS_CN_DD_W, SETTINGS_CN_DD_H);
	lv_obj_set_style_local_bg_color(dd_cn, LV_DROPDOWN_PART_SELECTED, LV_STATE_DEFAULT, GUI_THEME_SLD_BG_COLOR);
	lv_obj_set_event_cb(dd_cn, _cb_cn_dd);
	
	// Mic gain slider control label
	lbl_mic = lv_label_create(screen, NULL);
	lv_obj_set_pos(lbl_mic, SETTINGS_MIC_LBL_LEFT_X, SETTINGS_MIC_LBL_TOP_Y);
	lv_label_set_static_text(lbl_mic, "Mic");
	
	// Mic gain slider control
	sld_mic = lv_slider_create(screen, NULL);
	lv_obj_set_pos(sld_mic, SETTINGS_MIC_SLD_LEFT_X, SETTINGS_MIC_SLD_TOP_Y);
	lv_obj_set_size(sld_mic, SETTINGS_MIC_SLD_W, SETTINGS_MIC_SLD_H);
	lv_obj_set_style_local_bg_color(sld_mic, LV_SLIDER_PART_BG, LV_STATE_DEFAULT, GUI_THEME_SLD_BG_COLOR);
	lv_obj_set_style_local_bg_color(sld_mic, LV_SLIDER_PART_INDIC, LV_STATE_DEFAULT, GUI_THEME_SLD_BG_COLOR);
	lv_slider_set_range(sld_mic, GAIN_APP_MIC_MIN_DB, GAIN_APP_MIC_MAX_DB);
	lv_obj_set_event_cb(sld_mic, _cb_gain_sld);
	
	// Speaker gain slider control label
	lbl_spk = lv_label_create(screen, NULL);
	lv_obj_set_pos(lbl_spk, SETTINGS_SPK_LBL_LEFT_X, SETTINGS_SPK_LBL_TOP_Y);
	lv_label_set_static_text(lbl_spk, "Speaker");
	
	// Speaker gain slider control
	sld_spk = lv_slider_create(screen, NULL);
	lv_obj_set_pos(sld_spk, SETTINGS_SPK_SLD_LEFT_X, SETTINGS_SPK_SLD_TOP_Y);
	lv_obj_set_size(sld_spk, SETTINGS_SPK_SLD_W, SETTINGS_SPK_SLD_H);
	lv_obj_set_style_local_bg_color(sld_spk, LV_SLIDER_PART_BG, LV_STATE_DEFAULT, GUI_THEME_SLD_BG_COLOR);
	lv_obj_set_style_local_bg_color(sld_spk, LV_SLIDER_PART_INDIC, LV_STATE_DEFAULT, GUI_THEME_SLD_BG_COLOR);
	lv_slider_set_range(sld_spk, GAIN_APP_SPK_MIN_DB, GAIN_APP_SPK_MAX_DB);
	lv_obj_set_event_cb(sld_spk, _cb_gain_sld);
	
	// Set time control label
	lbl_time = lv_label_create(screen, NULL);
	lv_obj_set_pos(lbl_time, SETTINGS_TIME_LBL_LEFT_X, SETTINGS_TIME_LBL_TOP_Y);
	lv_label_set_static_text(lbl_time, "Time/Date");
	
	// Set time control
	btn_time = lv_btn_create(screen, NULL);
	lv_obj_set_pos(btn_time, SETTINGS_TIME_BTN_LEFT_X, SETTINGS_TIME_BTN_TOP_Y);
	lv_obj_set_size(btn_time, SETTINGS_TIME_BTN_W, SETTINGS_TIME_BTN_H);
	lv_obj_set_style_local_bg_color(btn_time, LV_BTN_PART_MAIN, LV_STATE_PRESSED, GUI_THEME_BG_COLOR);
	lv_obj_set_style_local_border_color(btn_time, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, GUI_THEME_BG_COLOR);
	lv_obj_set_style_local_bg_color(btn_time, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, GUI_THEME_BG_COLOR);
	lv_obj_set_style_local_border_color(btn_time, LV_BTN_PART_MAIN, LV_STATE_PRESSED, lv_theme_get_color_secondary());
	lv_obj_set_event_cb(btn_time, _cb_set_time);
	
	btn_time_lbl = lv_label_create(btn_time, NULL);
	lv_obj_set_style_local_text_font(btn_time_lbl, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &lv_font_montserrat_34);
	lv_label_set_static_text(btn_time_lbl, LV_SYMBOL_RIGHT);

#if (CONFIG_AUDIO_SAMPLE_ENABLE == true)
	// Button to trigger audio sample acquisition
	btn_smpl = lv_btn_create(screen, NULL);
	lv_obj_set_pos(btn_smpl, SETTINGS_SMPL_BTN_LEFT_X, SETTINGS_SMPL_BTN_TOP_Y);
	lv_obj_set_size(btn_smpl, SETTINGS_SMPL_BTN_W, SETTINGS_SMPL_BTN_H);
	lv_obj_set_event_cb(btn_smpl, _cb_smpl_btn);
	
	btn_smpl_lbl = lv_label_create(btn_smpl, NULL);
	lv_label_set_static_text(btn_smpl_lbl, "S");
#endif
	
	return screen;
}


void gui_screen_settings_set_active(bool en)
{
	if (en) {
		// Update state from persistent storage
		cur_is_paired = ps_get_bt_is_paired();
		ps_get_bt_pair_name(cur_paired_name);
		if (cur_is_paired) {
			lv_label_set_static_text(btn_bt_lbl, "Forget");
			lv_label_set_static_text(lbl_bt_status, cur_paired_name);
		} else {
			lv_label_set_static_text(btn_bt_lbl, "Pair");
			lv_label_set_static_text(lbl_bt_status, "Not paired");
		}
		
		ps_get_brightness_info(&cur_brightness, &cur_auto_dim);
		lv_slider_set_value(sld_bl, cur_brightness, false);
		if (cur_auto_dim) {
			lv_switch_on(sw_ad, false);
		} else {
			lv_switch_off(sw_ad, false);
		}
		
		cur_country_code = ps_get_country_code();
		lv_dropdown_set_selected(dd_cn, (uint16_t) cur_country_code);
		
		cur_mic_gain = ps_get_gain(PS_GAIN_MIC);
		lv_slider_set_value(sld_mic, _gain_to_sld_int(GAIN_TYPE_MIC, cur_mic_gain), false);
		
		cur_spk_gain = ps_get_gain(PS_GAIN_SPK);
		lv_slider_set_value(sld_spk, _gain_to_sld_int(GAIN_TYPE_SPK, cur_spk_gain), false);
		
		update_ps_ram = false;
	}
	
	// Control our visibility
	screen_is_active = en;
	lv_obj_set_hidden(screen, !en);
}


void gui_screen_settings_update_mic_gain(float g)
{
	// Update the control
	cur_mic_gain = g;
	lv_slider_set_value(sld_mic, _gain_to_sld_int(GAIN_TYPE_MIC, g), false);
	
	// Update persistent storage
	ps_set_gain(PS_GAIN_MIC, g);
	
	// Handle writing to the backing store differently based on whether or not we are displayed
	if (screen_is_active) {
		// Note to do update when we leave the screen
		update_ps_ram = true;
	} else {
		// Update immediately
		ps_update_backing_store();
	}
}


void gui_screen_settings_update_spk_gain(float g)
{
	// Update the control
	cur_spk_gain = g;
	lv_slider_set_value(sld_spk, _gain_to_sld_int(GAIN_TYPE_SPK, g), false);
	
	// Update persistent storage
	ps_set_gain(PS_GAIN_SPK, g);
	
	// Handle writing to the backing store differently based on whether or not we are displayed
	if (screen_is_active) {
		// Note to do update when we leave the screen
		update_ps_ram = true;
	} else {
		// Update immediately
		ps_update_backing_store();
	}
}


void gui_screen_settings_update_peer_info(uint8_t* addr, char* name)
{
	if (pairing_in_process) {
		// Update pairing info in persistant storage
		ps_set_bt_pair_info(addr, name);
		ps_update_backing_store();

		// Update displayed info
		_stop_pairing();
	}
}


void gui_screen_settings_forget_peer_info()
{
	ps_set_bt_clear_pair_info();
	ps_update_backing_store();
	
	cur_is_paired = false;
	lv_label_set_static_text(btn_bt_lbl, "Pair");
	lv_label_set_static_text(lbl_bt_status, "Not paired");
}



//
// Internal functions
//
static void _get_country_list()
{
	int i;
	int n;
	size_t sum;
	const country_info_t* ci;
	
	if (country_list != NULL) {
		free(country_list);
	}
	
	// Determine required space for the list
	n = int_get_num_countries();
	sum = n;                         // Account for terminating '/n' and final null characters
	for (i=0; i<n; i++) {
		ci = int_get_country_info(i);
		sum += strlen(ci->name);
	}
	
	// Attempt to allocate storage for the strings
	country_list = malloc(sum);
	if (country_list == NULL) {
		ESP_LOGE(TAG, "Failed to allocate country list");
	} else {
		// Create the list
		sum = 0;   //re-used
		for (i=0; i<n; i++) {
			ci = int_get_country_info(i);
			strcpy(&country_list[sum], ci->name);
			sum += strlen(ci->name);
			if (i < (n-1)) {
				country_list[sum] = '\n';
			} else {
				country_list[sum] = 0;  // final terminator
			}
			sum += 1;
		}
	}
}


static int16_t _gain_to_sld_int(int gain_type, float g)
{
	int16_t t;
	
	if (gain_type == GAIN_TYPE_MIC) {
		t = round(g);
		
		if (t < GAIN_APP_MIC_MIN_DB) {
			t = GAIN_APP_MIC_MIN_DB;
		} else if (t > GAIN_APP_MIC_MAX_DB) {
			t = GAIN_APP_MIC_MAX_DB;
		}
	} else {
		t = round(g);
		
		if (t < GAIN_APP_SPK_MIN_DB) {
			t = GAIN_APP_SPK_MIN_DB;
		} else if (t > GAIN_APP_SPK_MAX_DB) {
			t = GAIN_APP_SPK_MAX_DB;
		}
	}
	
	return t;
}


static void _cb_bck_btn(lv_obj_t* obj, lv_event_t event)
{
	if (event == LV_EVENT_CLICKED) {
		// Save any changes to battery backed RAM on exit
		if (update_ps_ram) {
			ps_update_backing_store();
		}
		gui_set_screen(GUI_SCREEN_MAIN);
	}
}


static void _cb_bt_btn(lv_obj_t* obj, lv_event_t event)
{
	if (event == LV_EVENT_CLICKED) {
		if (cur_is_paired) {
			// Confirm deleting pairing with user
			gui_preset_message_box_string("Clear Bluetooth pairing?", true, GUI_MSGBOX_CLR_PAIRING);
			xTaskNotify(task_handle_gui, GUI_NOTIFY_MESSAGEBOX_MASK, eSetBits);
		} else {
			// Managing the pairing process
			if (pairing_in_process) {
				_stop_pairing();
			} else {
				_start_pairing();
			}
		}
	}
}


static void _cb_bl_sld(lv_obj_t* obj, lv_event_t event)
{
	int16_t new_val;
	
	if (event == LV_EVENT_VALUE_CHANGED) {
		new_val = lv_slider_get_value(obj);
		cur_brightness = (int8_t) new_val;
		ps_set_brightness_info(cur_brightness, cur_auto_dim);
		xTaskNotify(task_handle_gcore, GCORE_NOTIFY_BRGHT_UPD_MASK, eSetBits);
		update_ps_ram = true;
	}
}


static void _sw_ad_cb(lv_obj_t* obj, lv_event_t event)
{
	if (event == LV_EVENT_VALUE_CHANGED) {
		cur_auto_dim = lv_switch_get_state(obj);
		ps_set_brightness_info(cur_brightness, cur_auto_dim);
		xTaskNotify(task_handle_gcore, GCORE_NOTIFY_BRGHT_UPD_MASK, eSetBits);
		update_ps_ram = true;
	}
}


static void _cb_cn_dd(lv_obj_t* obj, lv_event_t event)
{
	uint16_t new_val;
	
	if (event == LV_EVENT_VALUE_CHANGED) {
		new_val = lv_dropdown_get_selected(obj);
		cur_country_code = (uint8_t) new_val;
		ps_set_country_code(cur_country_code);
		xTaskNotify(task_handle_pots, POTS_NOTIFY_NEW_COUNTRY_MASK, eSetBits);
		update_ps_ram = true;
	}
}


static void _cb_gain_sld(lv_obj_t* obj, lv_event_t event)
{
	int16_t new_val;
	
	if (event == LV_EVENT_VALUE_CHANGED) {
		new_val = lv_slider_get_value(obj);
		if (obj == sld_mic) {
			cur_mic_gain = (float) new_val;
			if (cur_mic_gain < GAIN_APP_MIC_MIN_DB) {
				cur_mic_gain = GAIN_APP_MIC_MIN_DB;
			} else if (cur_mic_gain > GAIN_APP_MIC_MAX_DB) {
				cur_mic_gain = GAIN_APP_MIC_MAX_DB;
			}
			ps_set_gain(PS_GAIN_MIC, cur_mic_gain);
			xTaskNotify(task_handle_app, APP_NOTIFY_NEW_GUI_MIC_GAIN_MASK, eSetBits);
		} else {
			cur_spk_gain = (float) new_val;
			if (cur_spk_gain < GAIN_APP_SPK_MIN_DB) {
				cur_spk_gain = GAIN_APP_SPK_MIN_DB;
			} else if (cur_spk_gain > GAIN_APP_SPK_MAX_DB) {
				cur_spk_gain = GAIN_APP_SPK_MAX_DB;
			}
			ps_set_gain(PS_GAIN_SPK, cur_spk_gain);
			xTaskNotify(task_handle_app, APP_NOTIFY_NEW_GUI_SPK_GAIN_MASK, eSetBits);
		}
		update_ps_ram = true;
	}
}


static void _cb_set_time(lv_obj_t* obj, lv_event_t event)
{
	if (event == LV_EVENT_CLICKED) {
		// Save any changes to battery backed RAM on exit
		if (update_ps_ram) {
			ps_update_backing_store();
		}
		
		gui_set_screen(GUI_SCREEN_TIME);
	}
}


static void _cb_pair_timer_task(lv_task_t* task)
{
	// Pairing timed out with no new pairing info
	_stop_pairing();	
}


static void _start_pairing()
{
	static char status_buf[22];    // Long enough for "Pairing (pin XXXX)..." + null
	pairing_in_process = true;
	
	// Indicate we are pairing
#if (CONFIG_BT_SSP_ENABLED == true)
	sprintf(status_buf, "Pairing...");
#else
	sprintf(status_buf, "Pairing (pin " BLUETOOTH_PIN_STRING ")...");
#endif
	lv_label_set_static_text(lbl_bt_status, status_buf);
	lv_label_set_static_text(btn_bt_lbl, "Cancel");
	
	// Start timer
	if (pair_timer_task != NULL) {
		lv_task_del(pair_timer_task);
		pair_timer_task = NULL;
	}
	pair_timer_task = lv_task_create(_cb_pair_timer_task, GUI_MAX_PAIR_MSEC, LV_TASK_PRIO_LOW, NULL);
	
	// Notify bt_task to allow pairing
	xTaskNotify(task_handle_bt, BT_NOTIFY_ENABLE_PAIR_MASK, eSetBits);
}


static void _stop_pairing()
{
	pairing_in_process = false;
	
	// Stop timer
	if (pair_timer_task != NULL) {
		lv_task_del(pair_timer_task);
		pair_timer_task = NULL;
	}
	
	// Notify bt_task to end pairing
	xTaskNotify(task_handle_bt, BT_NOTIFY_DISABLE_PAIR_MASK, eSetBits);
	
	// Update pair info
	cur_is_paired = ps_get_bt_is_paired();
	ps_get_bt_pair_name(cur_paired_name);
	if (cur_is_paired) {
		lv_label_set_static_text(btn_bt_lbl, "Forget");
		lv_label_set_static_text(lbl_bt_status, cur_paired_name);
	} else {
		lv_label_set_static_text(btn_bt_lbl, "Pair");
		lv_label_set_static_text(lbl_bt_status, "Not paired");
	}
}


#if (CONFIG_AUDIO_SAMPLE_ENABLE == true)
static void _cb_smpl_btn(lv_obj_t* obj, lv_event_t event)
{
	if (event == LV_EVENT_CLICKED) {
		xTaskNotify(task_handle_app, APP_NOTIFY_START_AUDIO_SMPL_MASK, eSetBits);
	}
}
#endif

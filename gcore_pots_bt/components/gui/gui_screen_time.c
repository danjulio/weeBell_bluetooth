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
#include "gui_screen_time.h"
#include "gui_task.h"
#include "esp_system.h"
#include "time_utilities.h"
#include <time.h>
#include <sys/time.h>



//
// Set Time GUI Screen constants
//
#define TIMESET_I_HOUR_H 0
#define TIMESET_I_HOUR_L 1
#define TIMESET_I_MIN_H  2
#define TIMESET_I_MIN_L  3
#define TIMESET_I_SEC_H  4
#define TIMESET_I_SEC_L  5
#define TIMESET_I_MON_H  6
#define TIMESET_I_MON_L  7
#define TIMESET_I_DAY_H  8
#define TIMESET_I_DAY_L  9
#define TIMESET_I_YEAR_H 10
#define TIMESET_I_YEAR_L 11
#define TIMESET_NUM_I    12


// Macro to convert a single-digit numeric value (0-9) to an ASCII digit ('0' - '9')
#define ASC_DIGIT(n)     '0' + n


//
// Set Time GUI Screen variables
//

// LVGL objects
static lv_obj_t* screen;
static lv_obj_t* btn_bck;
static lv_obj_t* btn_bck_lbl;
static lv_obj_t* lbl_screen;
static lv_obj_t* lbl_time_set;
static lv_obj_t* btn_set_time_keypad;
static lv_obj_t* btn_save;
static lv_obj_t* btn_save_lbl;

// Time set state
static tmElements_t timeset_value;
static int timeset_index;
static char timeset_string[32];   // "HH:MM:SS MM/DD/YY" + room for #FFFFFF n# recolor string

// Keypad array
static const char* keyp_map[] = {"1", "2", "3", "\n",
                                 "4", "5", "6", "\n",
                                 "7", "8", "9", "\n",
                                 LV_SYMBOL_LEFT, "0", LV_SYMBOL_RIGHT, ""};

static const char keyp_vals[] = {'1', '2', '3',
                                 '4', '5', '6',
                                 '7', '8', '9',
                                 'L', '0', 'R'};
                     
// Character values to prepend the set time/date string currently indexed character with
//static const char recolor_array[8] = {'#', '0', '0', 'A', '0', 'F', 'F', ' '};
static const char recolor_array[8] = {'#', 'F', 'F', 'F', 'F', '0', '0', ' '};

// Days per month (not counting leap years) for validation (0-based index)
static const uint8_t days_per_month[]={31,28,31,30,31,30,31,31,30,31,30,31};


//
// Set Time GUI Screen internal function forward declarations
//
static void _fix_timeset_value();
static bool _is_valid_digit_position(int i);
static void _display_timeset_value();
static bool _set_timeset_indexed_value(int n);
static void _cb_bck_btn(lv_obj_t * btn, lv_event_t event);
static void _cb_btn_set_time_keypad(lv_obj_t * btn, lv_event_t event);
static void _cb_save_btn(lv_obj_t * btn, lv_event_t event);



//
// Set Time GUI Screen API
//

/**
 * Create the set time screen, its graphical objects and link necessary callbacks
 */
lv_obj_t* gui_screen_time_create()
{
	// Create screen object
	screen = lv_obj_create(NULL, NULL);
	
	// Create the widgets for this screen
	//
	
	// Back control button
	btn_bck = lv_btn_create(screen, NULL);
	lv_obj_set_pos(btn_bck, TIME_BCK_BTN_LEFT_X, TIME_BCK_BTN_TOP_Y);
	lv_obj_set_size(btn_bck, TIME_BCK_BTN_W, TIME_BCK_BTN_H);
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
	lv_obj_set_pos(lbl_screen, TIME_SCR_LBL_LEFT_X, TIME_SCR_LBL_TOP_Y);
	lv_obj_set_width(lbl_screen, TIME_SCR_LBL_W);
	lv_obj_set_style_local_text_font(lbl_screen, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &lv_font_montserrat_20);
	lv_label_set_static_text(lbl_screen, "Set Time/Date");
	
	// Set Time/Date String (centered)
	lbl_time_set = lv_label_create(screen, NULL);
	lv_label_set_long_mode(lbl_time_set, LV_LABEL_LONG_BREAK);
	lv_label_set_align(lbl_time_set, LV_LABEL_ALIGN_CENTER);
	lv_obj_set_pos(lbl_time_set, TIME_TD_LEFT_X, TIME_TD_TOP_Y);
	lv_obj_set_width(lbl_time_set, TIME_TD_W);
	lv_obj_set_style_local_text_font(lbl_time_set, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &lv_font_montserrat_34);
	lv_label_set_recolor(lbl_time_set, true);
	lv_obj_set_style_local_text_color(lbl_time_set, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_MAKE(0xB0, 0xB0, 0xB0));
	
	// Time set button matrix
	btn_set_time_keypad = lv_btnmatrix_create(screen, NULL);
	lv_obj_set_pos(btn_set_time_keypad, TIME_BTN_MATRIX_LEFT_X, TIME_BTN_MATRIX_TOP_Y);
	lv_obj_set_size(btn_set_time_keypad, TIME_BTN_MATRIX_W, TIME_BTN_MATRIX_H);
	lv_btnmatrix_set_map(btn_set_time_keypad, keyp_map);
	lv_obj_set_style_local_text_font(btn_set_time_keypad, LV_BTNMATRIX_PART_BTN, LV_STATE_DEFAULT, &lv_font_montserrat_34);
	lv_obj_set_style_local_border_color(btn_set_time_keypad, LV_BTNMATRIX_PART_BTN, LV_STATE_DEFAULT, GUI_THEME_BG_COLOR);
	lv_obj_set_style_local_bg_color(btn_set_time_keypad, LV_BTNMATRIX_PART_BTN, LV_STATE_DEFAULT, GUI_THEME_BG_COLOR);
	lv_obj_set_style_local_bg_color(btn_set_time_keypad, LV_BTNMATRIX_PART_BTN, LV_STATE_PRESSED, GUI_THEME_BG_COLOR);
	lv_obj_set_style_local_border_color(btn_set_time_keypad, LV_BTNMATRIX_PART_BG, LV_STATE_DEFAULT, GUI_THEME_BG_COLOR);
	lv_obj_set_style_local_bg_color(btn_set_time_keypad, LV_BTNMATRIX_PART_BG, LV_STATE_DEFAULT, GUI_THEME_BG_COLOR);
	lv_btnmatrix_set_btn_ctrl_all(btn_set_time_keypad, LV_BTNMATRIX_CTRL_NO_REPEAT);
	lv_btnmatrix_set_btn_ctrl_all(btn_set_time_keypad, LV_BTNMATRIX_CTRL_CLICK_TRIG);
	lv_obj_set_event_cb(btn_set_time_keypad, _cb_btn_set_time_keypad);
	
	// Save control button
	btn_save = lv_btn_create(screen, NULL);
	lv_obj_set_pos(btn_save, TIME_SAVE_BTN_LEFT_X, TIME_SAVE_BTN_TOP_Y);
	lv_obj_set_size(btn_save, TIME_SAVE_BTN_W, TIME_SAVE_BTN_H);
	lv_obj_set_style_local_bg_color(btn_save, LV_BTN_PART_MAIN, LV_STATE_PRESSED, GUI_THEME_BG_COLOR);
	lv_obj_set_style_local_border_color(btn_save, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, GUI_THEME_BG_COLOR);
	lv_obj_set_style_local_bg_color(btn_save, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, GUI_THEME_BG_COLOR);
	lv_obj_set_style_local_border_color(btn_save, LV_BTN_PART_MAIN, LV_STATE_PRESSED, lv_theme_get_color_secondary());
	lv_obj_set_event_cb(btn_save, _cb_save_btn);
	
	btn_save_lbl = lv_label_create(btn_save, NULL);
	lv_obj_set_style_local_text_font(btn_save_lbl, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &lv_font_montserrat_20);
	lv_label_set_static_text(btn_save_lbl, "SAVE");

	return screen;
}


/**
 * Initialize the time screen's dynamic values
 */
void gui_screen_time_set_active(bool en)
{
	if (en) {
		// Get the system time into our variable
		time_get(&timeset_value);
		
		// Initialize the selection index to the first digit
		timeset_index = 0;
		
		// Update the time set label
		_display_timeset_value();
	}
}


//
// Set Time GUI Screen internal functions
//

/**
 * Fixup the timeset_value by recomputing it to get the correct day-of-week field
 * that we don't ask the user to set
 */
static void _fix_timeset_value()
{
	time_t secs;
	
	// Convert our timeset_value into seconds (this doesn't use DOW)
	secs = rtc_makeTime(timeset_value);
	
	// Rebuild the timeset_value from the seconds so it will have a correct DOW
	rtc_breakTime(secs, &timeset_value);
}


/**
 * Returns true when the passed in index is pointing to a valid digit position and
 * not a separator character
 */
static bool _is_valid_digit_position(int i)
{
	//       H  H  :  M  M  :  S  S     M  M  /  D  D  /  Y  Y
	// i     0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15 16
	return (!((i==2)||(i==5)||(i==8)||(i==11)||(i==14)));
}


/**
 * Update the set time/date label.  The current indexed digit is made to be full
 * white to indicate it is the one being changed.
 */
static void _display_timeset_value()
{
	int timeset_string_index = 0;  // Current timeset_string insertion point
	int time_string_index = 0;     // Current position in displayed "HH:MM:SS MM/DD/YY"
	int time_digit_index = 0;      // Current time digit index (0-11) for HHMMSSMMDDYY
	int i;
	bool did_recolor;

	while (time_string_index <= 16) {

		// Insert the recolor string before the currently selected time digit
		if ((timeset_index == time_digit_index) && _is_valid_digit_position(time_string_index)) {
			for (i=0; i<8; i++) {
				timeset_string[timeset_string_index++] = recolor_array[i];
			}
			did_recolor = true;
		} else {
			did_recolor = false;
		}
		
		// Insert the appropriate time character
		//
		//                          H  H  :  M  M  :  S  S     M  M  /  D  D  /  Y  Y
		// time_string_index        0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15 16
		// time_digit_index         0  1     2  3     4  5     6  7     8  9     10 11
		//
		switch (time_string_index++) {
			case 0: // Hours tens
				timeset_string[timeset_string_index++] = ASC_DIGIT(timeset_value.Hour / 10);
				time_digit_index++;
				break;
			case 1: // Hours units
				timeset_string[timeset_string_index++] = ASC_DIGIT(timeset_value.Hour % 10);
				time_digit_index++;
				break;
			case 3: // Minutes tens
				timeset_string[timeset_string_index++] = ASC_DIGIT(timeset_value.Minute / 10);
				time_digit_index++;
				break;
			case 4: // Minutes units
				timeset_string[timeset_string_index++] = ASC_DIGIT(timeset_value.Minute % 10);
				time_digit_index++;
				break;
			case 6: // Seconds tens
				timeset_string[timeset_string_index++] = ASC_DIGIT(timeset_value.Second / 10);
				time_digit_index++;
				break;
			case 7: // Seconds units
				timeset_string[timeset_string_index++] = ASC_DIGIT(timeset_value.Second % 10);
				time_digit_index++;
				break;
			case 9: // Month tens
				timeset_string[timeset_string_index++] = ASC_DIGIT(timeset_value.Month / 10);
				time_digit_index++;
				break;
			case 10: // Month units
				timeset_string[timeset_string_index++] = ASC_DIGIT(timeset_value.Month % 10);
				time_digit_index++;
				break;
			case 12: // Day tens
				timeset_string[timeset_string_index++] = ASC_DIGIT(timeset_value.Day / 10);
				time_digit_index++;
				break;
			case 13: // Day units
				timeset_string[timeset_string_index++] = ASC_DIGIT(timeset_value.Day % 10);
				time_digit_index++;
				break;
			case 15: // Year tens - Assume we're post 2000
				timeset_string[timeset_string_index++] = ASC_DIGIT(tmYearToY2k(timeset_value.Year) / 10);
				time_digit_index++;
				break;
			case 16: // Year units
				timeset_string[timeset_string_index++] = ASC_DIGIT(tmYearToY2k(timeset_value.Year) % 10);
				time_digit_index++;
				break;
			
			case 2: // Time section separators
			case 5:
				if (did_recolor) {
					// End recoloring before we insert this character
					timeset_string[timeset_string_index++] = '#';
					did_recolor = false;
				}
				timeset_string[timeset_string_index++] = ':';
				break;
				
			case 8: // Time / Date separator
				if (did_recolor) {
					// End recoloring before we insert this character
					timeset_string[timeset_string_index++] = '#';
					did_recolor = false;
				}
				timeset_string[timeset_string_index++] = ' ';
				break;
				
			case 11: // Date section separators
			case 14:
				if (did_recolor) {
					// End recoloring before we insert this character
					timeset_string[timeset_string_index++] = '#';
					did_recolor = false;
				}
				timeset_string[timeset_string_index++] = '/';
				break;
		}
		
		if (did_recolor) {
			// End the recoloring after the digit
			timeset_string[timeset_string_index++] = '#';
			did_recolor = false;
		}
	}
	
	// Make sure the string is terminated
	timeset_string[timeset_string_index] = 0;
	
	lv_label_set_static_text(lbl_time_set, timeset_string);
}


/**
 * Apply the button press value n to the timeset_value, making sure that only
 * valid values are allowed for each digit position (for example, you cannot set
 * an hour value > 23).  Return true if the digit position was updated, false if it
 * was not changed.
 */
static bool _set_timeset_indexed_value(int n)
{
	bool changed = false;
	uint8_t u8;
	
	switch (timeset_index) {
		case TIMESET_I_HOUR_H:
			if (n < 3) {
				timeset_value.Hour = (n * 10) + (timeset_value.Hour % 10);
				changed = true;
			}
			break;
		case TIMESET_I_HOUR_L:
			if (timeset_value.Hour >= 20) {
				// Only allow 20 - 23
				if (n < 4) {
					timeset_value.Hour = ((timeset_value.Hour / 10) * 10) + n;
					changed = true;
				}
			} else {
				// Allow 00-09 or 10-19
				timeset_value.Hour = ((timeset_value.Hour / 10) * 10) + n;
				changed = true;
			}
			break;
		case TIMESET_I_MIN_H:
			if (n < 6) {
				timeset_value.Minute = (n * 10) + (timeset_value.Minute % 10);
				changed = true;
			}
			break;
		case TIMESET_I_MIN_L:
			timeset_value.Minute = ((timeset_value.Minute / 10) * 10) + n;
			changed = true;
			break;
		case TIMESET_I_SEC_H:
			if (n < 6) {
				timeset_value.Second = (n * 10) + (timeset_value.Second % 10);
				changed = true;
			}
			break;
		case TIMESET_I_SEC_L:
			timeset_value.Second = ((timeset_value.Second / 10) * 10) + n;
			changed = true;
			break;
		case TIMESET_I_MON_H:
			if (n < 2) {
				timeset_value.Month = (n * 10) + (timeset_value.Month % 10);
				if (timeset_value.Month == 0) timeset_value.Month = 1;
				changed = true;
			}
			break;
		case TIMESET_I_MON_L:
			if (timeset_value.Month >= 10) {
				// Only allow 10-12
				if (n < 3) {
					timeset_value.Month = ((timeset_value.Month / 10) * 10) + n;
					changed = true;
				}
			} else {
				// Allow 1-9
				if (n > 0) {
					timeset_value.Month = ((timeset_value.Month / 10) * 10) + n;
					changed = true;
				}
			}
			break;
		case TIMESET_I_DAY_H:
			u8 = days_per_month[timeset_value.Month - 1];
			if (n <= (u8 / 10)) {
				// Only allow valid tens digit for this month (will be 2 or 3)
				timeset_value.Day = (n * 10) + (timeset_value.Day % 10);
				changed = true;
			}
			break;
		case TIMESET_I_DAY_L:
			u8 = days_per_month[timeset_value.Month - 1];
			if ((timeset_value.Day / 10) == (u8 / 10)) {
				if (n <= (u8 % 10)) {
					// Only allow valid units digits when the tens digit is the highest
					timeset_value.Day = ((timeset_value.Day / 10) * 10) + n;
					changed = true;
				}
			} else {
				// Units values of 0-9 are valid when the tens is lower than the highest
				timeset_value.Day = ((timeset_value.Day / 10) * 10) + n;
				changed = true;
			}
			break;
		case TIMESET_I_YEAR_H:
			u8 = tmYearToY2k(timeset_value.Year);
			u8 = (n * 10) + (u8 % 10);
			timeset_value.Year = y2kYearToTm(u8);
			changed = true;
			break;
		case TIMESET_I_YEAR_L:
			u8 = tmYearToY2k(timeset_value.Year);
			u8 = ((u8 / 10) * 10) + n;
			timeset_value.Year = y2kYearToTm(u8);
			changed = true;
			break;
	}

	return changed;
}


static void _cb_bck_btn(lv_obj_t * btn, lv_event_t event)
{
	if (event == LV_EVENT_CLICKED) {
		// Bail back to settings screen
		gui_set_screen(GUI_SCREEN_SETTINGS);
	}
}


static void _cb_btn_set_time_keypad(lv_obj_t * btn, lv_event_t event)
{
	char button_val = ' ';
	int n;
	
	if (event == LV_EVENT_VALUE_CHANGED) {

		n = (int) lv_btnmatrix_get_active_btn(btn);
		button_val = keyp_vals[n];
	
		if (button_val == 'L') {
			// Decrement to the previous digit
			if (timeset_index > TIMESET_I_HOUR_H) {
				timeset_index--;
			}
			_display_timeset_value();
		} else if (button_val == 'R') {
			// Increment to the next digit
			if (timeset_index < TIMESET_I_YEAR_L) {
				timeset_index++;
			}
			_display_timeset_value();
		} else if ((button_val >= '0') && (button_val <= '9')) {
			// Number button
			n = (int) (button_val - '0');

			// Update the indexed digit based on the button value
			if (_set_timeset_indexed_value(n)) {
				// Increment to next digit if the digit was changed
				if (timeset_index < TIMESET_I_YEAR_L) {
					timeset_index++;
				}
			}
			
			// Update the display
			_display_timeset_value();
		}
	}
}


static void _cb_save_btn(lv_obj_t * btn, lv_event_t event)
{
	if (event == LV_EVENT_CLICKED) {
		// Set the time before going back to the settings screen
		_fix_timeset_value();
		time_set(timeset_value);
		
		gui_set_screen(GUI_SCREEN_SETTINGS);
	}
}

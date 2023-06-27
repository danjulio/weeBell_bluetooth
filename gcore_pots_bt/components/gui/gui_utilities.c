/*
 * Utility functions for all screens
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
#include "gui_utilities.h"
#include "gui_task.h"




//
// GUI Utilities variables
//

// MessageBox
static char preset_msgbox_string[GUI_MSG_BOX_MAX_LEN];
static bool preset_msgbox_dual_btn;
static int preset_msgbox_id;
static int displayed_msgbox_id;

// LVGL objects
static lv_obj_t*  msg_box_bg = NULL;
static lv_obj_t*  msg_box = NULL;

// Message box buttons
static const char* msg_box_buttons1[] = {"OK", ""};
static const char* msg_box_buttons2[] = {"Cancel", "Confirm", ""};



//
// Forward Declarations for internal functions
//
static void _display_message_box(lv_obj_t* parent, const char* msg, bool dual_btn);
static void _cb_messagebox_event(lv_obj_t *obj, lv_event_t evt);
static void _cb_messagebox_opa_anim(void* bg, lv_anim_value_t v);



//
// GUI Utilities API
//

/**
 * Set the string for gui_preset_message_box - this function is designed to be called
 * by another task who then sends a GUI_NOTIFY_MESSAGEBOX_MASK to gui_task to initiate
 * the message box
 */
void gui_preset_message_box_string(const char* msg, bool dual_btn, int msgbox_id)
{
	char c;
	int i = 0;
	
	// Copy up to GUI_MSG_BOX_MAX_LEN-1 characters (leaving room for null)
	while (i<GUI_MSG_BOX_MAX_LEN-1) {
		c = *(msg+i);
		preset_msgbox_string[i++] = c;
		if (c == 0) break;
	}
	preset_msgbox_string[i] = 0;
	
	preset_msgbox_dual_btn = dual_btn;
	preset_msgbox_id = msgbox_id;
}


/**
 * Display a message box with the preset string - be sure to set the string first!!!
 */
void gui_preset_message_box(lv_obj_t* parent)
{
	_display_message_box(parent, preset_msgbox_string, preset_msgbox_dual_btn);
}


/**
 * Trigger a close of the message box
 */
void gui_close_message_box()
{
	if (msg_box_bg != NULL) {
		lv_msgbox_start_auto_close(msg_box, 0);
	}
}


/**
 * Return true if message box is still displayed
 */
bool gui_message_box_displayed()
{
	return (msg_box_bg != NULL);
}



//
// Internal functions
//

/**
 * Display a message box with at least one button for dismissal
 */
static void _display_message_box(lv_obj_t* parent, const char* msg, bool dual_btn)
{
	static lv_style_t modal_style;   // Message box background style
	
	// Create a full-screen background
	lv_style_init(&modal_style);
	
	// Set the background's style
	lv_style_set_bg_color(&modal_style, LV_STATE_DEFAULT, LV_COLOR_BLACK);
	
	// Create a base object for the modal background 
	msg_box_bg = lv_obj_create(parent, NULL);
	lv_obj_reset_style_list(msg_box_bg, LV_OBJ_PART_MAIN);
	lv_obj_add_style(msg_box_bg, LV_OBJ_PART_MAIN, &modal_style);
	lv_obj_set_pos(msg_box_bg, 0, 0);
	lv_obj_set_size(msg_box_bg, LV_HOR_RES, LV_VER_RES);
	lv_obj_set_event_cb(msg_box_bg, _cb_messagebox_event);
	
	// Create the message box as a child of the modal background
	msg_box = lv_msgbox_create(msg_box_bg, NULL);
	if (dual_btn) {
		lv_msgbox_add_btns(msg_box, msg_box_buttons2);
	} else {
		lv_msgbox_add_btns(msg_box, msg_box_buttons1);
	}
	lv_msgbox_set_text(msg_box, msg);
	lv_obj_set_size(msg_box, GUI_MSG_BOX_W, GUI_MSG_BOX_H);
	lv_obj_align(msg_box, NULL, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_event_cb(msg_box, _cb_messagebox_event);
	
	// Fade the message box in with an animation
	lv_anim_t a;
	lv_anim_init(&a);
	lv_anim_set_var(&a, msg_box_bg);
	lv_anim_set_time(&a, 500);
	lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_50);
	lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)_cb_messagebox_opa_anim);
	lv_anim_start(&a);
	
	// Save id in case another preset is called before this messagebox is dismissed
	displayed_msgbox_id = preset_msgbox_id;
}


/**
 * Message Box callback handling closure and deferred object deletion
 */
static void _cb_messagebox_event(lv_obj_t *obj, lv_event_t event)
{
	if (event == LV_EVENT_DELETE) {
		if (obj == msg_box_bg) {
			msg_box_bg = NULL;
		} else if (obj == msg_box) {
			// Delete the parent modal background
			lv_obj_del_async(lv_obj_get_parent(msg_box));
			msg_box = NULL; // happens before object is actually deleted!
		}
	} else if (event == LV_EVENT_VALUE_CHANGED) {
		// Let gui_task know a button was clicked
		gui_set_msgbox_btn(displayed_msgbox_id, lv_msgbox_get_active_btn(obj));
		
		// Dismiss the message box
		lv_msgbox_start_auto_close(msg_box, 0);
	}
}


/**
 * Message Box animation
 */
static void _cb_messagebox_opa_anim(void* bg, lv_anim_value_t v)
{
	lv_obj_set_style_local_bg_opa(bg, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, v);
}

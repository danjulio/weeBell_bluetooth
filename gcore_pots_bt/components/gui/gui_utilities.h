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
#ifndef GUI_UTILITIES_H
#define GUI_UTILITIES_H

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

//
// GUI Utilities Constants
//

// Message box button ids
#define GUI_MSG_BOX_BTN_DISMSS  0
#define GUI_MSG_BOX_BTN_AFFIRM  1

// Message box dimensions
#define GUI_MSG_BOX_W           240
#define GUI_MSG_BOX_H           180

// Maximum preset message box string length
#define GUI_MSG_BOX_MAX_LEN     128



//
// GUI Utilities API
//
void gui_preset_message_box_string(const char* msg, bool dual_btn, int msgbox_id);
void gui_preset_message_box(lv_obj_t* parent);
void gui_close_message_box();
bool gui_message_box_displayed();

#endif /* GUI_UTILITIES_H */
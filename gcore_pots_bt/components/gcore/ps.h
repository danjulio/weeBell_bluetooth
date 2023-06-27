/*
 * Persistent Storage Module
 *
 * Manage the persistent storage kept in the gCore EFM8 RAM and provide access
 * routines to it.
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
#ifndef PS_UTILITIES_H
#define PS_UTILITIES_H

#include <stdbool.h>
#include <stdint.h>

//
// Constants
//

// PS_VERSION increments when the layout changes.  This allows us to automatically
// migrate when we add new features.
#define PS_VERSION 1

// Gain types
#define PS_GAIN_MIC 0
#define PS_GAIN_SPK 1


//
// PS Utilities API
//
bool ps_init();
bool ps_set_factory_default();
bool ps_update_backing_store();    // Call after making changes vis ps_set_* routines

bool ps_get_bt_is_paired();
void ps_get_bt_pair_addr(uint8_t* addr);
void ps_get_bt_pair_name(char* name);   // name must be ESP_BT_GAP_MAX_BDNAME_LEN+1 long
void ps_set_bt_pair_info(uint8_t* addr, char* name);
void ps_set_bt_clear_pair_info();

uint8_t ps_get_country_code();
void ps_set_country_code(uint8_t code);

float ps_get_gain(int gain_type);
void ps_set_gain(int gain_type, float g);

void ps_get_brightness_info(uint8_t* br, bool* auto_dim_en);
void ps_set_brightness_info(uint8_t br, bool auto_dim_en);

#endif /* PS_UTILITIES_H */
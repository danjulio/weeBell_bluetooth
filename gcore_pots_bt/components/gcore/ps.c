/*
 * Persistent Storage Module
 *
 * Manage the persistent storage kept in the gCore EFM8 RAM and provide access
 * routines to it.
 *
 * Persistent storage RAM layout:
 *   ps_header_t
 *   ps_v1_data_t
 *   uint16_t checksum
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
#include <stdbool.h>
#include <string.h>
#include "esp_system.h"
#include "esp_log.h"
#include "esp_gap_bt_api.h"
#include "gain.h"
#include "gcore.h"
#include "international.h"
#include "ps.h"

//
// Constants
//

// Allows us to determine if the contents of RAM are valid
#define PS_MAGIC_BYTES 0x47434254   /* "GCBT" */



//
// Persistent storage data structures
//
typedef struct {
	uint32_t magic_bytes;
	uint8_t version;
} ps_header_t;

// Version 1 persistent storage data fields
typedef struct {
	char peer_name[ESP_BT_GAP_MAX_BDNAME_LEN+1];
	uint8_t paired;
	uint8_t peer_addr[6];
	uint8_t country_code;
	float mic_gain;             // +/- dB
	float spk_gain;             // +/- dB
	uint8_t brightness;         // Percentage 
	uint8_t auto_dim;
} ps_v1_data_t;


//
// Global variables
//
static const char* TAG = "ps";

static ps_header_t ps_header;
static ps_v1_data_t ps_data;



//
// Forward declarations for internal functions
//
static bool _ps_read_header();
static bool _ps_read_data();
static bool _ps_read_checksum(uint16_t* cs);
static bool _ps_write_array();
static uint16_t _ps_compute_checksum();
static bool _ps_validate_checksum(uint16_t cs);



//
// API
//
bool ps_init()
{
	bool success = true;
	bool is_valid;
	uint16_t cs;
	
	// Check to see if RAM has valid information
	if (!_ps_read_header()) {
		return false;
	}
	is_valid = (ps_header.magic_bytes == PS_MAGIC_BYTES) && (ps_header.version == PS_VERSION);
	
	if (!is_valid) {
		ESP_LOGI(TAG, "Initialize persistent storage");
		success = ps_set_factory_default();
	} else {
		// Validate the data using the checksum
		success = _ps_read_data();
		if (success) {
			success = _ps_read_checksum(&cs);
			if (success) {
				if (_ps_validate_checksum(cs)) {
					ESP_LOGI(TAG, "Read persistent storage");
				} else {
					ESP_LOGE(TAG, "Invalid checksum : Re-initialize persistent storage");
					success = ps_set_factory_default();
				}
			}
		}
	}
	
	return success;
}


bool ps_set_factory_default()
{
	int i;
	
	// Setup default values
	ps_header.magic_bytes = PS_MAGIC_BYTES;
	ps_header.version = PS_VERSION;
	
	ps_data.paired = 0;
	for (i=0; i<ESP_BT_GAP_MAX_BDNAME_LEN+1; i++) ps_data.peer_name[i] = 0;
	for (i=0; i<6; i++) ps_data.peer_addr[i] = 0;
	ps_data.country_code = INT_DEFAULT_COUNTRY;
	ps_data.mic_gain = GAIN_APP_MIC_NOM_DB;
	ps_data.spk_gain = GAIN_APP_SPK_NOM_DB;
	ps_data.brightness = 80;
	ps_data.auto_dim = 0;
	
	// Store to RAM
	return (_ps_write_array());
}


bool ps_update_backing_store()
{
	return (_ps_write_array());
}


bool ps_get_bt_is_paired()
{
	return (ps_data.paired != 0);
}


void ps_get_bt_pair_addr(uint8_t* addr)
{
	for (int i=0; i<6; i++) {
		*(addr+i) = ps_data.peer_addr[i];
	}
}


void ps_get_bt_pair_name(char* name)
{
	strncpy(name, ps_data.peer_name, ESP_BT_GAP_MAX_BDNAME_LEN+1);
	*(name + ESP_BT_GAP_MAX_BDNAME_LEN) = 0;
}


void ps_set_bt_pair_info(uint8_t* addr, char* name)
{
	ps_data.paired = 1;
	
	for (int i=0; i<6; i++) {
		ps_data.peer_addr[i] = *(addr+i);
	}
	
	strncpy(ps_data.peer_name, name, ESP_BT_GAP_MAX_BDNAME_LEN);
	*(name + ESP_BT_GAP_MAX_BDNAME_LEN) = 0;
}


void ps_set_bt_clear_pair_info()
{
	ps_data.paired = 0;
	
	for (int i=0; i<6; i++) {
		ps_data.peer_addr[i] = 0;
	}
	
	ps_data.peer_name[0] = 0;
}


uint8_t ps_get_country_code()
{
	return ps_data.country_code;
}


void ps_set_country_code(uint8_t code)
{
	ps_data.country_code = code;
}


float ps_get_gain(int gain_type)
{
	if (gain_type == PS_GAIN_MIC) {
		return ps_data.mic_gain;
	} else {
		return ps_data.spk_gain;
	}
}


void ps_set_gain(int gain_type, float g)
{
	if (gain_type == PS_GAIN_MIC) {
		ps_data.mic_gain = g;
	} else {
		ps_data.spk_gain = g;
	}
}


void ps_get_brightness_info(uint8_t* br, bool* auto_dim_en)
{
	*br = ps_data.brightness;
	*auto_dim_en = ps_data.auto_dim != 0;
}


void ps_set_brightness_info(uint8_t br, bool auto_dim_en)
{
	if (br > 100) br = 100;
	ps_data.brightness = br;
	ps_data.auto_dim = auto_dim_en ? 1 : 0;
}



//
// Internal Functions
//
static bool _ps_read_header()
{
	uint16_t len;
	
	len = (uint16_t) sizeof(ps_header);
	
	if (!gcore_get_nvram_bytes(0, (uint8_t*) &ps_header, len)) {
		ESP_LOGE(TAG, "Failed to read header from RAM");
		return false;
	}
	
	return true;
}


static bool _ps_read_data()
{
	uint16_t start;
	uint16_t len;
	
	len = (uint16_t) sizeof(ps_data);
	start = (uint16_t) sizeof(ps_header);
	
	if (!gcore_get_nvram_bytes(start, (uint8_t*) &ps_data, len)) {
		ESP_LOGE(TAG, "Failed to read data from RAM");
		return false;
	}
	
	return true;
}


static bool _ps_read_checksum(uint16_t* cs)
{
	uint16_t start;
	
	start = (uint16_t) (sizeof(ps_data) + sizeof(ps_header));
	
	if (!gcore_get_nvram_bytes(start, (uint8_t*) cs, 2)) {
		ESP_LOGE(TAG, "Failed to read checksum from RAM");
		return false;
	}
	
	return true;
}


static bool _ps_write_array()
{
	bool success = true;
	uint16_t cs;
	uint16_t start;
	uint16_t len;
	
	len = (uint16_t) sizeof(ps_header);
	if (!gcore_set_nvram_bytes(0, (uint8_t*) &ps_header, len)) {
		ESP_LOGE(TAG, "Failed to write header from RAM");
		success = false;
	} else {
		len = (uint16_t) sizeof(ps_data);
		start = (uint16_t) sizeof(ps_header);
		if (!gcore_set_nvram_bytes(start, (uint8_t*) &ps_data, len)) {
			ESP_LOGE(TAG, "Failed to write data to RAM");
			success = false;
		} else {
			cs = _ps_compute_checksum();			
			if (!gcore_set_nvram_bytes(start+len, (uint8_t*) &cs, 2)) {
				ESP_LOGE(TAG, "Failed to write checksum to RAM");
				success = false;
			}
		}
	}
	
	return success;
}


static uint16_t _ps_compute_checksum()
{
	int i;
	size_t header_len;
	size_t data_len;
	uint8_t* sP;
	uint16_t cs = 0;
	
	header_len = sizeof(ps_header);
	data_len = sizeof(ps_data);
	
	sP = (uint8_t*) &ps_header;
	for (i=0; i<header_len; i++) {
		cs += *sP++;
	}
	
	sP = (uint8_t*) &ps_data;
	for (i=0; i<data_len; i++) {
		cs += *sP++;
	}
	
	return cs;
}


static bool _ps_validate_checksum(uint16_t cs)
{
	return (cs == _ps_compute_checksum());
}

/*
 * gain - utility module that maps gain values between the following three subsystems
 *   1. Codec ADC and DAC hardware gain settings
 *   2. Application gain range
 *   3. Bluetooth HFP gain range
 *
 * It also provides functions to set the codec ADC and DAC gain.
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
 */
#include "gain.h"
#include "audio_hal.h"
#include <math.h>


//
// Forward declarations for internal functions
//
float _map_ranges(float v, float v_min, float v_max, float to_min, float to_max);
int _gainDB2Codec(int gain_type, float g);



//
// API
//
float gainBT2DB(int gain_type, int bt_val)
{
	if (gain_type == GAIN_TYPE_MIC) {
		return _map_ranges((float) bt_val, GAIN_BT_MIN_VAL, GAIN_BT_MAX_VAL, GAIN_APP_MIC_MIN_DB, GAIN_APP_MIC_MAX_DB);
	} else {
		return _map_ranges((float) bt_val, GAIN_BT_MIN_VAL, GAIN_BT_MAX_VAL, GAIN_APP_SPK_MIN_DB, GAIN_APP_SPK_MAX_DB);
	}
}


int gainDB2BT(int gain_type, float g)
{
	float f;
	int v;
	
	if (gain_type == GAIN_TYPE_MIC) {
		f = _map_ranges(g, GAIN_APP_MIC_MIN_DB, GAIN_APP_MIC_MAX_DB, GAIN_BT_MIN_VAL, GAIN_BT_MAX_VAL);
	} else {
		f = _map_ranges(g, GAIN_APP_SPK_MIN_DB, GAIN_APP_SPK_MAX_DB, GAIN_BT_MIN_VAL, GAIN_BT_MAX_VAL);
	}
	
	v = round(f);
	if (v < GAIN_BT_MIN_VAL) {
		v = GAIN_BT_MIN_VAL;
	} else if (v > GAIN_BT_MAX_VAL) {
		v = GAIN_BT_MAX_VAL;
	}
	
	return v;
}


bool gainSetCodec(int gain_type, float g)
{
	uint32_t v;
	
	v = (uint32_t) _gainDB2Codec(gain_type, g);
	
	if (gain_type == GAIN_TYPE_MIC) {
		return(audio_hal_set_volume(AUDIO_HAL_VOLUME_MIC, v) == ESP_OK);
	} else {
		return(audio_hal_set_volume(AUDIO_HAL_VOLUME_SPK, v) == ESP_OK);
	}
}



//
// Internal functions
//
float _map_ranges(float v, float v_min, float v_max, float to_min, float to_max)
{
	return to_min + ((v - v_min) / (v_max - v_min)) * (to_max - to_min);
}


int _gainDB2Codec(int gain_type, float g)
{
	float f;
	int v;
	
	if (gain_type == GAIN_TYPE_MIC) {
		f = _map_ranges(g, GAIN_CODEC_ADC_MIN_DB, GAIN_CODEC_ADC_MAX_DB, GAIN_CODEC_ADC_MIN_VAL, GAIN_CODEC_ADC_MAX_VAL);
		v = round(f);
		if (v < GAIN_CODEC_ADC_MIN_VAL) {
			v = GAIN_CODEC_ADC_MIN_VAL;
		} else if (v > GAIN_CODEC_ADC_MAX_VAL) {
			v = GAIN_CODEC_ADC_MAX_VAL;
		}
	} else {
		f = _map_ranges(g, GAIN_CODEC_DAC_MIN_DB, GAIN_CODEC_DAC_MAX_DB, GAIN_CODEC_DAC_MIN_VAL, GAIN_CODEC_DAC_MAX_VAL);
		v = round(f);
		if (v < GAIN_CODEC_DAC_MIN_VAL) {
			v = GAIN_CODEC_DAC_MIN_VAL;
		} else if (v > GAIN_CODEC_DAC_MAX_VAL) {
			v = GAIN_CODEC_DAC_MAX_VAL;
		}
	}

	return v;
}

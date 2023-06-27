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
#ifndef _GAIN_H_
#define _GAIN_H_

#include "gui_task.h"
#include <stdbool.h>



//
// Constants
//

// Codec settings and ranges
#define GAIN_CODEC_ADC_MIN_VAL 4
#define GAIN_CODEC_ADC_MAX_VAL 100
#define GAIN_CODEC_ADC_MIN_DB  -84
#define GAIN_CODEC_ADC_MAX_DB  12

#define GAIN_CODEC_DAC_MIN_VAL 4
#define GAIN_CODEC_DAC_MAX_VAL 100
#define GAIN_CODEC_DAC_MIN_DB  -91.5
#define GAIN_CODEC_DAC_MAX_DB  4.5

// Application settings (must take into account Codec capabilities)
#define GAIN_APP_MIC_NOM_DB    0
#define GAIN_APP_MIC_MIN_DB    -39
#define GAIN_APP_MIC_MAX_DB    9

#define GAIN_APP_SPK_NOM_DB    0
#define GAIN_APP_SPK_MIN_DB    -43.5
#define GAIN_APP_SPK_MAX_DB    4.5

// Bluetooth HFP range (from spec)
#define GAIN_BT_MIN_VAL        0
#define GAIN_BT_MAX_VAL        15

// Gain types
#define GAIN_TYPE_MIC          0
#define GAIN_TYPE_SPK          1



//
// API
// 
float gainBT2DB(int gain_type, int bt_val);
int gainDB2BT(int gain_type, float g);
bool gainSetCodec(int gain_type, float g);

#endif /* _GAIN_H_ */ 
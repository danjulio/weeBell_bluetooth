/*
 * Internationalization - Provide access to a data structure describing
 * various attributes of how POTS phones work in different countries.
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
#ifndef _INTERNATIONAL_H_
#define _INTERNATIONAL_H_

#include <stdint.h>

//
// Constants
//

// The index of the default country
#define INT_DEFAULT_COUNTRY     1

// Maximum number of cadence pairs for use generating a tone
#define INT_MAX_TONE_PAIRS      2

// Number of tone sets (dial, re-order, off-hook)
#define INT_NUM_TONE_SETS       3

// Tone set indicies
#define INT_TONE_SET_DIAL_INDEX 0
#define INT_TONE_SET_RO_INDEX   1
#define INT_TONE_SET_OH_INDEX   2

// Caller ID (CID) types (based on international standards, from 
// https://en.wikipedia.org/wiki/Caller_ID)
#define INT_CID_TYPE_NONE         0
#define INT_CID_TYPE_BELLCORE_FSK 1
#define INT_CID_TYPE_ETSI_FSK     2
#define INT_CID_TYPE_SIN227       3
#define INT_CID_TYPE_DTMF         4



//
// Structures making up internationalization entry
//
typedef struct {
	float tone[4];             // Frequency - Hz
	float level;               // Power - dB
	int num_cadence_pairs;     // Number of cadence pairs : 0 -> continuous; max INT_MAX_TONE_PAIRS
	int cadence_pairs[INT_MAX_TONE_PAIRS*2];  // On/Off time pairs in mSec
} tone_info_t;

typedef struct {
	int length;                // Number of entries in sample
	const int16_t* sampleP;    // Pointer to sample array
} sample_info_t;

typedef struct {
	int freq;                  // Frequency Hz
	int num_cadence_pairs;     // Number of cadence pairs : minimum 1, maximum 2
	int cadence_pairs[4];      // On/Off time pairs in mSec
} ring_info_t;

typedef struct {
	char* name;                                   // Country identifier
	uint8_t cid;                                  // Caller ID type
	sample_info_t sample_set[INT_NUM_TONE_SETS];  // Tones generated from sampled audio (set length nonzero to use)
	tone_info_t tone_set[INT_NUM_TONE_SETS];      // Tones generated using DDS (if corresponding sample_set length = 0)
	ring_info_t ring_info;                        // Ring information
	int rotary_map[10];                           // Maps pulses to dialed digit (some phones had reverse order!!!)
} country_info_t;



//
// API
//
int int_get_num_countries();
const country_info_t* int_get_country_info(int n);    // n = 0 .. num_countries - 1

#endif /* _INTERNATIONAL_H_ */
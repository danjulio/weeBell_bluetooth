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
#include "international.h"
#include <stddef.h>
//
// Included samples files
//
#include "aus_dialtone.h"
#include "uk_offhook.h"


//
// Constants
//

// NUM_COUNTRIES must match data structure below
#define NUM_COUNTRIES 4

// Country list - alphabetize
static const country_info_t country_info[] = {
	{"Australia",
	 INT_CID_TYPE_ETSI_FSK,                                     // Caller ID Type
	 {                                                          // Sample generated tones:
	    {AUS_DIALTONE_SAMPLES, snd_aus_dialtone},               //   Dial tone
	    {0, NULL},                                              //   Reorder tone
	    {0, NULL},                                              //   Off-hook tone
	 },
	 {                                                          // DDS generated tones:
		{{0, 0, 0, 0}, 0, 0, {0, 0, 0, 0}},                     //   Dial tone
		{{425, 0, 0, 0}, -10, 1, {200, 200, 0, 0}},             //   Reorder tone
		{{425, 0, 0, 0}, -10, 1, {400, 400, 0, 0}},             //   Off-hook tone (using busy - might be wrong)
	 },
	 {25, 2, {400, 200, 400, 2000}},                            // Ring
	 {1, 2, 3, 4, 5, 6, 7, 8, 9, 0}                             // Rotary map
	},
	
	{"EU - Harmonized",
	 INT_CID_TYPE_ETSI_FSK,                                     // Caller ID Type
	 {                                                          // Sample generated tones:
	    {0, NULL},                                              //   Dial tone
	    {0, NULL},                                              //   Reorder tone
	    {0, NULL},                                              //   Off-hook tone
	 },
	 {                                                          // DDS generated tones:
		{{425, 0, 0, 0}, -10, 0, {0, 0, 0, 0}},                 //   Dial tone
		{{425, 0, 0, 0}, -10, 1, {250, 250, 0, 0}},             //   Reorder tone
		{{1400, 2060, 2450, 2600}, -10, 1, {100, 100, 0, 0}},   //   Off-hook tone (wrong - dunno what it is)
	 },
	 {25, 1, {1000, 0, 0, 0}},                                  // Ring
	 {1, 2, 3, 4, 5, 6, 7, 8, 9, 0}                             // Rotary map
	},
	
	{"United States",
	 INT_CID_TYPE_BELLCORE_FSK,                                 // Caller ID Type
	 {                                                          // Sample generated tones:
	    {0, NULL},                                              //   Dial tone
	    {0, NULL},                                              //   Reorder tone
	    {0, NULL},                                              //   Off-hook tone
	 },
	 {                                                          // DDS generated tones:
		{{350, 440, 0, 0}, -13, 0, {0, 0, 0, 0}},               //   Dial tone
		{{480, 620, 0, 0}, -13, 1, {250, 250, 0, 0}},           //   Reorder tone
		{{1400, 2060, 2450, 2600}, -10, 1, {100, 100, 0, 0}},   //   Off-hook tone
	 },
	 {20, 1, {2000, 0, 0, 0}},                                  // Ring
	 {1, 2, 3, 4, 5, 6, 7, 8, 9, 0}                             // Rotary map
	},
	
	{"United Kingdom",
	 INT_CID_TYPE_SIN227,                                       // Caller ID Type
	 {                                                          // Sample generated tones:
	    {0, NULL},                                              //   Dial tone
	    {0, NULL},                                              //   Reorder tone
	    {UK_OFFHOOK_SAMPLES, snd_uk_offhook},                   //   Off-hook tone
	 },
	 {                                                          // DDS generated tones:
		{{350, 450, 0, 0}, -13, 0, {0, 0, 0, 0}},               //   Dial tone
		{{400, 0, 0, 0}, -10, 2, {400, 350, 225, 525}},         //   Reorder tone
		{{0, 0, 0, 0}, 0, 0, {0, 0, 0, 0}},                     //   Off-hook tone
	 },
	 {25, 2, {400, 200, 400, 200}},                             // Ring
	 {1, 2, 3, 4, 5, 6, 7, 8, 9, 0}                             // Rotary map
	},
};


//
// API
//
int int_get_num_countries()
{
	return NUM_COUNTRIES;
}


const country_info_t* int_get_country_info(int n)
{
	if ((n < 0) || (n >= NUM_COUNTRIES)) {
		return NULL;
	}
	
	return &(country_info[n]);
}

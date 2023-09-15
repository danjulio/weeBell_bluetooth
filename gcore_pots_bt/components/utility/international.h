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

// Caller ID Specification Value
//    Bits 15:8 : Flags
//    Bits  7:4 : Reserved
//    Bits  3:0 : Caller ID Format
// (matching functionality supported by spandsp - see spandsp/adsi.h)
//
// Caller ID Flags - Bits 15:8
#define INT_CID_FLAG_BEFORE_RING  0x8000  /* CID Before first ring */
#define INT_CID_FLAG_EN_LR        0x4000  /* Enable Line Reversal */
#define INT_CID_FLAG_EN_DT_AS     0x2000  /* Enable DT-AS (tone) alert */
#define INT_CID_FLAG_EN_RP_AS     0x1000  /* Enable RP-AS (ring) alert */
#define INT_CID_FLAG_EN_SHORT_PRE 0x0800  /* Enable short preamble */

// Caller ID (CID) Formats - Bits 7:0
#define INT_CID_TYPE_MASK         0x000F
#define INT_CID_TYPE_NONE         0       /* CID Disabled */
#define INT_CID_TYPE_BELLCORE_FSK 1       /* Bellcore FSK format */
#define INT_CID_TYPE_ETSI_FSK     2       /* European FSK format */
#define INT_CID_TYPE_SIN227       3       /* UK - British Telecom */
#define INT_CID_TYPE_DTMF1        4       /* Variant 1 - Most common */
#define INT_CID_TYPE_DTMF2        5       /* Variant 2 */
#define INT_CID_TYPE_DTMF3        6       /* Variant 3 - Taiwan and Kuwait */
#define INT_CID_TYPE_DTMF4        7       /* Variant 4 - Denmark and Holland */
#define INT_CID_TYPE_JCLIP        8       /* NTT Japanese format - not supported */
#define INT_CID_TYPE_ACLIP        9       /* Singapore format */

// Internationalization and Caller ID specification notes
//   1. The period between DT-AS and the CID message is hardwired to 60 mSec by the spandsp
//      library and not specified here.
//   2. When CID before first ring is false (Bellcore, ETSI after first ring):
//      a. The period between the first ring and the start of the CID message
//         is specified by the final Ring Off cadence pair.
//      b. The period between the end of the CID message and enabling of subsequent
//         rings is specified by cid_info_t post_msec.
//   3. When CID before first ring is true (ETSI before first ring, SIN227):
//      a. The period between Line Reversal to DT-AS or RP-AS to CID message is specified
//         by cid_info_t pre_msec.
//      b. The period between the end of the CID message and a CID-triggered ring is
//         specified by cid_info_t post_msec.
//   4. The ring period for an alerting CID RP-AS ring is specified by cid_info_t rp_as_msec
//      (see ETSI EN 300 659-1 for a description of how alerting works).
//   5. Always include an at least short final Ring Off cadence pair.  This is because Caller ID
//      sequences before the first ring trigger a ring when they are done.  It is possible the total
//      time of the Caller ID + Ring is longer than the period between Ring notifications from the
//      host.  This final Ring Off is the time between the end of the Caller ID triggered Ring and
//      the Ring that follows it.
//   6. According to ETSI EN 300 659-1, LR may be combined with DT-AS but not with RP-AS.  DT-AS
//      and RP-AS are mutually exclusive.
//   7. Disable off-hook tone generation by setting off_hook_timeout to 0.  When doing this
//      set the off-hook tone_info_t entry to the same as the dial tone entry but with the
//      level set to -56 (essentially tone off).



//
// Structures making up internationalization entry
//
typedef struct {
	uint16_t cid_spec;         // Caller ID Specification value
	int pre_msec;              // [Optional] "before tone/message audio" delay in mSec
	int post_msec;             // "after tone/message audio" delay in mSec
	int rp_as_msec;            // RP-AS (short ring alert) period in mSec
} cid_info_t;

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
	cid_info_t cid;                               // Caller ID specification
	sample_info_t sample_set[INT_NUM_TONE_SETS];  // Tones generated from sampled audio (set length nonzero to use)
	tone_info_t tone_set[INT_NUM_TONE_SETS];      // Tones generated using DDS (if corresponding sample_set length = 0)
	ring_info_t ring_info;                        // Ring information
	int off_hook_timeout;                         // Timeout (mSec) to generate off-hook tone.  Set to 0 to disable off-hook tone
	int rotary_map[10];                           // Maps pulses to dialed digit (some phones had reverse order!!!)
} country_info_t;



//
// API
//
int int_get_num_countries();
const country_info_t* int_get_country_info(int n);    // n = 0 .. num_countries - 1

#endif /* _INTERNATIONAL_H_ */
/*
 * AG1171/Phone interface module - state management and control for the telephone
 *  - Off/On Hook Detection
 *  - Ring Control
 *  - Dial Tone or No Service tone generation
 *  - Receiver Off Hook tone generation
 *  - Rotary Dial or DTMF dialing support
 *
 * Copyright (c) 2019, 2023 Dan Julio
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "app_task.h"
#include "audio_task.h"
#include "pots_task.h"
#include "international.h"
#include "ps.h"
#include "spandsp.h"
#include "sys_common.h"
#include "time_utilities.h"

//
// Local constants
//

// Uncomment for state debug
#define POTS_STATE_DEBUG
//#define POTS_DIAL_DEBUG
//#define POTS_RING_DEBUG
#define POTS_CID_DEBUG

// State machine evaluation interval
#define POTS_EVAL_MSEC           10

// Make period required to move from off-hook back to on-hook (differentiate from rotary dialing pulses)
#define POTS_ON_HOOK_DETECT_MSEC 500

// Rotary dialing pulse detection
//   Break is maximum period to detect a pulse in a digit
//   Make is minimum period between digits
#define POTS_ROT_BREAK_MSEC      100
#define POTS_ROT_MAKE_MSEC       100

// Post send DTMF tone wait period to allow audio buffers to drain of echoed back audio
// (to prevent it from confusing the echo canceller if it gets switched in)
#define POTS_TONE_FLUSH_MSEC     30

// Post CID message wait period to allow audio buffers to drain of CID before switching to call
#define POTS_CID_FLUSH_MSEC      50

// Maximum number of tone steps
#define POTS_MAX_TONE_STEPS      (INT_MAX_TONE_PAIRS * 2)

// Tone generator buffer size
#define POTS_TONE_BUF_LEN        (8000 * POTS_EVAL_MSEC / 1000)

// DTMF decoder buffer size
#define POTS_DTMF_BUF_LEN        (8000 * POTS_EVAL_MSEC / 1000)



//
// Variables
//

static const char* TAG = "pots_task";

// Country code
static uint8_t country_code;
static const country_info_t* country_code_infoP;

// Call state
static bool pots_in_service = false;       // Set if the phone can make a call, clear if it isn't in service
static bool pots_has_call_audio = false;   // Set when the external process has a connected phone call (used to suppress off-hook tone, enable audio)
static bool pots_call_audio_16k;           // Set when connected phone call is using 16k samples/sec (false for 8k samples/sec)

// Hook logic
typedef enum {ON_HOOK, OFF_HOOK, ON_HOOK_PROVISIONAL} pots_stateT;
#ifdef POTS_STATE_DEBUG
static const char* pots_state_name[] = {"ON_HOOK", "OFF_HOOK", "ON_HOOK_PROVISIONAL"};
#endif
static pots_stateT pots_state = ON_HOOK;
static int pots_state_count;                     // Down counter for phone state change detection
static bool pots_cur_off_hook = false;           // Debounced off-hook state
static bool pots_saw_hook_state_change = false;  // For API notification

// Ring logic
static bool pots_do_not_disturb = false;         // Inhibits ringing
static bool pots_trigger_pots_ring = false;      // Trigger a ring associated with an incoming call
static bool pots_trigger_cid_ring = false;       // Trigger a RP-AS alert ring associated with Caller ID
typedef enum {RING_IDLE, RING_PULSE_ON, RING_PULSE_OFF, RING_STEP_WAIT} pots_ring_stateT;
#ifdef POTS_RING_DEBUG
static const char* pots_ring_state_name[] = {"RING_IDLE", "RING_PULSE_ON", "RING_PULSE_OFF", "RING_STEP_WAIT"};
#endif
static pots_ring_stateT pots_ring_state = RING_IDLE;
static int pots_num_ring_steps;          // Number of steps in a ring (at least 2 for a single ON/OFF)
static int pots_ring_step;               // The current cadence step
static int pots_ring_period_count;       // Counts down evaluation cycles for each ringing state
static int pots_ring_pulse_count;        // Counts down pulses in one ring ON or OFF portion
static int pots_ring_num = 0;            // Number of rings starting with 0 (used by CID)

// Dialing logic
typedef enum {DIAL_IDLE, DIAL_BREAK, DIAL_MAKE} pots_dial_stateT;
#ifdef POTS_DIAL_DEBUG
static const char* pots_dial_state_name[] = {"DIAL_IDLE", "DIAL_BREAK", "DIAL_MAKE"};
#endif
static pots_dial_stateT pots_dial_state = DIAL_IDLE;
static int pots_dial_period_count;       // Counts up evaluation cycles for each dialing state
static int pots_dial_pulse_count;        // Counts pulses from the rotary dial for one digit
static char pots_dial_cur_digit;         // 0 - 9, A - D, *, #
static char pots_dial_last_dtmf_digit = ' ';

// Tone generation logic
typedef enum {TONE_IDLE, TONE_VOICE, TONE_VOICE_WAIT_HANGUP, TONE_DIAL, TONE_DIAL_QUIET,
              TONE_DTMF, TONE_DTMF_FLUSH, TONE_NO_SERVICE, TONE_OFF_HOOK, TONE_CID, TONE_CID_FLUSH
             } pots_tone_stateT;
#ifdef POTS_STATE_DEBUG
static const char* pots_tone_state_name[] = {"TONE_IDLE", "TONE_VOICE", "TONE_VOICE_WAIT_HANGUP",
                                             "TONE_DIAL", "TONE_DIAL_QUIET", "TONE_DTMF", 
                                             "TOND_DIAL_FLUSH", "TONE_NO_SERVICE", "TONE_OFF_HOOK",
                                             "TONE_CID", "TONE_CID_FLUSH"
                                            };
#endif
static pots_tone_stateT pots_tone_state = TONE_IDLE;
static int pots_tone_timer_count = 0;             // Evaluation down count timer for tone logic
static bool pots_notify_ext_digit_dialed = false; // Set by notification when another task dials a digit
                                                  // (used to suppress dial tone and generate DTMF here)

// Caller ID logic
static bool pots_trigger_cid = false;
typedef enum {CID_IDLE, CID_RP_AS, CID_PRE_MSG_WAIT, CID_MSG, CID_POST_MSG_WAIT} pots_cid_stateT;
#ifdef POTS_CID_DEBUG
static const char* pots_cid_state_name[] = {"CID_IDLE", "CID_RP_AS", "CID_PRE_MSG_WAIT", "CID_MSG", "CID_POST_MSG_WAIT"};
#endif
static pots_cid_stateT pots_cid_state = CID_IDLE;
static int pots_cid_wait_count;           // Counts down evaluation cycles for each CID delay
static adsi_tx_state_t* cid_tx_stateP;
static uint8_t adsi_msg_buf[64];          // Buffer to hold complete CID message for spandsp
                                          // must be larger that maximum message (date + caller phone #)

// DDS Tone generator
static int16_t tone_tx_buf[POTS_TONE_BUF_LEN];
static super_tone_tx_step_t* tone_step[INT_NUM_TONE_SETS][POTS_MAX_TONE_STEPS];
static super_tone_tx_state_t tone_state;

// Sample-based Tone generator
static int sample_tone_tx_length[INT_NUM_TONE_SETS];   // Non-zero values cause us to use samples instead of DDS generated
static const int16_t* sample_tone_tx_bufP[INT_NUM_TONE_SETS];
static const int16_t* sample_tone_tx_cur_buf;
static int sample_tone_tx_cur_len;
static int sample_tone_tx_index;

// Tone generation type flag
static bool tone_tx_use_sample;

// DTMF Decoder
static int16_t dtmf_rx_buf[POTS_DTMF_BUF_LEN];
static dtmf_rx_state_t dtmf_rx_state;

// DTMF Encoder
static char dtmf_tx_digit_buf[2];          // DTMF character to generate a tone for + null
static dtmf_tx_state_t dtmf_tx_state;


//
// Forward declarations
//
static void _potsInitGPIO();
static void _potsInitTones(bool init);
static void _potsLineReverse(bool en);
static void _potsLineRingMode(bool en);
static void _potsHandleNotifications();
static bool _potsEvalHook();
static void _potsEvalPhoneState(bool hookChange);
static void _potsEvalRinger();
static void _potsStartRing(bool is_rp_as);
static void _potsEndRing();
static int _potsGetRingPulseCount(bool on_portion);
static void _potsEvalCID();
static bool _potsSetupCID();
static int _potsLocaleToCIDstandard();
static bool _potsEvalDialer(bool hookChange);
static void _potsSetToneState(pots_tone_stateT ns);
static void _potsEvalToneState(bool potsDigitDialed, bool appDigitDialed);
static bool _potsEvalToneGen();
static bool _potsToneTimerExpired();
static void _potsSendDialedDigit(char d);
static void _potsSetAudioOutput(pots_tone_stateT s);
static void _potsSetupAudioTone(int tone_index);
static void _potsEvalDtmfDetect();
static void _potsDtmfCallback(void *data, const char *digits, int len);



//
// API
//
void pots_task(void* args)
{
	bool hook_changed;       // Debounced hook output changed
	bool pots_digit_dialed;  // Set when a digit is detected having been dialed on the POTS phone
  
	
  	ESP_LOGI(TAG, "Start task");
  	
	// Country code configures what tones and patterns we generate
	country_code = ps_get_country_code();
	if (country_code >= int_get_num_countries()) {
		country_code = 0;
		ps_set_country_code(country_code);
	}
	country_code_infoP = int_get_country_info(country_code);
	ESP_LOGI(TAG, "Country: %s", country_code_infoP->name);
		
	// configure GPIO
	_potsInitGPIO();
	
	// Initialize our outgoing tone set (pre-allocate tone step info so we're
	// not always needing to free and potentially fragment heap)
	_potsInitTones(true);
	
	// Initialize our Caller ID data structure here so it will pre-allocate memory
	// at the beginning of time
	cid_tx_stateP = adsi_tx_init(NULL, _potsLocaleToCIDstandard());
		
	while (true) {
		// Look for notifications from other tasks
		_potsHandleNotifications();
				
		// Evaluate hardware for changes
		hook_changed = _potsEvalHook();
		
		// Evaluate hook state
		_potsEvalPhoneState(hook_changed);
		
		// Evaluate DTMF tone detection
		_potsEvalDtmfDetect();
		
		// Evaluate our output state
		_potsEvalRinger();
		pots_digit_dialed = _potsEvalDialer(hook_changed);
		_potsEvalCID();
		_potsEvalToneState(pots_digit_dialed, pots_notify_ext_digit_dialed);
		
		if (pots_digit_dialed) {
			_potsSendDialedDigit(pots_dial_cur_digit);
#ifdef POTS_STATE_DEBUG
			ESP_LOGI(TAG, "Dial %c", pots_dial_cur_digit);
#endif
		}
		
		// Clear notifications
		pots_notify_ext_digit_dialed = false;
		
		vTaskDelay(pdMS_TO_TICKS(POTS_EVAL_MSEC));
	}
}


void pots_set_app_dialed_digit(char d)
{
	// Make a one-character string
	dtmf_tx_digit_buf[0] = d;
	dtmf_tx_digit_buf[1] = 0;
}



//
// Internal functions
//

static void _potsHandleNotifications()
{
	uint32_t notification_value = 0;
	
	// Handle notifications (clear them upon reading)
	if (xTaskNotifyWait(0x00, 0xFFFFFFFF, &notification_value, 0)) {
		//
		// Service available
		//
		if (Notification(notification_value, POTS_NOTIFY_IN_SERVICE_MASK)) {
			pots_in_service = true;
		}
		if (Notification(notification_value, POTS_NOTIFY_OUT_OF_SERVICE_MASK)) {
			pots_in_service = false;
		}
		
		//
		// Call status
		//
		if (Notification(notification_value, POTS_NOTIFY_AUDIO_8K_MASK)) {
			pots_has_call_audio = true;
		}
		if (Notification(notification_value, POTS_NOTIFY_AUDIO_16K_MASK)) {
			pots_has_call_audio = true;
			pots_call_audio_16k = true;
		}
		if (Notification(notification_value, POTS_NOTIFY_AUDIO_DIS_MASK)) {
			pots_has_call_audio = false;
			pots_call_audio_16k = false;
		}
		
		//
		// Ring management
		//
		if (Notification(notification_value, POTS_NOTIFY_MUTE_RING_MASK)) {
			pots_do_not_disturb = true;
		}
		if (Notification(notification_value, POTS_NOTIFY_UNMUTE_RING_MASK)) {
			pots_do_not_disturb = false;
		}
		if (Notification(notification_value, POTS_NOTIFY_RING_MASK)) {
			if (!pots_do_not_disturb) {
				if ((pots_ring_num == 0) &&
				    (country_code_infoP->cid.cid_spec & INT_CID_TYPE_MASK) &&
				    (country_code_infoP->cid.cid_spec & INT_CID_FLAG_BEFORE_RING)) {
				    
					// Generate Caller ID information instead of a ring if this is the first ring
					// of a phone call and the country information requires CID before ring
					pots_trigger_cid = true;
#ifdef POTS_CID_DEBUG
					ESP_LOGI(TAG, "Pre-ring CID trigger");
#endif
				} else {
					// Start a ring
					pots_trigger_pots_ring = true;
				}
			}
		}
		if (Notification(notification_value, POTS_NOTIFY_DONE_RINGING_MASK)) {
			// Reset the ring count when app_task determines a call we haven't picked
			// up is over
			pots_ring_num = 0;
		}
		
		//
		// External dialing
		//
		if (Notification(notification_value, POTS_NOTIFY_EXT_DIAL_DIGIT_MASK)) {
			pots_notify_ext_digit_dialed = true;   // Must be cleared during evaluation
		}
		
		//
		// State
		//
		if (Notification(notification_value, POTS_NOTIFY_NEW_COUNTRY_MASK)) {
			country_code = ps_get_country_code();
			country_code_infoP = int_get_country_info(country_code);
			ESP_LOGI(TAG, "New Country: %s", country_code_infoP->name);
					
			// Re-initialize tone set
			_potsInitTones(false);
			
			// Re-initialize tone state if we're in the middle of generating a tone
			if (pots_tone_state == TONE_DIAL){
				_potsSetupAudioTone(INT_TONE_SET_DIAL_INDEX);
			} else if (pots_tone_state == TONE_NO_SERVICE) {
				_potsSetupAudioTone(INT_TONE_SET_RO_INDEX);
			} else if (pots_tone_state == TONE_OFF_HOOK) {
				_potsSetupAudioTone(INT_TONE_SET_OH_INDEX);
			}
		}
	}
}


static void _potsInitGPIO()
{
	// Ring (RM) pin - output, default low
	gpio_reset_pin(PIN_RM);
	gpio_set_direction(PIN_RM, GPIO_MODE_OUTPUT);
	gpio_set_level(PIN_RM, 0);
	
	// Forward/Reversion (FR) pin - output, default high
	gpio_reset_pin(PIN_FR);
	gpio_set_direction(PIN_FR, GPIO_MODE_OUTPUT);
	gpio_set_level(PIN_FR, 1);
	
	// Switch Hook (SHK) pin - input, high = off-hook
	gpio_reset_pin(PIN_SHK);
	gpio_set_direction(PIN_SHK, GPIO_MODE_INPUT);
}


static void _potsInitTones(bool init)
{
	int cycles;
	int i, j;
	int steps;
	float t1, t2, t3, t4;
	const tone_info_t* cur_tone_set;
	super_tone_tx_step_t* prev_tone_step;
	
	// Always load all tone (both sample and DDS generated) from the current country
	// data structure
	for (i=0; i<INT_NUM_TONE_SETS; i++) {
		// Sample generated info
		sample_tone_tx_bufP[i] = country_code_infoP->sample_set[i].sampleP;
		if (sample_tone_tx_bufP[i] != NULL) {
			sample_tone_tx_length[i] = country_code_infoP->sample_set[i].length;
		} else {
			// Just a safety...
			sample_tone_tx_length[i] = 0;
		}
		
		// DDS generated info
		cur_tone_set = &(country_code_infoP->tone_set[i]);
		
		// Get number of valid steps in the tone sequence
		if (cur_tone_set->num_cadence_pairs == 0) {
			steps = 1;
		} else {
			steps = cur_tone_set->num_cadence_pairs * 2;
		}
	
		// Load each entry
		prev_tone_step = NULL;
		for (j=0; j<POTS_MAX_TONE_STEPS; j++) {  // j is step 0, 2.. Max-1
			if (j < steps) {
				// First tone step repeats forever, linked ones only execute once
				cycles = (j == 0) ? 0 : 1;
				
				// Even tone steps have a tone, Odd tone steps are silent
				if ((j & 0x1) == 0) {
					t1 = cur_tone_set->tone[0];
					t2 = cur_tone_set->tone[1];
					t3 = cur_tone_set->tone[2];
					t4 = cur_tone_set->tone[3];
				} else {
					t1 = 0;
					t2 = 0;
					t3 = 0;
					t4 = 0;
				}
				
				// Load valid tone info
				if (init) {
					// Initialize and perform the initial malloc of the actual data structure
					tone_step[i][j] = super_tone_tx_make_step_4(NULL, 
															    t1,
															    t2,
															    t3,
															    t4,
															    cur_tone_set->level,
															    cur_tone_set->cadence_pairs[j],
															    cycles);
				} else {
					// Initialize but reuse previous memory allocation
					tone_step[i][j] = super_tone_tx_make_step_4(tone_step[i][j], 
															    t1,
															    t2,
															    t3,
															    t4,
															    cur_tone_set->level,
															    cur_tone_set->cadence_pairs[j],
															    cycles);
				}
				
				// Link to previous step
				if (j > 0) {
					prev_tone_step->nest = tone_step[i][j];
				}
			} else {
				// No valid tone for this entry - but allocate it if it does not exist (some future selected
				// country code may use it)
				if (init) {
					tone_step[i][j] = super_tone_tx_make_step_4(NULL, 0, 0, 0, 0, 0, 0, 0);
				}
				
				// Previous tone step has no link
				if (j > 0) {
					prev_tone_step->nest = NULL;
				}
			}
			prev_tone_step = tone_step[i][j];
		}
	}
}


static void _potsLineReverse(bool en)
{
	gpio_set_level(PIN_FR, en ? 0 : 1);
}


static void _potsLineRingMode(bool en)
{
	gpio_set_level(PIN_RM, en ? 1 : 0);
}


// Updates current hook switch state
//   returns true if the state changes, false otherwise
static bool _potsEvalHook() {
	bool cur_hw_off_hook;
	bool changed_detected = false;
	static bool pots_prev_off_hook = false;          // Previous state for off-hook debounce
	
	// Get current hardware signal
	cur_hw_off_hook = (gpio_get_level(PIN_SHK) == 1);
	
	// Look for debounced transitions
	if (cur_hw_off_hook && pots_prev_off_hook && !pots_cur_off_hook) {
		changed_detected = true;
		pots_cur_off_hook = true;
	} else if (!cur_hw_off_hook && !pots_prev_off_hook && pots_cur_off_hook) {
		changed_detected = true;
		pots_cur_off_hook = false;
	}
	
	// Update state
	pots_prev_off_hook = cur_hw_off_hook;
	
	return changed_detected;
}


// This state machine determines when we're on hook (otherwise we're either off hook
// or on hook only temporarily as the rotary dial switches)
static void _potsEvalPhoneState(bool hookChange)
{
#ifdef POTS_STATE_DEBUG
	static pots_stateT prev_pots_state = ON_HOOK;
#endif
	pots_saw_hook_state_change = false;
	
	switch (pots_state) {
		case ON_HOOK:
			if (hookChange && pots_cur_off_hook) {
				pots_state = OFF_HOOK;
				pots_saw_hook_state_change = true;
			}
			break;
		  
		case OFF_HOOK:
			if (hookChange && !pots_cur_off_hook) {
				// Back on-hook - it could be permanent or the start of a rotary dial
				pots_state = ON_HOOK_PROVISIONAL;
				pots_state_count = 0; // Initialize end-of-call (back on-hook) timer
			}
			break;
		  
		case ON_HOOK_PROVISIONAL:
			// Increment timer so we can decide where to go on next action
			++pots_state_count;
		
			if (hookChange && pots_cur_off_hook) {
				pots_state = OFF_HOOK;
			} else {
				if (pots_state_count >= (POTS_ON_HOOK_DETECT_MSEC / POTS_EVAL_MSEC)) {
					// Call has ended
					pots_state = ON_HOOK;
					pots_saw_hook_state_change = true;
				}
			}
			break;
	}
    
    if (pots_saw_hook_state_change) {
    	if (pots_state == ON_HOOK) {
    		xTaskNotify(task_handle_app, APP_NOTIFY_POTS_ON_HOOK_MASK, eSetBits);
    	} else {
    		xTaskNotify(task_handle_app, APP_NOTIFY_POTS_OFF_HOOK_MASK, eSetBits);
    	}
    }
#ifdef POTS_STATE_DEBUG
	STATE_CHANGE_PRINT(prev_pots_state, pots_state, pots_state_name);
	prev_pots_state = pots_state;
	if (pots_saw_hook_state_change) {
		ESP_LOGI(TAG, "   Hook State Change");
	}
#endif
}


static void _potsEvalRinger()
{
#ifdef POTS_RING_DEBUG
	static pots_dial_stateT prev_pots_ring_state = RING_IDLE;
#endif

	if (pots_state == OFF_HOOK) {		
		// End ringing if necessary
		if (pots_ring_state != RING_IDLE) {
			_potsEndRing();
		}
		
		// Always reset ring count when we go off hook and after ending ringing
		pots_ring_num = 0;
	}
	
	switch (pots_ring_state) {
		case RING_IDLE:
			if (pots_state == ON_HOOK) {
				if (pots_trigger_pots_ring) {
					pots_trigger_pots_ring = false;
					_potsStartRing(false);
				} else if (pots_trigger_cid_ring) {
					pots_trigger_cid_ring = false;
					_potsStartRing(true);
				}
			} else {
				// Throw away any requests when we go off-hook
				pots_trigger_pots_ring = false;
				pots_trigger_cid_ring = false;
			}
			break;
		
		case RING_PULSE_ON:
			// Decrement ring-on timer
			pots_ring_period_count--;
			if (--pots_ring_pulse_count <= 0) {
				// Ring pulse done
				pots_ring_state = RING_PULSE_OFF;
				pots_ring_pulse_count = _potsGetRingPulseCount(false);  // Off for half a pulse
				_potsLineReverse(false);
			}
			break;
		
		case RING_PULSE_OFF:
			// Decrement ring-off timer and pulse timer
			pots_ring_period_count--;
			pots_ring_pulse_count--;
			// Either timer expiring ends this half of the pulse
			if ((pots_ring_period_count <= 0) || (pots_ring_pulse_count <= 0)) {
				if (pots_ring_period_count <= 0) {
					// End of ring - check if there are more rings in this sequence
					if (++pots_ring_step >= pots_num_ring_steps) {
						_potsEndRing();
					} else {
						pots_ring_state = RING_STEP_WAIT;
						pots_ring_period_count = country_code_infoP->ring_info.cadence_pairs[pots_ring_step] / POTS_EVAL_MSEC;
						
						// Switch off ring mode when not actually ringing
						_potsLineRingMode(false);
					}
				} else {
					// Setup next ring pulse in this ring
					pots_ring_state = RING_PULSE_ON;
					pots_ring_pulse_count = _potsGetRingPulseCount(true);
					_potsLineReverse(true);
				}
			}
			break;
		
		case RING_STEP_WAIT:
			if (--pots_ring_period_count <= 0) {
				// End of wait
				if (++pots_ring_step >= pots_num_ring_steps) {
					// No more rings
					_potsEndRing();
				} else {
					// Next ring in sequence
					pots_ring_state = RING_PULSE_ON;
					pots_ring_period_count = country_code_infoP->ring_info.cadence_pairs[pots_ring_step] / POTS_EVAL_MSEC;
					pots_ring_pulse_count = _potsGetRingPulseCount(true);
					
					// Switch ring mode back on
					_potsLineRingMode(true);
				}
			}
			break;
	}	
	
#ifdef POTS_RING_DEBUG
	STATE_CHANGE_PRINT(prev_pots_ring_state, pots_ring_state, pots_ring_state_name);
	prev_pots_ring_state = pots_ring_state;
#endif
}


static void _potsStartRing(bool is_rp_as)
{	
	if (is_rp_as) {
		// Special CID Ring Alert (RP-AS) - See ETSI EN 300 659-1
		pots_num_ring_steps = 1;
		pots_ring_period_count = country_code_infoP->cid.rp_as_msec / POTS_EVAL_MSEC;
	} else {
		// Normal ring
		pots_num_ring_steps = country_code_infoP->ring_info.num_cadence_pairs * 2;
		pots_ring_period_count = country_code_infoP->ring_info.cadence_pairs[0] / POTS_EVAL_MSEC;
	}
	
	pots_ring_pulse_count = _potsGetRingPulseCount(true);
	pots_ring_step = 0;
	pots_ring_state = RING_PULSE_ON;
	pots_ring_num += 1;
	_potsLineRingMode(true);   // Cause the line to enter ring mode
	_potsLineReverse(true);    // Toggle the line (reverse) to start this pulse of the ring
}


static void _potsEndRing()
{
	pots_ring_state = RING_IDLE;
	_potsLineReverse(false);   // Make sure tip/ring are not reversed
	_potsLineRingMode(false);  // Exit ring mode
	
	// Look to start caller ID after first ring if required
	if ((pots_ring_num == 1) &&
	    (country_code_infoP->cid.cid_spec & INT_CID_TYPE_MASK) &&
	    ((country_code_infoP->cid.cid_spec & INT_CID_FLAG_BEFORE_RING) == 0)) {
	    
		pots_trigger_cid = true;
#ifdef POTS_CID_DEBUG
		ESP_LOGI(TAG, "Post-ring CID trigger");
#endif
	}
}


static int _potsGetRingPulseCount(bool on_portion)
{
	int ring_pulse_period_msec;
	int ring_on_eval_counts;
	
	// Get the period for one pulse (both ON then OFF)
	ring_pulse_period_msec = 1000 / country_code_infoP->ring_info.freq;
	
	// Determine how many evaluation cycles for the ON portion - it is nominally 1/2 of the full period divided by our evaluation interval
	// but this may end up slightly off because 1/2 of the full period may not be evenly divisible by the evaluation interval
	ring_on_eval_counts = ring_pulse_period_msec / 2 / POTS_EVAL_MSEC;
	
	if (on_portion) {
		return ring_on_eval_counts;
	} else {
		// We handle the case where the ON cycles weren't exactly right by compensating in the OFF portion
		ring_pulse_period_msec = ring_pulse_period_msec - (ring_on_eval_counts * POTS_EVAL_MSEC);
		return (ring_pulse_period_msec / POTS_EVAL_MSEC);
	}
}


static void _potsEvalCID()
{
#ifdef POTS_CID_DEBUG
	static pots_cid_stateT prev_pots_cid_state = CID_IDLE;
#endif
	// Stop caller ID if the phone goes off hook
	if (pots_state != ON_HOOK) {
		// Clean any pending caller ID
		pots_trigger_cid = false;
		
		// End CID if in progress
		if (pots_cid_state != CID_IDLE) {
			// Reset our state
			pots_cid_state = CID_IDLE;		
			_potsLineReverse(false);
		}
	}

	switch (pots_cid_state) {
		case CID_IDLE:
			if ((pots_state == ON_HOOK) && pots_trigger_cid) {
				pots_trigger_cid = false;
				
				// Setup the spandsp library caller ID audio generator 
				if (_potsSetupCID()) {					
					// Determine how to start caller ID based on country information
					if ((country_code_infoP->cid.cid_spec & INT_CID_FLAG_BEFORE_RING)) {
						// Before first ring
						if (country_code_infoP->cid.cid_spec & INT_CID_FLAG_EN_LR) {
							// Reverse Line
							_potsLineReverse(true);
							
							// Setup timer for wait before CID audio
							pots_cid_wait_count = country_code_infoP->cid.pre_msec / POTS_EVAL_MSEC;
							pots_cid_state = CID_PRE_MSG_WAIT;
						} else if (country_code_infoP->cid.cid_spec & INT_CID_FLAG_EN_RP_AS) {
							// Start short ring
							pots_trigger_cid_ring = true;
							pots_cid_state = CID_RP_AS;
						} else {
							// Start CID audio
							_potsSetToneState(TONE_CID);
							pots_cid_state = CID_MSG;
						}
					} else {
						// Just start CID audio when after first ring
						_potsSetToneState(TONE_CID);
						pots_cid_state = CID_MSG;
					}
				} else {
					// No message to send (e.g. blocked number and the selected standard
					// has no way to indicate that).  Trigger subsequent ring if necessary
					if ((country_code_infoP->cid.cid_spec & INT_CID_FLAG_BEFORE_RING)) {
						pots_trigger_pots_ring = true;
					}
				}
			}
			break;
			
		case CID_RP_AS: // Generating RP-AS (ring) alert
			// Wait for ring to complete
			if (pots_ring_state == RING_IDLE) {
				// Setup delay before CID audio
				pots_cid_wait_count = country_code_infoP->cid.pre_msec / POTS_EVAL_MSEC;
				pots_cid_state = CID_PRE_MSG_WAIT;
			}
			break;
			
		case CID_PRE_MSG_WAIT:  // Waiting to start CID audio
			if (--pots_cid_wait_count <= 0) {
				// Start CID audio
				_potsSetToneState(TONE_CID);
				pots_cid_state = CID_MSG;
			}
			break;
			
		case CID_MSG:  // Generating Caller ID message audio
			// Wait for message to complete
			if (pots_tone_state != TONE_CID) {
				// Setup the post CID timeout
				pots_cid_wait_count = country_code_infoP->cid.post_msec / POTS_EVAL_MSEC;
				pots_cid_state = CID_POST_MSG_WAIT;
			}
			break;
			
		case CID_POST_MSG_WAIT:  // Waiting after Caller ID before allowing or enabling ring
			// Can't start any subsequent ring until out of this state
			if (--pots_cid_wait_count <= 0) {
				if (country_code_infoP->cid.cid_spec & INT_CID_FLAG_EN_LR) {
					// Set normal line polarity (for the case we reversed it)
					_potsLineReverse(false);
				}
				
				if ((country_code_infoP->cid.cid_spec & INT_CID_FLAG_BEFORE_RING)) {
					// Start the first ring if CID was sent before first ring
					pots_trigger_pots_ring = true;
				}
				
				pots_cid_state = CID_IDLE;
			}
			break;
	}

#ifdef POTS_CID_DEBUG
	STATE_CHANGE_PRINT(prev_pots_cid_state, pots_cid_state, pots_cid_state_name);
	prev_pots_cid_state = pots_cid_state;
#endif
}


static bool _potsSetupCID()
{
	bool valid_cid = true;
	char cid_buf[33];
	char time_buf[9];
	int cid_buf_len;
	int len = -1;
	tmElements_t tm;
	
	// Setup the state based on current locale
	(void) adsi_tx_init(cid_tx_stateP, _potsLocaleToCIDstandard());
	
	// Configure DT-AS if necessary
	if ((country_code_infoP->cid.cid_spec & INT_CID_FLAG_EN_DT_AS)) {
		adsi_tx_send_alert_tone(cid_tx_stateP);
	}
	
	// Change the caller ID message pre-amble if necessary
	
	if ((country_code_infoP->cid.cid_spec & INT_CID_TYPE_MASK) == INT_CID_TYPE_BELLCORE_FSK) {
		// BellCore spec wants 156 or 180 Mark bits (depending on what document you read)
		// after preamble but adsi.c does 80 by default so we reset that here
		adsi_tx_set_preamble(cid_tx_stateP, -1, 156, -1, -1);
	} else if ((country_code_infoP->cid.cid_spec & INT_CID_FLAG_EN_SHORT_PRE) == 0) {
		// ETSI EN 300 659-1 specifies normal pre-amble to be 180 Mark bits so once again
		// we override the adsi.c default 80 (short pre-amble)
		adsi_tx_set_preamble(cid_tx_stateP, -1, 180, -1, -1);
	}
	
	// Get message strings
	cid_buf_len = app_get_cid_number(cid_buf);
	if (cid_buf_len == 0) {
		sprintf(cid_buf, UNKNOWN_CID_STRING);
		cid_buf_len = strlen(cid_buf);
		valid_cid = false;
	}
	time_get(&tm);
	time_get_cid_string(tm, time_buf);
	ESP_LOGI(TAG, "CID Time: %s  Message: %s", time_buf, cid_buf);
	
	// Set the message
	switch (country_code_infoP->cid.cid_spec & INT_CID_TYPE_MASK) {
		case INT_CID_TYPE_ETSI_FSK:
		case INT_CID_TYPE_SIN227:
			// ETSI and SIN227 FSK MDMF format
			len = adsi_add_field(cid_tx_stateP, adsi_msg_buf, len, CLIP_MDMF_CALLERID, NULL, 0);
            len = adsi_add_field(cid_tx_stateP, adsi_msg_buf, len, CLIP_CALLTYPE, (uint8_t *) "\x81", 1);
            len = adsi_add_field(cid_tx_stateP, adsi_msg_buf, len, CLIP_DATETIME, (uint8_t *) time_buf, 8);
            if (valid_cid) {
            	len = adsi_add_field(cid_tx_stateP, adsi_msg_buf, len, CLIP_CALLER_NUMBER, (uint8_t *) cid_buf, cid_buf_len);
            } else {
            	len = adsi_add_field(cid_tx_stateP, adsi_msg_buf, len, CLIP_ABSENCE1, (uint8_t *) "O", 1);
            }
			break;
		
		case INT_CID_TYPE_DTMF1:
			// Format: A<caller's phone number>D<redirected number>B<special information>C
			//   Special information codes are defined:
			//    - "00" indicates the calling party number is not available.
			//    - "10" indicates that the presentation of the calling party number is restricted.
			len = adsi_add_field(cid_tx_stateP, adsi_msg_buf, len, CLIP_DTMF_C_TERMINATED, NULL, 0);
			if (valid_cid) {
				len = adsi_add_field(cid_tx_stateP, adsi_msg_buf, len, CLIP_DTMF_C_CALLER_NUMBER, (uint8_t *) cid_buf, cid_buf_len);
			} else {
				len = adsi_add_field(cid_tx_stateP, adsi_msg_buf, len, CLIP_DTMF_C_ABSENCE, (uint8_t *) "10", 2);
			}
			break;
			
		case INT_CID_TYPE_DTMF2:
			// Format: A<caller's phone number>#
			//  - D1#     Number not available because the caller has restricted it.
			//  - D2#     Number not available because the call is international.
			//  - D3#     Number not available due to technical reasons.
			len = adsi_add_field(cid_tx_stateP, adsi_msg_buf, len, CLIP_DTMF_HASH_TERMINATED, NULL, 0);
			if (valid_cid) {
				len = adsi_add_field(cid_tx_stateP, adsi_msg_buf, len, CLIP_DTMF_HASH_CALLER_NUMBER, (uint8_t *) cid_buf, cid_buf_len);
			} else {
				len = adsi_add_field(cid_tx_stateP, adsi_msg_buf, len, CLIP_DTMF_HASH_ABSENCE, (uint8_t *) "1", 1);
			}
			break;
		
		case INT_CID_TYPE_DTMF3:
			// Format: D<caller's phone number>C
			if (valid_cid) {
				len = adsi_add_field(cid_tx_stateP, adsi_msg_buf, len, CLIP_DTMF_C_TERMINATED, NULL, 0);
				len = adsi_add_field(cid_tx_stateP, adsi_msg_buf, len, CLIP_DTMF_C_REDIRECT_NUMBER, (uint8_t *) cid_buf, cid_buf_len);
			}
			break;
			
		case INT_CID_TYPE_DTMF4:
			// Format: <caller's phone number>#
			if (valid_cid) {
				len = adsi_add_field(cid_tx_stateP, adsi_msg_buf, len, CLIP_DTMF_HASH_TERMINATED, NULL, 0);
            	len = adsi_add_field(cid_tx_stateP, adsi_msg_buf, len, CLIP_DTMF_HASH_UNSPECIFIED, (uint8_t *) cid_buf, cid_buf_len);
			}
			break;
		
		case INT_CID_TYPE_JCLIP:
			// Japanese NTT FSK MDMF format (this isn't really right as it doesn't implement the multi-call functionality)
			len = adsi_add_field(cid_tx_stateP, adsi_msg_buf, len, JCLIP_MDMF_CALLERID, NULL, 0);
			if (valid_cid) {
				len = adsi_add_field(cid_tx_stateP, adsi_msg_buf, len, JCLIP_DIALED_NUMBER, (uint8_t *) cid_buf, cid_buf_len);
			} else {
				len = adsi_add_field(cid_tx_stateP, adsi_msg_buf, len, JCLIP_ABSENCE, (uint8_t *) "O", 1);
			}
			break;
		
		case INT_CID_TYPE_ACLIP:
			// Singapore FSK MDMF format
			len = adsi_add_field(cid_tx_stateP, adsi_msg_buf, len, ACLIP_MDMF_CALLERID, NULL, 0);
			len = adsi_add_field(cid_tx_stateP, adsi_msg_buf, len, ACLIP_DATETIME, (uint8_t *) time_buf, 8);
			if (valid_cid) {
				len = adsi_add_field(cid_tx_stateP, adsi_msg_buf, len, ACLIP_CALLER_NUMBER, (uint8_t *) cid_buf, cid_buf_len);
			} else {
				len = adsi_add_field(cid_tx_stateP, adsi_msg_buf, len, ACLIP_NUMBER_ABSENCE, (uint8_t *) "O", 1);
			}
			break;
		
		default:
			if (valid_cid) {
				// BellCore FSK SDMF Format when we have the number
				len = adsi_add_field(cid_tx_stateP, adsi_msg_buf, len, CLASS_SDMF_CALLERID, NULL, 0);
				len = adsi_add_field(cid_tx_stateP, adsi_msg_buf, len, 0, (uint8_t *) time_buf, 8);
				len = adsi_add_field(cid_tx_stateP, adsi_msg_buf, len, 0, (uint8_t *) cid_buf, cid_buf_len);
			} else {
				// MDMF Format when we don't have a number to try to deliver the reason why
				len = adsi_add_field(cid_tx_stateP, adsi_msg_buf, len, CLASS_MDMF_CALLERID, NULL, 0);
            	len = adsi_add_field(cid_tx_stateP, adsi_msg_buf, len, MCLASS_DATETIME, (uint8_t *) time_buf, 8);
            	len = adsi_add_field(cid_tx_stateP, adsi_msg_buf, len, MCLASS_ABSENCE1, (uint8_t *) "O", 1);
			}
	}
	
	if (len != -1) {
		// Load the message
		len = adsi_tx_put_message(cid_tx_stateP, adsi_msg_buf, len);
		
		return true;
	} else {
		// No message to send
		return false;
	}
}


static int _potsLocaleToCIDstandard()
{
	switch (country_code_infoP->cid.cid_spec & INT_CID_TYPE_MASK) {
		case INT_CID_TYPE_BELLCORE_FSK:
			return ADSI_STANDARD_CLASS;
			break;
			
		case INT_CID_TYPE_ETSI_FSK:
		case INT_CID_TYPE_SIN227:
			return ADSI_STANDARD_CLIP;
			break;
		
		case INT_CID_TYPE_DTMF1:
		case INT_CID_TYPE_DTMF2:
		case INT_CID_TYPE_DTMF3:
		case INT_CID_TYPE_DTMF4:
			return ADSI_STANDARD_CLIP_DTMF;
			break;
		
		case INT_CID_TYPE_JCLIP:
			return ADSI_STANDARD_JCLIP;
			break;
		
		case INT_CID_TYPE_ACLIP:
			return ADSI_STANDARD_JCLIP;
			break;
		
		default:
			return ADSI_STANDARD_NONE;
	}
}


// Assumes that we'll only have one source (DTMF or rotary) dialing at a time
// Returns true when a digit was detected dialed
static bool _potsEvalDialer(bool hookChange)
{
#ifdef POTS_DIAL_DEBUG
	static pots_dial_stateT prev_pots_dial_state = DIAL_IDLE;
#endif
	bool digit_dialed_detected = false;
	
	switch (pots_dial_state) {
		case DIAL_IDLE:
			if (pots_state != ON_HOOK) {
				if (hookChange && !pots_cur_off_hook) {
					pots_dial_state = DIAL_BREAK;
					pots_dial_pulse_count = 0;
					pots_dial_period_count = 0;  // Start timer to detect rotary dial break
				} else if (pots_dial_last_dtmf_digit != ' ') {
					digit_dialed_detected = true;
					pots_dial_cur_digit = pots_dial_last_dtmf_digit;
					pots_dial_last_dtmf_digit = ' ';
				}
			}
			break;
		  
		case DIAL_BREAK:
			// Increment timer
			++pots_dial_period_count;
			
			if (pots_dial_period_count > (POTS_ROT_BREAK_MSEC / POTS_EVAL_MSEC)) {
				// Too long for a rotary dialer so this must be the switch hook going back on-hook
				pots_dial_state = DIAL_IDLE;
			} else if (hookChange && pots_cur_off_hook) {
				// Valid rotary pulse
				if (pots_dial_pulse_count < 10) ++pots_dial_pulse_count;
				pots_dial_state = DIAL_MAKE;
				pots_dial_period_count = 0;  // Start timer to detect rotary dial make action (either end of digit or inner-pulse)
			}
			break;
		  
		case DIAL_MAKE:
			// Increment timer
			++pots_dial_period_count;
			
			if (pots_dial_period_count > (POTS_ROT_MAKE_MSEC / POTS_EVAL_MSEC)) {
				// End of one rotary dial - note we have a digit
				digit_dialed_detected = true;
				
				// Convert the pulse count (1-10) to an internationalized digit value
				if (pots_dial_pulse_count > 10) pots_dial_pulse_count = 10;  // Should never occur
				pots_dial_cur_digit = '0' + country_code_infoP->rotary_map[pots_dial_pulse_count-1];
				
				pots_dial_state = DIAL_IDLE;
			} else if (hookChange && !pots_cur_off_hook) {
				// Start of next rotary pulse in this dial
				pots_dial_state = DIAL_BREAK;
				pots_dial_period_count = 0;  // Start timer to detect rotary dial break
			}
			break;
	}
	
#ifdef POTS_DIAL_DEBUG
	STATE_CHANGE_PRINT(prev_pots_dial_state, pots_dial_state, pots_dial_state_name);
	prev_pots_dial_state = pots_dial_state;
#endif
	
	return digit_dialed_detected;
}

static void _potsSetToneState(pots_tone_stateT ns)
{
	_potsSetAudioOutput(ns);
	pots_tone_state = ns;
}


static void _potsEvalToneState(bool potsDigitDialed, bool appDigitDialed)
{
#ifdef POTS_STATE_DEBUG
	static pots_tone_stateT prev_pots_tone_state = TONE_IDLE;
#endif

	switch (pots_tone_state) {
		case TONE_IDLE:  // No audio
			if (pots_state == OFF_HOOK) {
				if (pots_has_call_audio) {
					// Answering call
					_potsSetToneState(TONE_VOICE);
				} else if (pots_in_service) {
					// Dialing: Give the user some dial tone
					_potsSetToneState(TONE_DIAL);
				} else {
					// Dialing: Give the re-order tone (no service)
					_potsSetToneState(TONE_NO_SERVICE);
				} 
			}
			break;
		
		case TONE_VOICE: // Enable audio connection to remote source
			if (!pots_has_call_audio && (pots_state == OFF_HOOK)) {
				// Audio stopped but user hasn't hung up
				_potsSetToneState(TONE_VOICE_WAIT_HANGUP);
			} else if (pots_state == ON_HOOK) {
				// User hung up so stop audio
				_potsSetToneState(TONE_IDLE);
			}
			break;
		
		case TONE_VOICE_WAIT_HANGUP: // Wait for user to hang up after a call
			if (pots_state == ON_HOOK) {
				_potsSetToneState(TONE_IDLE);
			} else if (pots_has_call_audio) {
				// Audio had stopped while phone was off-hook (e.g. user changed audio path on cellphone) but now
				// it's back
				_potsSetToneState(TONE_VOICE);
			} else {
				// Remote side ended call but phone is off-hook so check if it's time to give the receiver off hook tone
				if (_potsToneTimerExpired()) {
					_potsSetToneState(TONE_OFF_HOOK);
				}
			}
			break;
		
		case TONE_DIAL: // Generating dial tone
			if (!pots_in_service) {
				_potsSetToneState(TONE_NO_SERVICE);
			} else if ((pots_state == ON_HOOK) || potsDigitDialed) {
				// End dial tone because either they are hanging up or dialing a number
				_potsSetToneState(TONE_DIAL_QUIET);
			} else if ((pots_state == OFF_HOOK) && appDigitDialed) {
				// Phone is off hook and app (not phone) dialed a digit so we generate a DTMF tone for it
				_potsSetToneState(TONE_DTMF);
			} else if (pots_has_call_audio) {
				// Not sure this is entirely correct but we'll switch over to voice if they
				// get a call while preparing to dial
				_potsSetToneState(TONE_VOICE);
			} else if (_potsToneTimerExpired()) {
				_potsSetToneState(TONE_OFF_HOOK);
			} else {
				// Generate [endless] dial tone
				(void) _potsEvalToneGen();
			}
			break;
		
		case TONE_DIAL_QUIET: // No tone but listening for DTMF
			if (pots_has_call_audio) {
				_potsSetToneState(TONE_VOICE);
			} else if (pots_state == ON_HOOK) {
				_potsSetToneState(TONE_IDLE);
			} else if ((pots_state == OFF_HOOK) && appDigitDialed) {
				// Generate a DTMF tone for app generated digit
				_potsSetToneState(TONE_DTMF);
			} else if (_potsToneTimerExpired()) {
				_potsSetToneState(TONE_OFF_HOOK);
			}
			break;
		
		case TONE_DTMF:  // Generate a DTMF tone as feedback to the phone's user for a digit
		                 // dialed elsewhere than on the phone
			if (pots_has_call_audio) {
				_potsSetToneState(TONE_VOICE);
			} else if (pots_state == ON_HOOK) {
				_potsSetToneState(TONE_IDLE);
			} else if (_potsToneTimerExpired()) {
				_potsSetToneState(TONE_OFF_HOOK);
			} else {
				if (!_potsEvalToneGen()) {
					// Done with DTMF tone generation
					_potsSetToneState(TONE_DTMF_FLUSH);
				}
			}
			break;
		
		case TONE_DTMF_FLUSH:  // Let the RX audio buffer flush of the DTMF tone we just
		                       // sent that will be echoed back to us by the hybrid (and
		                       // we don't want our own DTMF receiver to process it)
		    if ((pots_state == OFF_HOOK) && appDigitDialed) {
				// Generate a DTMF tone for app generated digit
				_potsSetToneState(TONE_DTMF);
			} else if (_potsToneTimerExpired()) {
				_potsSetToneState(TONE_DIAL_QUIET);
			}
			break;
		
		case TONE_NO_SERVICE:
			if (pots_in_service) {
				// Suddenly have service
				_potsSetToneState(TONE_DIAL);
			} else if (pots_state == ON_HOOK) {
				// End beep because phone has been hung up
				_potsSetToneState(TONE_IDLE);
			} else {
				// Generate [endless] no service tone
				(void) _potsEvalToneGen();
			}
			break;
		
		case TONE_OFF_HOOK:
			if (pots_state == ON_HOOK) {
				// End tone because phone has been hung up
				_potsSetToneState(TONE_IDLE);
			} else {
				// Generate [endless] off hook tone
				(void) _potsEvalToneGen();
			}
			break;
		
		case TONE_CID:
			if (pots_state == ON_HOOK) {
				if (!_potsEvalToneGen()) {
					// Done with Caller ID audio transmission
					_potsSetToneState(TONE_CID_FLUSH);
				}
			} else {
				// End CID if phone is picked up
				_potsSetToneState(TONE_CID_FLUSH);
			}
			break;
		
		case TONE_CID_FLUSH: // Let the RX audio buffer flush of the CID message we just sent
			if (_potsToneTimerExpired()) {
				_potsSetToneState(TONE_IDLE);
			}
			break;
	}
	
#ifdef POTS_STATE_DEBUG
	STATE_CHANGE_PRINT(prev_pots_tone_state, pots_tone_state, pots_tone_state_name);
	prev_pots_tone_state = pots_tone_state;
#endif
}


// Returns false if no tone audio was loaded
static bool _potsEvalToneGen()
{
	bool valid_data = true;               // Always return true for continuously generated tones
	int cur_samples_in_tx;
	int samples_in_buf = 0;
	
	// Samples already in the audio stream buffer
	cur_samples_in_tx = audioGetTxCount();
	
	while ((cur_samples_in_tx <= POTS_TONE_BUF_LEN) && valid_data) {
		// Get some audio
		if (pots_tone_state == TONE_CID) {
			// See if there is CID audio (CID audio is only generated for a period of time)
			samples_in_buf = adsi_tx(cid_tx_stateP, tone_tx_buf, POTS_TONE_BUF_LEN);
			if (samples_in_buf == 0) {
				// No more CID audio to generate
				valid_data = false;
			}
		} else if (pots_tone_state == TONE_DTMF) {
			// See if there is DTMF audio (a DTMF tone is only generated for a period of time)
			samples_in_buf = dtmf_tx(&dtmf_tx_state, tone_tx_buf, POTS_TONE_BUF_LEN);
			if (samples_in_buf == 0) {
				// No more DTMF to generate
				valid_data = false;
			}
		} else {
			// Status tones never end (they are stopped when this routine isn't called)
			if (tone_tx_use_sample) {
				while (samples_in_buf < POTS_TONE_BUF_LEN) {
					tone_tx_buf[samples_in_buf++] = sample_tone_tx_cur_buf[sample_tone_tx_index++];
					if (sample_tone_tx_index >= sample_tone_tx_cur_len) {
						sample_tone_tx_index = 0;
					}
				}
			} else {
				samples_in_buf = super_tone_tx(&tone_state, tone_tx_buf, POTS_TONE_BUF_LEN);
			}
		}
		
		// send it to audio_task
		if (samples_in_buf != 0) {
			audioPutToneTx(tone_tx_buf, samples_in_buf);
			cur_samples_in_tx += samples_in_buf;
		}
	}
		
	return valid_data;
}


static bool _potsToneTimerExpired()
{
	if (pots_tone_timer_count != 0) {
		if (--pots_tone_timer_count == 0) {
			return true;
		} else {
			return false;
		}
	} else {
		return false;
	}
}


static void _potsSendDialedDigit(char d)
{
	app_set_pots_digit(d);
	xTaskNotify(task_handle_app, APP_NOTIFY_POTS_DIGIT_DIALED_MASK, eSetBits);
}


static void _potsSetAudioOutput(pots_tone_stateT s)
{
	switch (s) {
		case TONE_IDLE:
			// Notify audio_task to disable audio
			xTaskNotify(task_handle_audio, AUDIO_NOTIFY_DISABLE_MASK, eSetBits);
			
			// Notify app_task to restore audio levels if necessary
			if (pots_tone_state == TONE_OFF_HOOK) {
				xTaskNotify(task_handle_app, APP_NOTIFY_POTS_NORM_SPK_GAIN_MASK, eSetBits);
			}
			break;
		
		case TONE_VOICE:
			// Notify audio_task to start processing voice
			if (pots_call_audio_16k) {
				xTaskNotify(task_handle_audio, AUDIO_NOTIFY_EN_VOICE_16_MASK, eSetBits);
			} else {
				xTaskNotify(task_handle_audio, AUDIO_NOTIFY_EN_VOICE_8_MASK, eSetBits);
			}
			break;
		
		case TONE_VOICE_WAIT_HANGUP:
			// Notify audio_task to disable audio
			xTaskNotify(task_handle_audio, AUDIO_NOTIFY_DISABLE_MASK, eSetBits);
			
			// (Re)set off-hook too long detection timeout
			pots_tone_timer_count = country_code_infoP->off_hook_timeout / POTS_EVAL_MSEC;;
			break;
		
		case TONE_DIAL:
			_potsSetupAudioTone(INT_TONE_SET_DIAL_INDEX);
			
			// Notify audio_task to start processing tone
			xTaskNotify(task_handle_audio, AUDIO_NOTIFY_EN_TONE_MASK, eSetBits);
			
			// Setup DTMF receiver in preparation to hear dialed digits
			(void) dtmf_rx_init(&dtmf_rx_state, _potsDtmfCallback, (void *) 0);
			pots_dial_last_dtmf_digit = ' ';
			
			// Setup DTMF transmitter in preparation to simulate dialed digit sounds that our
			// controlling app tells us it is dialing on behalf of the phone
			(void) dtmf_tx_init(&dtmf_tx_state);
			
			// (Re)set off-hook too long detection timeout
			pots_tone_timer_count = country_code_infoP->off_hook_timeout / POTS_EVAL_MSEC;;
			break;
		
		case TONE_DIAL_QUIET:
			// (Re)set off-hook too long detection timeout
			pots_tone_timer_count = country_code_infoP->off_hook_timeout / POTS_EVAL_MSEC;;
			break;
		
		case TONE_DTMF:
			// Add the digit provided by the app to our list of DTMF tones to generate
			dtmf_tx_put(&dtmf_tx_state, dtmf_tx_digit_buf, -1);
			
			// Notify audio_task to start processing tone
			xTaskNotify(task_handle_audio, AUDIO_NOTIFY_EN_TONE_MASK, eSetBits);
			
			// (Re)set off-hook too long detection timeout
			pots_tone_timer_count = country_code_infoP->off_hook_timeout / POTS_EVAL_MSEC;
			break;
		
		case TONE_DTMF_FLUSH:
			// Setup timer for flush period following a DTMF tone generation
			pots_tone_timer_count = POTS_TONE_FLUSH_MSEC / POTS_EVAL_MSEC;
			break;
		
		case TONE_NO_SERVICE:
			_potsSetupAudioTone(INT_TONE_SET_RO_INDEX);
			
			// Notify audio_task to start processing tone
			xTaskNotify(task_handle_audio, AUDIO_NOTIFY_EN_TONE_MASK, eSetBits);
			break;
		
		case TONE_OFF_HOOK:
			_potsSetupAudioTone(INT_TONE_SET_OH_INDEX);
			
			// Notify app_task to set maximum gain for signaling (in case user has set a very low speaker volume)
			xTaskNotify(task_handle_app, APP_NOTIFY_POTS_MAX_SPK_GAIN_MASK, eSetBits);
			
			
			// Notify audio_task to start processing tone
			xTaskNotify(task_handle_audio, AUDIO_NOTIFY_EN_TONE_MASK, eSetBits);
			break;
		
		case TONE_CID:
			// Notify app_task to set maximum gain for signaling (in case user has set a very low speaker volume)
			xTaskNotify(task_handle_app, APP_NOTIFY_POTS_MAX_SPK_GAIN_MASK, eSetBits);
			
			// Notify audio_task to start processing message
			xTaskNotify(task_handle_audio, AUDIO_NOTIFY_EN_TONE_MASK, eSetBits);
			break;
		
		case TONE_CID_FLUSH:
			// Notify app_task to restore audio levels
			xTaskNotify(task_handle_app, APP_NOTIFY_POTS_NORM_SPK_GAIN_MASK, eSetBits);
				
			// Setup timer for flush period following a CID audio generation
			pots_tone_timer_count = POTS_CID_FLUSH_MSEC / POTS_EVAL_MSEC;
			break;
	}
}


static void _potsSetupAudioTone(int tone_index)
{
	if (tone_index >= INT_NUM_TONE_SETS) return;
	
	if (sample_tone_tx_length[tone_index] != 0) {
		sample_tone_tx_cur_len = sample_tone_tx_length[tone_index];
		sample_tone_tx_cur_buf = sample_tone_tx_bufP[tone_index];
		sample_tone_tx_index = 0;
		tone_tx_use_sample = true;
	} else {
		(void) super_tone_tx_init(&tone_state, tone_step[tone_index][0]);
		tone_tx_use_sample = false;
	}
}


static void _potsEvalDtmfDetect()
{
	int cur_samples_in_rx;
	int samples_to_analyze;
	
	if ((pots_tone_state == TONE_IDLE) || (pots_tone_state == TONE_VOICE) || (pots_tone_state == TONE_VOICE_WAIT_HANGUP)) {
		// Don't do anything if audio_task isn't handling audio for us
		return;
	}
	
	// Get audio
	cur_samples_in_rx = audioGetRxCount();
	while (cur_samples_in_rx > 0) {
		samples_to_analyze = (cur_samples_in_rx >= POTS_DTMF_BUF_LEN) ? POTS_DTMF_BUF_LEN : cur_samples_in_rx;
		audioGetToneRx(dtmf_rx_buf, samples_to_analyze);
		if ((pots_tone_state == TONE_DIAL) || (pots_tone_state == TONE_DIAL_QUIET)) {
			// Only look for DTMF tones when we expect user-generated tones
			(void) dtmf_rx(&dtmf_rx_state, dtmf_rx_buf, samples_to_analyze);
		}
		cur_samples_in_rx -= samples_to_analyze;
	}
}


static void _potsDtmfCallback(void *data, const char *digits, int len)
{
	// Will be set if necessary
	pots_dial_last_dtmf_digit = ' ';

	if (len == 0) {
		return;
	}
	
	if (len > 1) {
		ESP_LOGE(TAG, "Saw too many DTMF keys - %d", len);
	}
	pots_dial_last_dtmf_digit = digits[0];
}

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
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "app_task.h"
#include "audio_task.h"
#include "pots_task.h"
#include "dtmf.h"
#include "international.h"
#include "ps.h"
#include "super_tone_tx.h"
#include "sys_common.h"

//
// Local constants
//

// Uncomment for state debug
#define POTS_STATE_DEBUG
//#define POTS_DIAL_DEBUG
//#define POTS_RING_DEBUG

// State machine evaluation interval
#define POTS_EVAL_MSEC           10

// Make period required to move from off-hook back to on-hook (differentiate from rotary dialing pulses)
#define POTS_ON_HOOK_DETECT_MSEC 500

// Rotary dialing pulse detection
//   Break is maximum period to detect a pulse in a digit
//   Make is minimum period between digits
#define POTS_ROT_BREAK_MSEC      100
#define POTS_ROT_MAKE_MSEC       100

// Post send DTMF tone wait period to allow audio buffers to drain of echoed back DTMF digit
#define POTS_DTMF_FLUSH_MSEC     30

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
static bool pots_in_service;             // Set if the phone can make a call, clear if it isn't in service
static bool pots_has_call_audio;         // Set when the external process has a connected phone call (used to suppress off-hook tone, enable audio)
static bool pots_call_audio_16k;         // Set when connected phone call is using 16k samples/sec (false for 8k samples/sec)

// Hook logic
typedef enum {ON_HOOK, OFF_HOOK, ON_HOOK_PROVISIONAL} pots_stateT;
#ifdef POTS_STATE_DEBUG
static const char* pots_state_name[] = {"ON_HOOK", "OFF_HOOK", "ON_HOOK_PROVISIONAL"};
#endif
static pots_stateT pots_state;
static int pots_state_count;             // Down counter for phone state change detection
static bool pots_cur_off_hook;           // Debounced off-hook state
static bool pots_saw_hook_state_change;  // For API notification

// Ring logic
static bool pots_do_not_disturb;         // Inhibits ringing
static bool pots_ring_request;           // Ringing requested
typedef enum {RING_IDLE, RING_PULSE_ON, RING_PULSE_OFF, RING_STEP_WAIT} pots_ring_stateT;
#ifdef POTS_RING_DEBUG
static const char* pots_ring_state_name[] = {"RING_IDLE", "RING_PULSE_ON", "RING_PULSE_OFF", "RING_STEP_WAIT"};
#endif
static pots_ring_stateT pots_ring_state;
static int pots_num_ring_steps;          // Number of steps in a ring (at least 2 for a single ON/OFF)
static int pots_ring_step;               // The current cadence step
static int pots_ring_period_count;       // Counts down evaluation cycles for each ringing state
static int pots_ring_pulse_count;        // Counts down pulses in one ring ON or OFF portion


// Dialing logic
typedef enum {DIAL_IDLE, DIAL_BREAK, DIAL_MAKE} pots_dial_stateT;
#ifdef POTS_DIAL_DEBUG
static const char* pots_dial_state_name[] = {"DIAL_IDLE", "DIAL_BREAK", "DIAL_MAKE"};
#endif
static pots_dial_stateT pots_dial_state;
static int pots_dial_period_count;       // Counts up evaluation cycles for each dialing state
static int pots_dial_pulse_count;        // Counts pulses from the rotary dial for one digit
static char pots_dial_cur_digit;         // 0 - 9, A - D, *, #
static char pots_dial_last_dtmf_digit;

// Tone generation logic
typedef enum {TONE_IDLE, TONE_VOICE, TONE_VOICE_WAIT_HANGUP, TONE_DIAL, TONE_DIAL_QUIET,
              TONE_DTMF, TONE_DTMF_FLUSH, TONE_NO_SERVICE, TONE_OFF_HOOK
             } pots_tone_stateT;
#ifdef POTS_STATE_DEBUG
static const char* pots_tone_state_name[] = {"TONE_IDLE", "TONE_VOICE", "TONE_VOICE_WAIT_HANGUP",
                                             "TONE_DIAL", "TONE_DIAL_QUIET", "TONE_DTMF", 
                                             "TOND_DIAL_FLUSH", "TONE_NO_SERVICE", "TONE_OFF_HOOK"
                                            };
#endif
static pots_tone_stateT pots_tone_state;
static int pots_tone_timer_count;
static bool pots_notify_ext_digit_dialed; // Set by notification when another task dials a digit
                                          // (used to suppress dial tone and generate DTMF here)

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
static void _potsHandleNotifications();
static bool _potsEvalHook();
static void _potsEvalPhoneState(bool hookChange);
static void _potsEvalRinger();
static void _potsStartRing();
static void _potsEndRing();
static int _potsGetRingPulseCount(bool on_portion);
static bool _potsEvalDialer(bool hookChange);
static void _potsSetToneState(pots_tone_stateT ns);
static void _potsEvalToneState(bool potsDigitDialed, bool appDigitDialed);
static bool _potsEvalToneGen();
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
  	
	// Init state
	pots_state = ON_HOOK;
	pots_ring_state = RING_IDLE;
	pots_dial_state = DIAL_IDLE;
	pots_tone_state = TONE_IDLE;
	pots_in_service = false;
	pots_has_call_audio = false;
	pots_do_not_disturb = false;
	pots_ring_request = false;
	pots_cur_off_hook = false;
	pots_saw_hook_state_change = false;
	pots_tone_timer_count = POTS_RCV_OFF_HOOK_MSEC / POTS_EVAL_MSEC;
	pots_dial_last_dtmf_digit = ' ';
	pots_notify_ext_digit_dialed = false;
	
	// Country code configures what tones and patterns we generate
	country_code = ps_get_country_code();
	country_code_infoP = int_get_country_info(country_code);
	ESP_LOGI(TAG, "Country: %s", country_code_infoP->name);
		
	// configure GPIO
	_potsInitGPIO();
	
	// Initialize our outgoing tone set (pre-allocate tone step info so we're
	// not always needing to free and potentially fragment heap)
	_potsInitTones(true);
		
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
				pots_ring_request = true;
			}
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

	// End ringing if phone just went off hook
	if ((pots_state == OFF_HOOK) && (pots_ring_state != RING_IDLE)) {
		_potsEndRing();
	}
	
	switch (pots_ring_state) {
		case RING_IDLE:
			if ((pots_state == ON_HOOK) && pots_ring_request) {
				_potsStartRing();
			}
			break;
		
		case RING_PULSE_ON:
			// Decrement ring-on timer
			pots_ring_period_count--;
			if (--pots_ring_pulse_count <= 0) {
				// Ring pulse done
				pots_ring_state = RING_PULSE_OFF;
				pots_ring_pulse_count = _potsGetRingPulseCount(false);  // Off for half a pulse
				gpio_set_level(PIN_FR, 1);
			}
			break;
		
		case RING_PULSE_OFF:
			// Decrement ring-off timer and pulse timer
			pots_ring_period_count--;
			pots_ring_pulse_count--;
			// Either timer expiring ends this half of the pulse
			if ((pots_ring_period_count <= 0) || (pots_ring_pulse_count <= 0)) {
				if (pots_ring_period_count <= 0) {
					// End of ring - check if there are more rings
					if (++pots_ring_step == (pots_num_ring_steps-1)) {
						_potsEndRing();
					} else {
						pots_ring_state = RING_STEP_WAIT;
						pots_ring_period_count = country_code_infoP->ring_info.cadence_pairs[pots_ring_step] / POTS_EVAL_MSEC;
					}
				} else {
					// Setup next ring pulse in this ring
					pots_ring_state = RING_PULSE_ON;
					pots_ring_pulse_count = _potsGetRingPulseCount(true);
					gpio_set_level(PIN_FR, 0);
				}
			}
			break;
		
		case RING_STEP_WAIT:
			if (--pots_ring_period_count <= 0) {
				// End of wait - start next ring
				++pots_ring_step;
				pots_ring_state = RING_PULSE_ON;
				pots_ring_period_count = country_code_infoP->ring_info.cadence_pairs[pots_ring_step] / POTS_EVAL_MSEC;
				pots_ring_pulse_count = _potsGetRingPulseCount(true);
			}
			break;
	}
	
	// Always clear notification flag after consumption
	pots_ring_request = false;
	
#ifdef POTS_RING_DEBUG
	STATE_CHANGE_PRINT(prev_pots_ring_state, pots_ring_state, pots_ring_state_name);
	prev_pots_ring_state = pots_ring_state;
#endif
}


static void _potsStartRing()
{
	pots_num_ring_steps = country_code_infoP->ring_info.num_cadence_pairs * 2;
	pots_ring_step = 0;
	pots_ring_state = RING_PULSE_ON;
	pots_ring_period_count = country_code_infoP->ring_info.cadence_pairs[pots_ring_step] / POTS_EVAL_MSEC;
	pots_ring_pulse_count = _potsGetRingPulseCount(true);
	gpio_set_level(PIN_RM, 1);   // Cause the line to enter ring mode
	gpio_set_level(PIN_FR, 0);   // Toggle the line (reverse) to start this pulse of the ring
}


static void _potsEndRing()
{
	pots_ring_state = RING_IDLE;
	gpio_set_level(PIN_FR, 1);   // Make sure tip/ring are not reversed
	gpio_set_level(PIN_RM, 0);   // Exit ring mode
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
	pots_tone_state = ns;
	_potsSetAudioOutput(ns);
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
				if (--pots_tone_timer_count <= 0) {
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
			} else if (--pots_tone_timer_count <= 0) {
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
			} else if (--pots_tone_timer_count <= 0) {
				_potsSetToneState(TONE_OFF_HOOK);
			}
			break;
		
		case TONE_DTMF:  // Generate a DTMF tone as feedback to the phone's user for a digit
		                 // dialed elsewhere than on the phone
			if (pots_has_call_audio) {
				_potsSetToneState(TONE_VOICE);
			} else if (pots_state == ON_HOOK) {
				_potsSetToneState(TONE_IDLE);
			} else if (--pots_tone_timer_count <= 0) {
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
			} else if (--pots_tone_timer_count <= 0) {
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
		if (pots_tone_state == TONE_DTMF) {
			// See if there is DTMF audio (a DTMF tone is only generated for a period of time)
			samples_in_buf = dtmf_tx(&dtmf_tx_state, tone_tx_buf, POTS_TONE_BUF_LEN);
			if (samples_in_buf == 0) {
				// No more DTMF to generate
				valid_data = false;
			}
		} else {
			// Tones never end (they are stopped when this routine isn't called)
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
			pots_tone_timer_count = POTS_RCV_OFF_HOOK_MSEC / POTS_EVAL_MSEC;
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
			pots_tone_timer_count = POTS_RCV_OFF_HOOK_MSEC / POTS_EVAL_MSEC;
			break;
		
		case TONE_DIAL_QUIET:
			// (Re)set off-hook too long detection timeout
			pots_tone_timer_count = POTS_RCV_OFF_HOOK_MSEC / POTS_EVAL_MSEC;
			break;
		
		case TONE_DTMF:
			// Add the digit provided by the app to our list of DTMF tones to generate
			dtmf_tx_put(&dtmf_tx_state, dtmf_tx_digit_buf);
			
			// Notify audio_task to start processing tone
			xTaskNotify(task_handle_audio, AUDIO_NOTIFY_EN_TONE_MASK, eSetBits);
			
			// (Re)set off-hook too long detection timeout
			pots_tone_timer_count = POTS_RCV_OFF_HOOK_MSEC / POTS_EVAL_MSEC;
			break;
		
		case TONE_DTMF_FLUSH:
			// Setup timer for flush period following a DTMF tone generation
			pots_tone_timer_count = POTS_DTMF_FLUSH_MSEC / POTS_EVAL_MSEC;
			break;
		
		case TONE_NO_SERVICE:
			_potsSetupAudioTone(INT_TONE_SET_RO_INDEX);
			
			// Notify audio_task to start processing tone
			xTaskNotify(task_handle_audio, AUDIO_NOTIFY_EN_TONE_MASK, eSetBits);
			break;
		
		case TONE_OFF_HOOK:
			_potsSetupAudioTone(INT_TONE_SET_OH_INDEX);
			
			// Notify audio_task to start processing tone
			xTaskNotify(task_handle_audio, AUDIO_NOTIFY_EN_TONE_MASK, eSetBits);
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
	
	// We ignore special characters 'A' - 'D'
	if (len > 1) {
		ESP_LOGE(TAG, "Saw too many DTMF keys - %d", len);
	}
	pots_dial_last_dtmf_digit = digits[0];
}

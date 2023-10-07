/*
 * App Task
 *
 * Contains the top-level logic involved in managing phone calls, coordinating
 * between the sub-tasks.  It also manages updating gain since we don't want to
 * run [slow] I2C cycles in audio_task and we get gain levels from both gui_task
 * and bt_task.
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
#include "esp_log.h"
#include "esp_system.h"
#include "esp_hf_client_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "app_task.h"
#include "bt_task.h"
#include "gcore_task.h"
#include "gui_task.h"
#include "pots_task.h"
#include "gain.h"
#include "ps.h"
#include "sample.h"
#include "sys_common.h"
#include "gui_utilities.h"
#include <string.h>



//
// Constants
//

// Uncomment to debug state transitions
#define APP_ST_DEBUG

// Count to determine a ringing but unanswered call is finished
#define APP_LAST_RING_DETECT_COUNT    (APP_LAST_RING_DETECT_MSEC/APP_EVAL_MSEC)

// Count to initiate dialing after POTS dialed digit
#define APP_LAST_DIGIT_2_DIAL_COUNT   (APP_LAST_DIGIT_2_DIAL_MSEC/APP_EVAL_MSEC)



//
// Variables
//

static const char* TAG = "app_task";

static const char* app_state_name[] = {"DISCONNECTED", "CONNECTED_IDLE", "CALL_RECEIVED", "CALL_WAIT_ACTIVE", "DIALING",
                                       "CALL_INITIATED", "CALL_ACTIVE", "CALL_ACTIVE_VOICE", "CALL_WAIT_END", "CALL_WAIT_ONHOOK"};

// State
static app_state_t app_state = DISCONNECTED;
static bool bt_in_service = false;                  // BT has SLC (service level connection)
static bool bt_in_call = false;                     // BT sees call has been established (successfully initiated or answered)
static bool bt_audio_connected = false;             // BT sending us audio
static bool pots_off_hook = false;
static bool cid_valid = false;                      // Set true when we get Caller ID info from bluetooth
static int call_received_timer = 0;                 // Timer to detect ringing has ended for incoming/unanswered calls
static int ring_count = 0;                          // Number of rings
static float new_mic_gain;                          // New gain set by bt_task from remote device
static float new_spk_gain;

// Notification flags - set by a notification and consumed/cleared by state evaluation
static bool notify_dial_btn_pressed = false;
static bool notify_bt_ring_indication = false;

// Phone dialing
static bool last_dial_digit_from_pots;
static char new_gui_digit;
static char new_pots_digit;
static char dialing_num[APP_MAX_DIALED_DIGITS+1];   // Statically allocated phone number dialing buffer
static int dialing_num_valid = 0;                   // Number of valid entries - also points to next location to load
static int dialing_pots_digit_timer = 0;            // Counts up evaluation cycles after each POTs digit dialed to initiate a call
static SemaphoreHandle_t dialing_num_mutex;

// Caller ID
static char cid_num[ESP_BT_HF_NUMBER_LEN+1];
static SemaphoreHandle_t cid_num_mutex;

#if (CONFIG_AUDIO_SAMPLE_ENABLE == true)
// Support for audio sample recording
static bool audio_sampling_in_progress = false;
#endif



//
// App Task internal function forward declarations
//
static void _appHandleNotifications();
static void _appPushNewDialedDigit(char c);
static void _appEvalState();
static void _appSetState(app_state_t st);
static bool _appCanInitiateAssistantCall();
static void _appInvalidateDialingNum();
static void _appInvalidateCID();


//
// API
//
void app_task()
{
	int activity_counter = 0;
	
	ESP_LOGI(TAG, "Start task");
	
	// Phone number access semaphores
	dialing_num_mutex = xSemaphoreCreateMutex();
	cid_num_mutex = xSemaphoreCreateMutex();
	
#if (CONFIG_AUDIO_SAMPLE_ENABLE == true)
	sample_mem_init();
#endif
	
	while (1) {
		// Get any new notifications
		_appHandleNotifications();
		
		// Evaluate state updates
		_appEvalState();
		
		// Notify gcore_task of activity twice/sec while we're busy with a call
		if ((app_state == DISCONNECTED) || (app_state == CONNECTED_IDLE)) {
			// Hold counter in reset
			activity_counter = 0;
		} else {
			// Look for timeout for activity notification
			if (++activity_counter > (500 / APP_EVAL_MSEC)) {
				activity_counter = 0;
				xTaskNotify(task_handle_gcore, GCORE_NOTIFY_ACTIVITY_MASK, eSetBits);
			}
		}
		
#if (CONFIG_AUDIO_SAMPLE_ENABLE == true)
	if (audio_sampling_in_progress) {
		if (!sample_in_progress()) {
			audio_sampling_in_progress = false;
			
			// Save audio samples and then notify user
			sample_save();
			sample_end();
			
			gui_preset_message_box_string("Audio samples saved.  Safe to remove card.", false, GUI_MSGBOX_SMPL_DONE);
			xTaskNotify(task_handle_gui, GUI_NOTIFY_MESSAGEBOX_MASK, eSetBits);
		}
	}
#endif
		
		vTaskDelay(pdMS_TO_TICKS(APP_EVAL_MSEC));
	}
}


void app_set_gui_digit(char c)
{
	new_gui_digit = c;
}


void app_set_pots_digit(char c)
{
	new_pots_digit = c;
}


void app_set_cid_number(const char* pn)
{
	xSemaphoreTake(cid_num_mutex, portMAX_DELAY);
	strncpy(cid_num, pn, ESP_BT_HF_NUMBER_LEN);
	cid_num[ESP_BT_HF_NUMBER_LEN] = 0;
	xSemaphoreGive(cid_num_mutex);
	
	cid_valid = true;
}


// pn must have ESP_BT_HF_NUMBER_LEN + 1 characters
int app_get_cid_number(char* pn)
{
	xSemaphoreTake(cid_num_mutex, portMAX_DELAY);
	strncpy(pn, cid_num, ESP_BT_HF_NUMBER_LEN+1);
	pn[ESP_BT_HF_NUMBER_LEN] = 0;
	xSemaphoreGive(cid_num_mutex);
	return strlen(pn);
}


int app_get_dial_number(char* pn)
{
	int n;
	
	xSemaphoreTake(dialing_num_mutex, portMAX_DELAY);
	n = dialing_num_valid;
	for (int i=0; i<=n; i++) {
		// Copy characters including the final null terminator
		*(pn+i) = dialing_num[i];
	}
	xSemaphoreGive(dialing_num_mutex);
	
	return n;
}


app_state_t app_get_state()
{
	// Access should be atomic so no mutex necessary
	return app_state;
}


void app_set_new_mic_gain(float g)
{
	new_mic_gain = g;
}


void app_set_new_spk_gain(float g)
{
	new_spk_gain = g;
}


//
// App Task Internal functions
//
static void _appHandleNotifications()
{
	uint32_t notification_value = 0;
	
	// Handle notifications (clear them upon reading)
	if (xTaskNotifyWait(0x00, 0xFFFFFFFF, &notification_value, 0)) {
		//
		// Hook state
		//
		if (Notification(notification_value, APP_NOTIFY_POTS_ON_HOOK_MASK)) {
			pots_off_hook = false;
		}
		
		if (Notification(notification_value, APP_NOTIFY_POTS_OFF_HOOK_MASK)) {
			pots_off_hook = true;
		}
		
		//
		// Dialing info
		//
		if (Notification(notification_value, APP_NOTIFY_POTS_DIGIT_DIALED_MASK)) {
			_appPushNewDialedDigit(new_pots_digit);
			last_dial_digit_from_pots = true;
		}
		
		if (Notification(notification_value, APP_NOTIFY_GUI_DIGIT_DIALED_MASK)) {
			_appPushNewDialedDigit(new_gui_digit);
			last_dial_digit_from_pots = false;
			
			// Let pots_task know the user dialed from the GUI so it can disable dial tone if necessary
			// and generate DTMF tones as user feedback if we're not in a call (if we're in a call we
			// tell the cellphone about the digit in _appPushNewDialedDigit and it should generate a
			// DTMF tone in the audio stream for the user)
			if (app_state == DIALING) {
				pots_set_app_dialed_digit(new_gui_digit);
				xTaskNotify(task_handle_pots, POTS_NOTIFY_EXT_DIAL_DIGIT_MASK, eSetBits);
			}
		}
		
		if (Notification(notification_value, APP_NOTIFY_GUI_DIGIT_DELETED_MASK)) {
			if (app_state == DIALING) {
				if (dialing_num_valid > 0) {
					xSemaphoreTake(dialing_num_mutex, portMAX_DELAY);
					dialing_num_valid -= 1;
					dialing_num[dialing_num_valid] = 0;  // Replace deleted character with null terminator
					xSemaphoreGive(dialing_num_mutex);
					
					last_dial_digit_from_pots = false;   // Always assume if user deleted from GUI, they're entering too
					xTaskNotify(task_handle_gui, GUI_NOTIFY_PH_NUM_UPDATE_MASK, eSetBits);
				}
			}
		}
		
		if (Notification(notification_value, APP_NOTIFY_GUI_DIAL_BTN_PRESSED_MASK)) {
			notify_dial_btn_pressed = true;
		}
		
		//
		// Bluetooth info
		//
		if (Notification(notification_value, APP_NOTIFY_BT_IN_SERVICE_MASK)) {
			bt_in_service = true;
		}
		
		if (Notification(notification_value, APP_NOTIFY_BT_OUT_OF_SERVICE_MASK)) {
			bt_in_service = false;
		}
		
		if (Notification(notification_value, APP_NOTIFY_BT_RING_MASK)) {
			notify_bt_ring_indication = true;
			ring_count += 1;
		}
		
		if (Notification(notification_value, APP_NOTIFY_BT_CALL_STARTED_MASK)) {
			bt_in_call = true;
		}
		
		if (Notification(notification_value, APP_NOTIFY_BT_CALL_ENDED_MASK)) {
			bt_in_call = false;
		}
		
		if (Notification(notification_value, APP_NOTIFY_BT_CID_AVAILABLE_MASK)) {
			xTaskNotify(task_handle_gui, GUI_NOTIFY_CID_NUM_UPDATE_MASK, eSetBits);
		}
		
		if (Notification(notification_value, APP_NOTIFY_BT_AUDIO_START_MASK)) {
			bt_audio_connected = true;
		}
		
		if (Notification(notification_value, APP_NOTIFY_BT_AUDIO_ENDED_MASK)) {
			bt_audio_connected = false;
		}
		
		//
		// Audio gain updates
		//
		if (Notification(notification_value, APP_NOTIFY_NEW_GUI_MIC_GAIN_MASK)) {
			// Get updated gain from PS
			new_mic_gain = ps_get_gain(PS_GAIN_MIC);
			
			// Update the codec value
			if (!gainSetCodec(GAIN_TYPE_MIC, new_mic_gain)) {
				ESP_LOGE(TAG, "Update codec mic gain failed");
			}
			
			// Inform BT so it can update the remote device (it will get the value from PS)
			xTaskNotify(task_handle_bt, BT_NOTIFY_NEW_MIC_GAIN_MASK, eSetBits);
		}
		
		if (Notification(notification_value, APP_NOTIFY_NEW_GUI_SPK_GAIN_MASK)) {
			// Get updated gain from PS
			new_spk_gain = ps_get_gain(PS_GAIN_SPK);
			
			// Update the codec value
			if (!gainSetCodec(GAIN_TYPE_SPK, new_spk_gain)) {
				ESP_LOGE(TAG, "Update codec speaker gain failed");
			}
			
			// Inform BT so it can update the remote device (it will get the value from PS)
			xTaskNotify(task_handle_bt, BT_NOTIFY_NEW_SPK_GAIN_MASK, eSetBits);
		}
		
		if (Notification(notification_value, APP_NOTIFY_NEW_BT_MIC_GAIN_MASK)) {
			// Update the codec value
			if (!gainSetCodec(GAIN_TYPE_MIC, new_mic_gain)) {
				ESP_LOGE(TAG, "Update codec mic gain failed");
			}
			
			// Inform the GUI so it can update the control and PS
			gui_set_new_mic_gain(new_mic_gain);
			xTaskNotify(task_handle_gui, GUI_NOTIFY_UPDATE_MIC_GAIN_MASK, eSetBits);
		}
		
		if (Notification(notification_value, APP_NOTIFY_NEW_BT_SPK_GAIN_MASK)) {
			// Update the codec value
			if (!gainSetCodec(GAIN_TYPE_SPK, new_spk_gain)) {
				ESP_LOGE(TAG, "Update codec speaker gain failed");
			}
			
			// Inform the GUI so it can update the control and PS
			gui_set_new_spk_gain(new_spk_gain);
			xTaskNotify(task_handle_gui, GUI_NOTIFY_UPDATE_SPK_GAIN_MASK, eSetBits);
		}
		
		if (Notification(notification_value, APP_NOTIFY_POTS_MAX_SPK_GAIN_MASK)) {
			// Set the maximum value
			if (!gainSetCodec(GAIN_TYPE_SPK, GAIN_APP_SPK_MAX_DB)) {
				ESP_LOGE(TAG, "Set max codec speaker gain failed");
			}
		}
		
		if (Notification(notification_value, APP_NOTIFY_POTS_NORM_SPK_GAIN_MASK)) {
			// Get operating gain from PS
			new_spk_gain = ps_get_gain(PS_GAIN_SPK);
			
			// Update the codec value
			if (!gainSetCodec(GAIN_TYPE_SPK, new_spk_gain)) {
				ESP_LOGE(TAG, "Restore codec speaker gain failed");
			}
		}
		
#if (CONFIG_AUDIO_SAMPLE_ENABLE == true)
		if (Notification(notification_value, APP_NOTIFY_START_AUDIO_SMPL_MASK)) {
			if (sample_start()) {
				audio_sampling_in_progress = true;
			} else {
				gui_preset_message_box_string("Could not mount Micro-SD Card", false, GUI_MSGBOX_SMPL_FAIL);
				xTaskNotify(task_handle_gui, GUI_NOTIFY_MESSAGEBOX_MASK, eSetBits);
			}
		}
#endif
	}
}


static void _appPushNewDialedDigit(char c)
{
	if ((app_state == DIALING) || (app_state == CALL_ACTIVE) || (app_state == CALL_ACTIVE_VOICE)) {
		if (dialing_num_valid < APP_MAX_DIALED_DIGITS) {
			// Add digit to phone number
			xSemaphoreTake(dialing_num_mutex, portMAX_DELAY);
			dialing_num[dialing_num_valid] = c;
			dialing_num_valid += 1;
			dialing_num[dialing_num_valid] = 0; // Make sure string is terminated
			xSemaphoreGive(dialing_num_mutex);
			
			// Update GUI
			xTaskNotify(task_handle_gui, GUI_NOTIFY_PH_NUM_UPDATE_MASK, eSetBits);
			
			// Reset dial timer
			dialing_pots_digit_timer = 0;
		}
	}
	
	if ((app_state == CALL_ACTIVE) || (app_state == CALL_ACTIVE_VOICE)) {
		// Tell bluetooth to send digits entered during a call as DTMF tones
		bt_set_dtmf_digit(c);
		xTaskNotify(task_handle_bt, BT_NOTIFY_DIAL_DTMF_MASK, eSetBits);
	}
}


static void _appEvalState()
{
		switch (app_state) {
		case DISCONNECTED:  // No bluetooth connection
			if (bt_in_service) {
				_appSetState(CONNECTED_IDLE);
			}
			break;
		
		case CONNECTED_IDLE: // Have bluetooth SLC
			if (!bt_in_service) {
				_appSetState(DISCONNECTED);
			} else if (notify_bt_ring_indication) {
				_appSetState(CALL_RECEIVED);
			} else if (pots_off_hook) {
				if (bt_audio_connected) {
					// Picking up when cellphone has routed audio to us takes priority over dialing
					_appSetState(CALL_ACTIVE_VOICE);
				} else {
					_appSetState(DIALING);
				}
			}
			break;
		
		case CALL_RECEIVED:  // Bluetooth received call
			if (!bt_in_service) {
				_appSetState(DISCONNECTED);
			} else if (notify_dial_btn_pressed) {
				// User is rejecting the call from the GUI so tell cellphone to end call
				_appSetState(CALL_WAIT_END);
			} else if (bt_in_call) {
				// bluetooth connected call (probably shouldn't happen but we handle it)
				if (bt_audio_connected && pots_off_hook) {
					_appSetState(CALL_ACTIVE_VOICE);
				} else {
					_appSetState(CALL_ACTIVE);
				}
			} else if (pots_off_hook) {
				// User picked up so tell cellphone to accept call
				_appSetState(CALL_WAIT_ACTIVE);
			} else if (notify_bt_ring_indication) {
				// Reset ring timeout counter every time we get a ring
				_appSetState(CALL_RECEIVED);
			} else if (++call_received_timer >= APP_LAST_RING_DETECT_COUNT) {
				// call ended with no action; we detect this when we haven't received
				// any rings in a while
				_appSetState(CONNECTED_IDLE);
				
				// Special notification to pots_task that all rings associated with a call are done
				xTaskNotify(task_handle_pots, POTS_NOTIFY_DONE_RINGING_MASK, eSetBits);
			}
			
			// Handle the special case where by the 2nd ring we didn't get any Caller ID
			// info probably indicating the number was blocked.  Let the GUI know here since
			// it won't have been updated by received CID info so it will see an empty
			// caller ID string and display something appropriate.
			if (!cid_valid && (ring_count == 2)) {
				xTaskNotify(task_handle_gui, GUI_NOTIFY_CID_NUM_UPDATE_MASK, eSetBits);
			}
			break;
			
		case CALL_WAIT_ACTIVE:  // Waiting for bluetooth to tell us the call is connected
			if (!bt_in_service) {
				_appSetState(DISCONNECTED);
			} else if (!pots_off_hook || notify_dial_btn_pressed) {
				// User ended call so tell cellphone to end call
				_appSetState(CALL_WAIT_END);
			} else if (bt_in_call) {
				if (bt_audio_connected && pots_off_hook) {
					_appSetState(CALL_ACTIVE_VOICE);
				} else {
					_appSetState(CALL_ACTIVE);
				}
			}
			break;
		
		case DIALING:  // User went off-hook to dial
			if (!bt_in_service) {
				_appSetState(DISCONNECTED);
			} else if (!pots_off_hook) {
				_appSetState(CONNECTED_IDLE);
			} else if (notify_bt_ring_indication) {
				// Someone calling us while we're dialing takes precedence
				_appSetState(CALL_RECEIVED);
			} else if (bt_audio_connected) {
				// Cellphone routed audio to us
				_appSetState(CALL_ACTIVE_VOICE);
			} else if (dialing_num_valid > 0) {
				// Increment the dial timer
				dialing_pots_digit_timer += 1;
				
				// Look to see if can tell bluetooth to dial a number
				if ((notify_dial_btn_pressed) || 
				    (last_dial_digit_from_pots && (dialing_pots_digit_timer >= APP_LAST_DIGIT_2_DIAL_COUNT))) {
					
					_appSetState(CALL_INITIATED);
				}
			}
			break;
		
		case CALL_INITIATED: // Requested bluetooth initiate a phone call
			if (!bt_in_service) {
				_appSetState(DISCONNECTED);
			} else if (bt_in_call) {
				if (bt_audio_connected && pots_off_hook) {
					_appSetState(CALL_ACTIVE_VOICE);
				} else {
					_appSetState(CALL_ACTIVE);
				}
			} else if (notify_dial_btn_pressed || !pots_off_hook) {
				// Either the user ended the call from the GUI or our phone hung up the call
				// so tell cellphone to end call
				_appSetState(CALL_WAIT_END);
			}
			break;
		
		case CALL_ACTIVE:  // Call in progress, bluetooth audio is not routed to us
			if (!bt_in_service) {
				_appSetState(DISCONNECTED);
			} else if (notify_dial_btn_pressed) {
				// User ended the call from the GUI so we tell cellphone to end call
				_appSetState(CALL_WAIT_END);
			} if (!bt_in_call) {
				// Cellphone ended the call
				if (pots_off_hook) {
					// Wait for phone to hang up
					_appSetState(CALL_WAIT_ONHOOK);
				} else {
					_appSetState(CONNECTED_IDLE);
				}
			} else if (bt_audio_connected && pots_off_hook) {
				// We now have bluetooth audio
				_appSetState(CALL_ACTIVE_VOICE);
			}
			break;
		
		case CALL_ACTIVE_VOICE:  // Call in progress, bluetooth audio is routed to us
			if (!bt_in_service) {
				_appSetState(DISCONNECTED);
			} else if (notify_dial_btn_pressed || !pots_off_hook) {
				// Either the user ended the call from the GUI or our phone hung up the call
				// so tell cellphone to end call
				_appSetState(CALL_WAIT_END);
			} else if (!bt_audio_connected) {
				if (!bt_in_call) {
					// Cellphone ended the call so wait for phone to hang up
					_appSetState(CALL_WAIT_ONHOOK);
				} else {
					// We no longer have bluetooth audio (the cellphone changed the audio destination)
					_appSetState(CALL_ACTIVE);
				}
			}
			break;
		
		case CALL_WAIT_END:  // Waiting for bluetooth to indicate call has ended
			if (!bt_in_service) {
				_appSetState(DISCONNECTED);
			} else if (bt_audio_connected && pots_off_hook) {
					// Back to having some sort of a call
					_appSetState(CALL_ACTIVE_VOICE);
			} else if (!bt_in_call) {
				if (pots_off_hook) {
					_appSetState(CALL_WAIT_ONHOOK);
				} else {
					_appSetState(CONNECTED_IDLE);
				}
			}
			break;
		
		case CALL_WAIT_ONHOOK:  // Call ended, waiting for user to hang up phone
			if (!bt_in_service) {
				_appSetState(DISCONNECTED);
			} else if (!pots_off_hook) {
				_appSetState(CONNECTED_IDLE);
			}
			break;
	}
	
	// Clear notifiers
	notify_dial_btn_pressed = false;
	notify_bt_ring_indication = false;
}


static void _appSetState(app_state_t st)
{	
	switch (st) {
		case DISCONNECTED:
			xTaskNotify(task_handle_pots, POTS_NOTIFY_OUT_OF_SERVICE_MASK, eSetBits);
			break;
		
		case CONNECTED_IDLE:
			xTaskNotify(task_handle_pots, POTS_NOTIFY_IN_SERVICE_MASK, eSetBits);
			
			// Reset state
			_appInvalidateCID();
			cid_valid = false;
			ring_count = 0;
			
			// Reset number indication on GUI
			_appInvalidateDialingNum();
			xTaskNotify(task_handle_gui, GUI_NOTIFY_PH_NUM_UPDATE_MASK, eSetBits);
			break;
		
		case CALL_RECEIVED:
			xTaskNotify(task_handle_gcore, GCORE_NOTIFY_ACTIVITY_MASK, eSetBits);
			
			// Setup unanswered call detection timer
			call_received_timer = 0;
			break;
		
		case CALL_WAIT_ACTIVE:
			xTaskNotify(task_handle_bt, BT_NOTIFY_ANSWER_CALL_MASK, eSetBits);
			break;
			
		case DIALING:
			// Setup to start dialing
			dialing_pots_digit_timer = 0;
			break;
		
		case CALL_INITIATED:
			if (_appCanInitiateAssistantCall()) {
				xTaskNotify(task_handle_bt, BT_NOTIFY_DIAL_OPER_MASK, eSetBits);
			} else {
				bt_set_outgoing_number(dialing_num);
				xTaskNotify(task_handle_bt, BT_NOTIFY_DIAL_NUM_MASK, eSetBits);
			}
			break;
		
		case CALL_ACTIVE:
			break;
			
		case CALL_ACTIVE_VOICE:
			break;
		
		case CALL_WAIT_END:
			xTaskNotify(task_handle_bt, BT_NOTIFY_HANGUP_CALL_MASK, eSetBits);
			break;
		
		case CALL_WAIT_ONHOOK:
			break;
	}
	
#ifdef APP_ST_DEBUG
	STATE_CHANGE_PRINT(app_state, st, app_state_name);
#endif
	app_state = st;
	
	// Let the GUI know about our state change
	xTaskNotify(task_handle_gui, GUI_NOTIFY_STATUS_UPDATE_MASK, eSetBits);
}


static bool _appCanInitiateAssistantCall()
{
	return ((dialing_num_valid == 1) && (dialing_num[0] == '0'));
}


static void _appInvalidateDialingNum()
{
	dialing_num_valid = 0;
}


static void _appInvalidateCID()
{
	// Create empty string
	cid_num[0] = 0;
}

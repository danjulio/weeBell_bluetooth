/*
  * Bluetooth manager
 *   - Start Bluetooth classic
 *   - Start handsfree (HF) protocol
 *   - Provide GAP and HF callbacks
 *   - Bluetooth/HF state management
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
#include "app_task.h"
#include "audio_task.h"
#include "bt_task.h"
#include "gui_task.h"
#include "pots_task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_hf_client_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gain.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "ps.h"
#include "sys_common.h"
#include <string.h>

//
// Constants
//

// Uncomment for state debug
#define BT_STATE_DEBUG

// Uncomment for full GAP event logging (including unused events)
#define BT_GAP_EVENT_DEBUG

// Uncomment for full HF event logging (including unused events
#define BT_HF_EVENT_DEBUG



//
// Variables
//

static const char* TAG = "bt_task";
static const char* GAP_TAG = "bt_gap";  // Bluetooth stack gap callback
static const char* HF_TAG = "bt_hf";    // Bluetooth stack handsfree callback

// State
static int bt_reconnect_count = 0;               // Counts evaluation periods up while disconnected before attempting to reconnect
static bool bt_in_service = false;               // Set when a HF Bluetooth SLC connection exists, clear when nothing connected
static bool bt_in_call = false;                  // Set when call active, clear when call inactive (CALL_SETUP_IND_EVT)
static bool bt_audio_connected = false;          // Set when audio is connected, clear when there is no audio connection
static float bt_cur_mic_gain;
static float bt_cur_spk_gain;

// Pin for traditional pairing (non SSP)
const esp_bt_pin_code_t bt_trad_pin = BLUETOOTH_PIN_ARRAY;

// Notification flags - set by a notification and consumed/cleared by state evaluation
static bool notify_bt_dial_num = false;
static bool notify_bt_dial_oper = false;
static bool notify_bt_answer = false;
static bool notify_bt_hangup = false;

// Phone numbers
static char outgoing_phone_num[APP_MAX_DIALED_DIGITS+1];   // Statically allocated phone number buffer
static char outgoing_dtmf_digit;

// Bluetooth state
static const char* bt_state_name[] = {"DISCONNECTED", "CONNECTED-IDLE", "INITIATED", "ACTIVE", "WAIT_END"};
typedef enum {BT_DISCONNECTED, BT_CONNECTED_IDLE, BT_CALL_INITIATED, BT_CALL_ACTIVE, BT_WAIT_END} bt_stateT;
static bt_stateT bt_state = BT_DISCONNECTED;

// Remote device
static esp_bd_addr_t peer_addr;
static char peer_bdname[ESP_BT_GAP_MAX_BDNAME_LEN + 1];
static uint8_t peer_bdname_len;

#if (CONFIG_BT_SSP_ENABLED == true)
static esp_bd_addr_t ssp_pairing_addr;
#endif

static const char device_name[] = "weeBell";
static char peer_device_name[ESP_BT_GAP_MAX_BDNAME_LEN + 1];



//
// Forward declarations for Espressif bluetooth stack callbacks and related functions
//
static void _bt_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);
static bool _get_name_from_eir(uint8_t *eir, char *bdname, uint8_t *bdname_len);
static void _bt_hf_client_cb(esp_hf_client_cb_event_t event, esp_hf_client_cb_param_t *param);
static void _bt_hf_client_audio_open(bool is_msbc);
static void _bt_hf_client_audio_close(void);
static uint32_t _bt_hf_client_outgoing_cb(uint8_t *p_buf, uint32_t sz);
static void _bt_hf_client_incoming_cb(const uint8_t *buf, uint32_t sz);



//
// Forward declarations for internal functions used by bt_task
//
static bool _btStartBluetooth();
static void _bt_cleanup_bond_info();
static bool _bt_validate_bond_info();
static bool _bt_addr_match(uint8_t a1[], uint8_t a2[]);
static void _btEval();
static void _btSetState(bt_stateT s);
static void _btHandleNotifications();



//
// API
//
void bt_task(void* args)
{
	ESP_LOGI(TAG, "Start task");
	
	// Get gain values from persistent storage
	bt_cur_mic_gain = ps_get_gain(PS_GAIN_MIC);
	bt_cur_spk_gain = ps_get_gain(PS_GAIN_SPK);
	
	// Attempt to start the bluetooth stack
	if (!_btStartBluetooth()) {
		ESP_LOGE(TAG, "Bluetooth stack init failed");
		gui_set_fatal_error("Bluetooth stack init failed");
		vTaskDelete(NULL);
	}
	
	// Currently we only support one paired connection at a time.  The Espressif bluetooth
	// stack stores bond information in ESP32 NVS.  To prevent any possible funny business
	// with it thinking it can connect and us not thinking we're paired, we delete all
	// bonds if we don't think we are paired.
	if (!ps_get_bt_is_paired()) {
		_bt_cleanup_bond_info();
	}
	
	// Preset our reconnect timer so we'll immediately try to connect if we're paired
	bt_reconnect_count = (BT_RECONNECT_MSEC / BT_EVAL_MSEC);
	
	while (true) {
		_btHandleNotifications();
		_btEval();
		
		vTaskDelay(pdMS_TO_TICKS(BT_EVAL_MSEC));
	}
}

void bt_set_outgoing_number(const char* buf)
{
	strncpy(outgoing_phone_num, buf, APP_MAX_DIALED_DIGITS);
	outgoing_phone_num[APP_MAX_DIALED_DIGITS] = 0;
}


void bt_set_dtmf_digit(const char d)
{
	outgoing_dtmf_digit = d;
}



//
// Espressif bluetooth stack callbacks and related functions
//
const char *c_hf_evt_str[] = {
    "CONNECTION_STATE_EVT",              /*!< connection state changed event */
    "AUDIO_STATE_EVT",                   /*!< audio connection state change event */
    "VR_STATE_CHANGE_EVT",                /*!< voice recognition state changed */
    "CALL_IND_EVT",                      /*!< call indication event */
    "CALL_SETUP_IND_EVT",                /*!< call setup indication event */
    "CALL_HELD_IND_EVT",                 /*!< call held indicator event */
    "NETWORK_STATE_EVT",                 /*!< network state change event */
    "SIGNAL_STRENGTH_IND_EVT",           /*!< signal strength indication event */
    "ROAMING_STATUS_IND_EVT",            /*!< roaming status indication event */
    "BATTERY_LEVEL_IND_EVT",             /*!< battery level indication event */
    "CURRENT_OPERATOR_EVT",              /*!< current operator name event */
    "RESP_AND_HOLD_EVT",                 /*!< response and hold event */
    "CLIP_EVT",                          /*!< Calling Line Identification notification event */
    "CALL_WAITING_EVT",                  /*!< call waiting notification */
    "CLCC_EVT",                          /*!< listing current calls event */
    "VOLUME_CONTROL_EVT",                /*!< audio volume control event */
    "AT_RESPONSE",                       /*!< audio volume control event */
    "SUBSCRIBER_INFO_EVT",               /*!< subscriber information event */
    "INBAND_RING_TONE_EVT",              /*!< in-band ring tone settings */
    "LAST_VOICE_TAG_NUMBER_EVT",         /*!< requested number from AG event */
    "RING_IND_EVT",                      /*!< ring indication event */
};

// esp_hf_client_connection_state_t
const char *c_connection_state_str[] = {
    "disconnected",
    "connecting",
    "connected",
    "slc_connected",
    "disconnecting",
};

// esp_hf_client_audio_state_t
const char *c_audio_state_str[] = {
    "disconnected",
    "connecting",
    "connected",
    "connected_msbc",
};

/// esp_hf_vr_state_t
const char *c_vr_state_str[] = {
    "disabled",
    "enabled",
};

// esp_hf_service_availability_status_t
const char *c_service_availability_status_str[] = {
    "unavailable",
    "available",
};

// esp_hf_roaming_status_t
const char *c_roaming_status_str[] = {
    "inactive",
    "active",
};

// esp_hf_client_call_state_t
const char *c_call_str[] = {
    "NO call in progress",
    "call in progress",
};

// esp_hf_client_callsetup_t
const char *c_call_setup_str[] = {
    "NONE",
    "INCOMING",
    "OUTGOING_DIALING",
    "OUTGOING_ALERTING"
};

// esp_hf_client_callheld_t
const char *c_call_held_str[] = {
    "NONE held",
    "Held and Active",
    "Held",
};

// esp_hf_response_and_hold_status_t
const char *c_resp_and_hold_str[] = {
    "HELD",
    "HELD ACCEPTED",
    "HELD REJECTED",
};

// esp_hf_client_call_direction_t
const char *c_call_dir_str[] = {
    "outgoing",
    "incoming",
};

// esp_hf_client_call_state_t
const char *c_call_state_str[] = {
    "active",
    "held",
    "dialing",
    "alerting",
    "incoming",
    "waiting",
    "held_by_resp_hold",
};

// esp_hf_current_call_mpty_type_t
const char *c_call_mpty_type_str[] = {
    "single",
    "multi",
};

// esp_hf_volume_control_target_t
const char *c_volume_control_target_str[] = {
    "SPEAKER",
    "MICROPHONE"
};

// esp_hf_at_response_code_t
const char *c_at_response_code_str[] = {
    "OK",
    "ERROR"
    "ERR_NO_CARRIER",
    "ERR_BUSY",
    "ERR_NO_ANSWER",
    "ERR_DELAYED",
    "ERR_BLACKLILSTED",
    "ERR_CME",
};

// esp_hf_subscriber_service_type_t
const char *c_subscriber_service_type_str[] = {
    "unknown",
    "voice",
    "fax",
};

// esp_hf_client_in_band_ring_state_t
const char *c_inband_ring_state_str[] = {
    "NOT provided",
    "Provided",
};


void _bt_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
		case ESP_BT_GAP_AUTH_CMPL_EVT: {
	        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
	            ESP_LOGI(GAP_TAG, "authentication success: %s", param->auth_cmpl.device_name);
	            esp_log_buffer_hex(GAP_TAG, param->auth_cmpl.bda, ESP_BD_ADDR_LEN);
	            gui_set_new_pair_info(param->auth_cmpl.bda, (char*) param->auth_cmpl.device_name);
	            xTaskNotify(task_handle_gui, GUI_NOTIFY_NEW_PAIR_INFO_MASK, eSetBits);
	            
	            // Don't immediately try to force a connection as a connection should
	            // be under way as part of the pairing success and we don't want a race condition
	            // that causes a failed connect
	            bt_reconnect_count = ((BT_RECONNECT_MSEC - 3000) / BT_EVAL_MSEC);
	        } else {
	            ESP_LOGE(GAP_TAG, "authentication failed, status:%d", param->auth_cmpl.stat);
	            xTaskNotify(task_handle_gui, GUI_NOTIFY_BT_AUTH_FAIL_MASK, eSetBits);
	        }
	        break;
	    }
		
#if (CONFIG_BT_SSP_ENABLED == true)
 	   case ESP_BT_GAP_CFM_REQ_EVT:
 	       ESP_LOGI(GAP_TAG, "ESP_BT_GAP_CFM_REQ_EVT Please compare the numeric value: %d", param->cfm_req.num_val);
 	       
 	       // Save the BT addr for use when we confirm
 	       memcpy(ssp_pairing_addr, param->cfm_req.bda, ESP_BD_ADDR_LEN);
 	       
 	       // Notify GUI to display this number for confirmation
 	       gui_set_new_pair_ssp_pin(param->cfm_req.num_val);
 	       xTaskNotify(task_handle_gui, GUI_NOTIFY_NEW_SSP_PIN_MASK, eSetBits);
 	       break;
#endif

	    case ESP_BT_GAP_PIN_REQ_EVT: {
	        ESP_LOGI(GAP_TAG, "ESP_BT_GAP_PIN_REQ_EVT min_16_digit:%d", param->pin_req.min_16_digit);
	        if (param->pin_req.min_16_digit) {
	            esp_bt_gap_pin_reply(param->pin_req.bda, true, 16, bt_trad_pin);
	        } else {
	            esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, bt_trad_pin);
	        }
	        break;
	    }
	    
//
// Events below aren't expected to be seen or need to be dealt with
//
#ifdef BT_GAP_EVENT_DEBUG
	    case ESP_BT_GAP_DISC_RES_EVT: {
	        for (int i = 0; i < param->disc_res.num_prop; i++){
	            if (param->disc_res.prop[i].type == ESP_BT_GAP_DEV_PROP_EIR
	                && _get_name_from_eir(param->disc_res.prop[i].val, peer_bdname, &peer_bdname_len)){
	                
	                ESP_LOGI(GAP_TAG, "Discovery found target device (%s) address: ", peer_bdname);
	                esp_log_buffer_hex(GAP_TAG, param->disc_res.bda, ESP_BD_ADDR_LEN);
	                
	                if (_bt_addr_match(param->disc_res.bda, peer_addr) && (strcmp(peer_bdname, peer_device_name) == 0)) {
	                    ESP_LOGI(GAP_TAG, "Found our paired device...connecting");
	                    esp_hf_client_connect(peer_addr);
	                    esp_bt_gap_cancel_discovery();
	                }
	            }
	        }
	        break;
	    }
	    
	    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
	    	if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED) {
	        	ESP_LOGI(GAP_TAG, "ESP_BT_GAP_DISC_STATE_CHANGED_EVT - started");
	        } else if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
	        	ESP_LOGI(GAP_TAG, "ESP_BT_GAP_DISC_STATE_CHANGED_EVT - stopped");
	        }
	        break;
	
#if (CONFIG_BT_SSP_ENABLED == true)
	    case ESP_BT_GAP_KEY_NOTIF_EVT:
	        ESP_LOGI(GAP_TAG, "ESP_BT_GAP_KEY_NOTIF_EVT passkey:%d", param->key_notif.passkey);
	        break;
	        
	    case ESP_BT_GAP_KEY_REQ_EVT:
	        ESP_LOGI(GAP_TAG, "ESP_BT_GAP_KEY_REQ_EVT Please enter passkey!");
	        break;
#endif
	
	    case ESP_BT_GAP_MODE_CHG_EVT:
	        ESP_LOGI(GAP_TAG, "ESP_BT_GAP_MODE_CHG_EVT mode:%d", param->mode_chg.mode);
	        break;
	    
	    case ESP_BT_GAP_REMOVE_BOND_DEV_COMPLETE_EVT:
	    	ESP_LOGI(GAP_TAG, "ESP_BT_GAP_REMOVE_BOND_DEV_COMPLETE_EVT status:%d", param->remove_bond_dev_cmpl.status);
	    	esp_log_buffer_hex(GAP_TAG, param->remove_bond_dev_cmpl.bda, ESP_BD_ADDR_LEN);
	    	
	    	break;
#endif /* BT_GAP_EVENT_DEBUG */

	    default: {
#ifdef BT_GAP_EVENT_DEBUG
 	       ESP_LOGI(GAP_TAG, "event: %d", event);
#endif
		    break;
	    }
    }
}


static bool _get_name_from_eir(uint8_t *eir, char *bdname, uint8_t *bdname_len)
{
    uint8_t *rmt_bdname = NULL;
    uint8_t rmt_bdname_len = 0;

    if (!eir) {
        return false;
    }

    rmt_bdname = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &rmt_bdname_len);
    if (!rmt_bdname) {
        rmt_bdname = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &rmt_bdname_len);
    }

    if (rmt_bdname) {
        if (rmt_bdname_len > ESP_BT_GAP_MAX_BDNAME_LEN) {
            rmt_bdname_len = ESP_BT_GAP_MAX_BDNAME_LEN;
        }

        if (bdname) {
            memcpy(bdname, rmt_bdname, rmt_bdname_len);
            bdname[rmt_bdname_len] = '\0';
        }
        if (bdname_len) {
            *bdname_len = rmt_bdname_len;
        }
        return true;
    }

    return false;
}


void _bt_hf_client_cb(esp_hf_client_cb_event_t event, esp_hf_client_cb_param_t *param)
{
    if (event <= ESP_HF_CLIENT_RING_IND_EVT) {
        ESP_LOGI(HF_TAG, "APP HFP event: %s", c_hf_evt_str[event]);
    } else {
        ESP_LOGE(HF_TAG, "APP HFP invalid event %d", event);
    }

    switch (event) {
    	case ESP_HF_CLIENT_RING_IND_EVT:
    	{
    		xTaskNotify(task_handle_app, APP_NOTIFY_BT_RING_MASK, eSetBits);
    		xTaskNotify(task_handle_pots, POTS_NOTIFY_RING_MASK, eSetBits);
    		break;
    	}
    	
        case ESP_HF_CLIENT_CONNECTION_STATE_EVT:
        {
            ESP_LOGI(HF_TAG, "--connection state %s, peer feats 0x%x, chld_feats 0x%x",
                    c_connection_state_str[param->conn_stat.state],
                    param->conn_stat.peer_feat,
                    param->conn_stat.chld_feat);
            
            if (param->conn_stat.state == ESP_HF_CLIENT_CONNECTION_STATE_SLC_CONNECTED) {
            	xTaskNotify(task_handle_bt, BT_NOTIFY_SLC_CON_MASK, eSetBits);
            } else if (param->conn_stat.state == ESP_HF_CLIENT_CONNECTION_STATE_DISCONNECTED) {
            	xTaskNotify(task_handle_bt, BT_NOTIFY_SLC_DIS_MASK, eSetBits);
            }
            break;
        }

        case ESP_HF_CLIENT_AUDIO_STATE_EVT:
        {
            ESP_LOGI(HF_TAG, "--audio state %s",
                    c_audio_state_str[param->audio_stat.state]);
#if CONFIG_BT_HFP_AUDIO_DATA_PATH_HCI
            if (param->audio_stat.state == ESP_HF_CLIENT_AUDIO_STATE_CONNECTED ||
                param->audio_stat.state == ESP_HF_CLIENT_AUDIO_STATE_CONNECTED_MSBC) {
                esp_hf_client_register_data_callback(_bt_hf_client_incoming_cb,
                                                    _bt_hf_client_outgoing_cb);
                _bt_hf_client_audio_open(param->audio_stat.state == ESP_HF_CLIENT_AUDIO_STATE_CONNECTED_MSBC);
                
                // Inform the cellphone of our current volume settings
                esp_hf_client_volume_update(ESP_HF_VOLUME_CONTROL_TARGET_MIC, gainDB2BT(GAIN_TYPE_MIC, bt_cur_mic_gain));
                esp_hf_client_volume_update(ESP_HF_VOLUME_CONTROL_TARGET_SPK, gainDB2BT(GAIN_TYPE_SPK, bt_cur_spk_gain));
            } else if (param->audio_stat.state == ESP_HF_CLIENT_AUDIO_STATE_DISCONNECTED) {
                _bt_hf_client_audio_close();
            }
#endif
            break;
        }
    	
        case ESP_HF_CLIENT_CIND_CALL_SETUP_EVT:
        {
            ESP_LOGI(HF_TAG, "--Call setup indicator %s",
                    c_call_setup_str[param->call_setup.status]);
            if (param->call_setup.status == ESP_HF_CALL_SETUP_STATUS_IDLE) {
            	xTaskNotify(task_handle_bt, BT_NOTIFY_CALL_INACT_MASK, eSetBits);
            } else {
            	xTaskNotify(task_handle_bt, BT_NOTIFY_CALL_ACT_MASK, eSetBits);
            }
            break;
        }

        case ESP_HF_CLIENT_CLIP_EVT:
        {
            ESP_LOGI(HF_TAG, "--clip number %s",
                    (param->clip.number == NULL) ? "NULL" : (param->clip.number));
            app_set_cid_number(param->clip.number);
            xTaskNotify(task_handle_app, APP_NOTIFY_BT_CID_AVAILABLE_MASK, eSetBits);
            break;
        }

        case ESP_HF_CLIENT_VOLUME_CONTROL_EVT:
        {
            ESP_LOGI(HF_TAG, "--volume_target: %s, volume %d",
                    c_volume_control_target_str[param->volume_control.type],
                    param->volume_control.volume);
            if (param->volume_control.type == ESP_HF_VOLUME_CONTROL_TARGET_MIC) {
            	app_set_new_mic_gain(gainBT2DB(GAIN_TYPE_MIC, param->volume_control.volume));
            	xTaskNotify(task_handle_app, APP_NOTIFY_NEW_BT_MIC_GAIN_MASK, eSetBits);
            } else if (param->volume_control.type == ESP_HF_VOLUME_CONTROL_TARGET_SPK) {
            	app_set_new_spk_gain(gainBT2DB(GAIN_TYPE_SPK, param->volume_control.volume));
            	xTaskNotify(task_handle_app, APP_NOTIFY_NEW_BT_SPK_GAIN_MASK, eSetBits);
            }
            break;
        }

//
// Events below aren't expected to be seen or need to be dealt with
//
#ifdef BT_HF_EVENT_DEBUG
        case ESP_HF_CLIENT_BVRA_EVT:
        {
            ESP_LOGI(HF_TAG, "--VR state %s",
                    c_vr_state_str[param->bvra.value]);
            break;
        }

        case ESP_HF_CLIENT_CIND_SERVICE_AVAILABILITY_EVT:
        {
            ESP_LOGI(HF_TAG, "--NETWORK STATE %s",
                    c_service_availability_status_str[param->service_availability.status]);
            break;
        }

        case ESP_HF_CLIENT_CIND_ROAMING_STATUS_EVT:
        {
            ESP_LOGI(HF_TAG, "--ROAMING: %s",
                    c_roaming_status_str[param->roaming.status]);
            break;
        }

        case ESP_HF_CLIENT_CIND_SIGNAL_STRENGTH_EVT:
        {
            ESP_LOGI(HF_TAG, "-- signal strength: %d",
                    param->signal_strength.value);
            break;
        }

        case ESP_HF_CLIENT_CIND_BATTERY_LEVEL_EVT:
        {
            ESP_LOGI(HF_TAG, "--battery level %d",
                    param->battery_level.value);
            break;
        }

        case ESP_HF_CLIENT_COPS_CURRENT_OPERATOR_EVT:
        {
            ESP_LOGI(HF_TAG, "--operator name: %s",
                    param->cops.name);
            break;
        }

        case ESP_HF_CLIENT_CIND_CALL_EVT:
        {
            ESP_LOGI(HF_TAG, "--Call indicator %s",
                    c_call_str[param->call.status]);
            break;
        }

        case ESP_HF_CLIENT_CIND_CALL_HELD_EVT:
        {
            ESP_LOGI(HF_TAG, "--Call held indicator %s",
                    c_call_held_str[param->call_held.status]);
            break;
        }

        case ESP_HF_CLIENT_BTRH_EVT:
        {
            ESP_LOGI(HF_TAG, "--response and hold %s",
                    c_resp_and_hold_str[param->btrh.status]);
            break;
        }

        case ESP_HF_CLIENT_CCWA_EVT:
        {
            ESP_LOGI(HF_TAG, "--call_waiting %s",
                    (param->ccwa.number == NULL) ? "NULL" : (param->ccwa.number));
            break;
        }

        case ESP_HF_CLIENT_CLCC_EVT:
        {
            ESP_LOGI(HF_TAG, "--Current call: idx %d, dir %s, state %s, mpty %s, number %s",
                    param->clcc.idx,
                    c_call_dir_str[param->clcc.dir],
                    c_call_state_str[param->clcc.status],
                    c_call_mpty_type_str[param->clcc.mpty],
                    (param->clcc.number == NULL) ? "NULL" : (param->clcc.number));
            break;
        }

        case ESP_HF_CLIENT_AT_RESPONSE_EVT:
        {
            ESP_LOGI(HF_TAG, "--AT response event, code %d, cme %d",
                    param->at_response.code, param->at_response.cme);
            break;
        }

        case ESP_HF_CLIENT_CNUM_EVT:
        {
            ESP_LOGI(HF_TAG, "--subscriber type %s, number %s",
                    c_subscriber_service_type_str[param->cnum.type],
                    (param->cnum.number == NULL) ? "NULL" : param->cnum.number);
            break;
        }

        case ESP_HF_CLIENT_BSIR_EVT:
        {
            ESP_LOGI(HF_TAG, "--inband ring state %s",
                    c_inband_ring_state_str[param->bsir.state]);
            break;
        }

        case ESP_HF_CLIENT_BINP_EVT:
        {
            ESP_LOGI(HF_TAG, "--last voice tag number: %s",
                    (param->binp.number == NULL) ? "NULL" : param->binp.number);
            break;
        }
#endif /* BT_HF_EVENT_DEBUG */

        default:
#ifdef BT_HF_EVENT_DEBUG
            ESP_LOGE(HF_TAG, "HF_CLIENT EVT: %d", event);
#endif
            break;
    }
}


static void _bt_hf_client_audio_open(bool is_msbc)
{
	xTaskNotify(task_handle_bt, BT_NOTIFY_AUDIO_CON_MASK, eSetBits);
	xTaskNotify(task_handle_app, APP_NOTIFY_BT_AUDIO_START_MASK, eSetBits);
	xTaskNotify(task_handle_pots, (is_msbc) ? POTS_NOTIFY_AUDIO_16K_MASK : POTS_NOTIFY_AUDIO_8K_MASK, eSetBits);
	
	ESP_LOGI(HF_TAG, "Using %d kHz sampling", is_msbc ? 16 : 8);
}


static void _bt_hf_client_audio_close(void)
{
	xTaskNotify(task_handle_app, APP_NOTIFY_BT_AUDIO_ENDED_MASK, eSetBits);
	xTaskNotify(task_handle_bt, BT_NOTIFY_AUDIO_DIS_MASK, eSetBits);
	xTaskNotify(task_handle_pots, POTS_NOTIFY_AUDIO_DIS_MASK, eSetBits);
}


static uint32_t _bt_hf_client_outgoing_cb(uint8_t *p_buf, uint32_t sz)
{
	audioGetVoiceRx((int16_t*) p_buf, sz/2);
	return sz;
}


static void _bt_hf_client_incoming_cb(const uint8_t *buf, uint32_t sz)
{
	audioPutVoiceTx((int16_t*) buf, sz/2);
    esp_hf_client_outgoing_data_ready();
}


//
// Internal functions for bt_task
//
static bool _btStartBluetooth()
{
	esp_err_t ret;
	esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
	
	// Initialize NVS used to store PHY calibration data
	ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
    	if ((ret = nvs_flash_erase()) != ESP_OK) {
    		ESP_LOGE(TAG, "nvs_flash_erase failed: (%s)", esp_err_to_name(ret));
    		return false;
    	}
        
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
    	ESP_LOGE(TAG, "nvs_flash_init failed: (%s)", esp_err_to_name(ret));
    	return false;
    }

	// Not using BLE
	if ((ret = esp_bt_controller_mem_release(ESP_BT_MODE_BLE)) != ESP_OK) {
		ESP_LOGE(TAG, "release BLE controller memory failed (%s)", esp_err_to_name(ret));
		return false;
	}

	// Startup Bluetooth controller
    if ((ret = esp_bt_controller_init(&bt_cfg)) != ESP_OK) {
        ESP_LOGE(TAG, "initialize controller failed (%s)", esp_err_to_name(ret));
        return false;
    }

    if ((ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT)) != ESP_OK) {
        ESP_LOGE(TAG, "enable controller failed (%s)", esp_err_to_name(ret));
        return false;
    }

	// Startup bluedroid
    if ((ret = esp_bluedroid_init()) != ESP_OK) {
        ESP_LOGE(TAG, "initialize bluedroid failed (%s)", esp_err_to_name(ret));
        return false;
    }

    if ((ret = esp_bluedroid_enable()) != ESP_OK) {
        ESP_LOGE(TAG, "enable bluedroid failed (%s)", esp_err_to_name(ret));
        return false;
    }
    
    // Setup class of device
    esp_bt_cod_t cod;
    cod.reserved_2 = 0;
	cod.minor = 0x02;     // binary 000010 (Handsfree device)
	cod.major = 0x04;     // binary 00100 (Audio/Video)
	cod.reserved_8 = 0;
	cod.service = 0x100;  // binary 00100000000 (Audio)
    if ((ret = esp_bt_gap_set_cod(cod, ESP_BT_INIT_COD)) != ESP_OK) {
    	ESP_LOGE(TAG, "configure COD failed (%s)", esp_err_to_name(ret));
        return false;
    }
    
    // Set up device name
    esp_bt_dev_set_device_name(device_name);
    
    // Register GAP callback function
	esp_bt_gap_register_callback(_bt_gap_cb);
	
	// Register HF callback function and startup HF client
	esp_hf_client_register_callback(_bt_hf_client_cb);
	esp_hf_client_init();
	
#if (CONFIG_BT_SSP_ENABLED == true)
    /* Set default parameters for Secure Simple Pairing */
    esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_IO;
    esp_bt_gap_set_security_param(param_type, &iocap, sizeof(uint8_t));
#endif
	
	esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_VARIABLE;
	esp_bt_pin_code_t pin_code;
	esp_bt_gap_set_pin(pin_type, 0, pin_code);
	
	ESP_LOGI(TAG, "Own Address:");
	esp_log_buffer_hex(TAG, esp_bt_dev_get_address(), ESP_BD_ADDR_LEN);
	
	// set discoverable and connectable mode
	esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
	
	return true;
}


static void _bt_cleanup_bond_info()
{
	esp_err_t ret;
	int paired_devs;
	uint8_t paired_devs_macs[10][6];
	
	paired_devs = esp_bt_gap_get_bond_device_num();
	
	if (paired_devs != 0) {
		if (paired_devs > 10) paired_devs = 10;
		ESP_LOGI(TAG, "Cleaning out %d old bonded device(s)", paired_devs);
		ret = esp_bt_gap_get_bond_device_list(&paired_devs, paired_devs_macs);
		if (ret == ESP_OK) {
			for (int i = 0; i < paired_devs; i++) {
				(void) esp_bt_gap_remove_bond_device(paired_devs_macs[i]);
			}
		}
	}
}


// peer_addr must be valid on entry
static bool _bt_validate_bond_info()
{
	esp_err_t ret;
	int paired_devs;
	uint8_t paired_devs_macs[10][6];
	
	paired_devs = esp_bt_gap_get_bond_device_num();
	
	if (paired_devs == 0) {
		return false;
	} else {
		ESP_LOGI(TAG, "Found %d bonded device(s)", paired_devs);
		if (paired_devs > 10) paired_devs = 10;
		ret = esp_bt_gap_get_bond_device_list(&paired_devs, paired_devs_macs);
		if (ret != ESP_OK) {
			ESP_LOGE(TAG, "esp_bt_gap_get_bond_device_list returned %d", ret);
			return false;
		} else {
			for (int i = 0; i < paired_devs; i++) {
				if (_bt_addr_match(paired_devs_macs[i], peer_addr)) {
					return true;
				}
			}
			
			return false;
		}
	}
}


static bool _bt_addr_match(uint8_t a1[], uint8_t a2[])
{
	for (int i=0; i<ESP_BD_ADDR_LEN; i++) {
		if (a1[i] != a2[i]) {
			return false;
		}
	}
	
	return true;
}


static void _btEval()
{
	switch (bt_state) {
		// No bluetooth connection
		case BT_DISCONNECTED:
			if (bt_in_service) {
				_btSetState(BT_CONNECTED_IDLE);
			} else {
				// Look to see if we can try to connect to something
				if (ps_get_bt_is_paired()) {
					if (++bt_reconnect_count >= (BT_RECONNECT_MSEC / BT_EVAL_MSEC)) {
						bt_reconnect_count = 0;
						ps_get_bt_pair_addr((uint8_t*) peer_addr);
						ps_get_bt_pair_name(peer_device_name);
						if (!_bt_validate_bond_info()) {
							ESP_LOGE(TAG, "Could not find bond information for %s - forgetting pairing...", peer_device_name);
							xTaskNotify(task_handle_gui, GUI_NOTIFY_FORGET_PAIRING_MASK, eSetBits);
						} else {
							ESP_LOGI(TAG, "Attempting to connect to %s:", peer_device_name);
							esp_log_buffer_hex(TAG, peer_addr, ESP_BD_ADDR_LEN);
							esp_hf_client_connect(peer_addr);
						}
					}
				}
			}
			break;
		
		// Bluetooth connected, no activity
		case BT_CONNECTED_IDLE:
			if (!bt_in_service) {
				_btSetState(BT_DISCONNECTED);
			} else if (notify_bt_answer) {
		 		esp_hf_client_answer_call();
			} else if (bt_in_call) {
		 		_btSetState(BT_CALL_ACTIVE);
			} else if (notify_bt_dial_num || notify_bt_dial_oper) {
				_btSetState(BT_CALL_INITIATED);
			} else if (notify_bt_hangup) {
				// Used to tell cellphone to reject incoming (ringing) call
				esp_hf_client_reject_call();
			}
			break;
		
		// Command sent to cellphone to dial a number but it hasn't yet acknowledged call in progress
		case BT_CALL_INITIATED:
			if (!bt_in_service) {
		 		_btSetState(BT_DISCONNECTED);
		 	} else if (bt_in_call) {
		 		_btSetState(BT_CALL_ACTIVE);
		 	} else if (notify_bt_hangup) {
		 		_btSetState(BT_CONNECTED_IDLE);
		 	}
			break;
				
		// Call in progress
		case BT_CALL_ACTIVE:
			if (!bt_in_service) {
		 		_btSetState(BT_DISCONNECTED);
		 	} else if (!bt_in_call) {
		 		_btSetState(BT_CONNECTED_IDLE);
		 	} else if (notify_bt_hangup) {
		 		_btSetState(BT_WAIT_END);
		 	}
			break;
		
		// Told cellphone to disconnect call, waiting for cellphone to acknowledge call is over
		case BT_WAIT_END:
			if (!bt_in_service) {
		 		_btSetState(BT_DISCONNECTED);
		 	} else if (!bt_in_call) {
		 		_btSetState(BT_CONNECTED_IDLE);
		 	}
			break;
	}
	
	// Clear notifiers
	notify_bt_dial_num = false;
	notify_bt_dial_oper = false;
	notify_bt_answer = false;
	notify_bt_hangup = false;
}


static void _btSetState(bt_stateT s)
{
	switch (s) {
		case BT_DISCONNECTED:
			bt_reconnect_count = (BT_RECONNECT_MSEC / BT_EVAL_MSEC) - 1;  // Attempt to reconnect immediately
			xTaskNotify(task_handle_app, APP_NOTIFY_BT_OUT_OF_SERVICE_MASK, eSetBits);
			
			// Clear any dangling state if BT connection suddenly disappears
			if (bt_in_call) {
				bt_in_call = false;
				xTaskNotify(task_handle_app, APP_NOTIFY_BT_CALL_ENDED_MASK, eSetBits);
			}
			if (bt_audio_connected) {
				bt_audio_connected = false;
				xTaskNotify(task_handle_app, APP_NOTIFY_BT_AUDIO_ENDED_MASK, eSetBits);
				xTaskNotify(task_handle_pots, POTS_NOTIFY_AUDIO_DIS_MASK, eSetBits);
			}
			break;
		
		case BT_CONNECTED_IDLE:
			xTaskNotify(task_handle_app, APP_NOTIFY_BT_IN_SERVICE_MASK, eSetBits);
			xTaskNotify(task_handle_app, APP_NOTIFY_BT_CALL_ENDED_MASK, eSetBits);
			if (bt_state == BT_DISCONNECTED) {
				// Tell the cellphone we'll handle echo cancellation when we first get a SLC
				if (esp_hf_client_send_nrec() != ESP_OK) {
					ESP_LOGE(TAG, "esp_hf_client_send_nrec failed");
				}
			}
			if (bt_state == BT_CALL_INITIATED) {
				// Hang up any initiated or incoming call
				esp_hf_client_reject_call();
				esp_hf_client_stop_voice_recognition();
			}
			
			// Reset our reconnect timer so we'll immediately try to reconnect if become disconnected
			bt_reconnect_count = (BT_RECONNECT_MSEC / BT_EVAL_MSEC);
			break;
		
		case BT_CALL_INITIATED:
			if (notify_bt_dial_num) {
				esp_hf_client_dial(outgoing_phone_num);
				ESP_LOGI(TAG, "Dial %s", outgoing_phone_num);
			} else if (notify_bt_dial_oper) {
				esp_hf_client_start_voice_recognition();
				ESP_LOGI(TAG, "Voice Dial");
			}
			break;
				
		case BT_CALL_ACTIVE:
			xTaskNotify(task_handle_app, APP_NOTIFY_BT_CALL_STARTED_MASK, eSetBits);
			break;
		
		case BT_WAIT_END:
			// Hang up any ongoing call
			esp_hf_client_reject_call();
			esp_hf_client_stop_voice_recognition();
			break;
	}
	
#ifdef BT_STATE_DEBUG
	STATE_CHANGE_PRINT(bt_state, s, bt_state_name);
#endif
	bt_state = s;
}


static void _btHandleNotifications()
{
	uint32_t notification_value = 0;
	
	// Handle notifications (clear them upon reading)
	if (xTaskNotifyWait(0x00, 0xFFFFFFFF, &notification_value, 0)) {
		//
		// Bluetooth stack notifications
		//
		if (Notification(notification_value, BT_NOTIFY_SLC_CON_MASK)) {
			bt_in_service = true;
		}
		if (Notification(notification_value, BT_NOTIFY_SLC_DIS_MASK)) {
			bt_in_service = false;
		}
		
		if (Notification(notification_value, BT_NOTIFY_CALL_ACT_MASK)) {
			bt_in_call = true;
		}
		if (Notification(notification_value, BT_NOTIFY_CALL_INACT_MASK)) {
			bt_in_call = false;
		}
		
		if (Notification(notification_value, BT_NOTIFY_AUDIO_CON_MASK)) {
			bt_audio_connected = true;
		}
		if (Notification(notification_value, BT_NOTIFY_AUDIO_DIS_MASK)) {
			bt_audio_connected = false;
		}
		
		//
		// gcore_task notifications
		if (Notification(notification_value, BT_NOFITY_DISCONNECT_MASK)) {
			// Disconnect if we are powering down 
			if (bt_in_service) {
				esp_hf_client_disconnect(peer_addr);
			}
		}
		
		//
		// app_task notifications
		//
		if (Notification(notification_value, BT_NOTIFY_ANSWER_CALL_MASK)) {
			notify_bt_answer = true;
		}
		if (Notification(notification_value, BT_NOTIFY_HANGUP_CALL_MASK)) {
			notify_bt_hangup = true;
		}
		
		if (Notification(notification_value, BT_NOTIFY_DIAL_NUM_MASK)) {
			notify_bt_dial_num = true;
		}
		if (Notification(notification_value, BT_NOTIFY_DIAL_OPER_MASK)) {
			notify_bt_dial_oper = true;
		}
		if (Notification(notification_value, BT_NOTIFY_DIAL_DTMF_MASK)) {
			if (bt_state == BT_CALL_ACTIVE) {
				esp_hf_client_send_dtmf(outgoing_dtmf_digit);
			}
		}
		if (Notification(notification_value, BT_NOTIFY_NEW_MIC_GAIN_MASK)) {
			// Get the new mic gain value
			bt_cur_mic_gain = ps_get_gain(PS_GAIN_MIC);
			
			// Update the cellphone immediately if we are in a call
			if (bt_state == BT_CALL_ACTIVE) {
				esp_hf_client_volume_update(ESP_HF_VOLUME_CONTROL_TARGET_MIC, gainDB2BT(GAIN_TYPE_MIC, bt_cur_mic_gain));
			}
		}
		if (Notification(notification_value, BT_NOTIFY_NEW_SPK_GAIN_MASK)) {
			// Get the new speaker gain value
			bt_cur_spk_gain = ps_get_gain(PS_GAIN_SPK);
			
			// Update the cellphone immediately if we are in a call
			if (bt_state == BT_CALL_ACTIVE) {
				esp_hf_client_volume_update(ESP_HF_VOLUME_CONTROL_TARGET_SPK, gainDB2BT(GAIN_TYPE_SPK, bt_cur_spk_gain));
			}
		}
		
		//
		// gui_task notifications
		//
		if (Notification(notification_value, BT_NOTIFY_ENABLE_PAIR_MASK)) {
			if (bt_in_service) {
				ESP_LOGI(TAG, "Disconnect client");
				esp_hf_client_disconnect(peer_addr);
			}
			ESP_LOGI(TAG, "Make discoverable");
			esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
		}
		if (Notification(notification_value, BT_NOTIFY_DISABLE_PAIR_MASK)) {
			ESP_LOGI(TAG, "Make not discoverable");
			esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
		}
		if (Notification(notification_value, BT_NOTIFY_FORGET_PAIR_MASK)) {
			if (bt_in_service) {
				ESP_LOGI(TAG, "Disconnect client");
				esp_hf_client_disconnect(peer_addr);
			}
			(void) esp_bt_gap_remove_bond_device(peer_addr);
		}
		
		if (Notification(notification_value, BT_NOTIFY_CONFIRM_PIN_MASK)) {
			ESP_LOGI(TAG, "Confirm SSP pin");
#if (CONFIG_BT_SSP_ENABLED == true)
			esp_bt_gap_ssp_confirm_reply(ssp_pairing_addr, true);
#endif
		}
		if (Notification(notification_value, BT_NOTIFY_DENY_PIN_MASK)) {
			ESP_LOGI(TAG, "Deny SSP pin");
#if (CONFIG_BT_SSP_ENABLED == true)
			esp_bt_gap_ssp_confirm_reply(ssp_pairing_addr, false);
#endif
		}
	}
}

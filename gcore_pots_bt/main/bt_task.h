/*
 * Bluetooth manager
 *   - Start Bluetooth classic
 *   - Start handsfree (HF) protocol
 *   - Provide GAP and HF callbacks
 *   - Bluetooth/HF state management
 *
 * Copyright (c) 2023 Dan Julio
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
#ifndef BT_TASK_H
#define BT_TASK_H

//
// Constants
//

// Evaluation interval
#define BT_EVAL_MSEC 20

// Interval to attempt to reconnect to the specified pairing when bluetooth is disconnected
#define BT_RECONNECT_MSEC 60000

// Notifications
#define BT_NOTIFY_SLC_CON_MASK       0x00000001
#define BT_NOTIFY_SLC_DIS_MASK       0x00000002
#define BT_NOTIFY_CALL_ACT_MASK      0x00000010
#define BT_NOTIFY_CALL_INACT_MASK    0x00000020
#define BT_NOTIFY_AUDIO_CON_MASK     0x00000100
#define BT_NOTIFY_AUDIO_DIS_MASK     0x00000200

#define BT_NOFITY_DISCONNECT_MASK    0x00001000
#define BT_NOTIFY_ANSWER_CALL_MASK   0x00002000
#define BT_NOTIFY_HANGUP_CALL_MASK   0x00004000
#define BT_NOTIFY_DIAL_NUM_MASK      0x00010000
#define BT_NOTIFY_DIAL_OPER_MASK     0x00020000
#define BT_NOTIFY_DIAL_DTMF_MASK     0x00040000

#define BT_NOTIFY_NEW_MIC_GAIN_MASK  0x00100000
#define BT_NOTIFY_NEW_SPK_GAIN_MASK  0x00200000

#define BT_NOTIFY_ENABLE_PAIR_MASK   0x01000000
#define BT_NOTIFY_DISABLE_PAIR_MASK  0x02000000
#define BT_NOTIFY_FORGET_PAIR_MASK   0x04000000
#define BT_NOTIFY_CONFIRM_PIN_MASK   0x10000000
#define BT_NOTIFY_DENY_PIN_MASK      0x20000000



//
// API
//
void bt_task(void* args);
void bt_set_outgoing_number(const char* buf);     // Should be legal phone number
void bt_set_dtmf_digit(const char d);             // Should be 0-9, *, #, A-D

#endif /* BT_TASK_H */
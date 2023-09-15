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
#ifndef POTS_TASK_H
#define POTS_TASK_H

//
// Constants
//

// Pins
#define PIN_RM  32
#define PIN_FR  33
#define PIN_SHK 35


// Pots Task notifications
#define POTS_NOTIFY_IN_SERVICE_MASK      0x00000001
#define POTS_NOTIFY_OUT_OF_SERVICE_MASK  0x00000002
#define POTS_NOTIFY_AUDIO_8K_MASK        0x00000010
#define POTS_NOTIFY_AUDIO_16K_MASK       0x00000020
#define POTS_NOTIFY_AUDIO_DIS_MASK       0x00000040
#define POTS_NOTIFY_MUTE_RING_MASK       0x00000100
#define POTS_NOTIFY_UNMUTE_RING_MASK     0x00000200
#define POTS_NOTIFY_RING_MASK            0x00000400
#define POTS_NOTIFY_DONE_RINGING_MASK    0x00000800
#define POTS_NOTIFY_EXT_DIAL_DIGIT_MASK  0x00001000
#define POTS_NOTIFY_NEW_COUNTRY_MASK     0x00010000


//
// API
//
void pots_task(void* args);
void pots_set_app_dialed_digit(char d);  // Called before sending POTS_NOTIFY_EXT_DIAL_DIGIT_MASK

#endif /* POTS_TASK_H */
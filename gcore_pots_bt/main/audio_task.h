/*
 * I2S Interface
 *   - Initializes HW Codec
 *   - Manages 8 KHz I2S stream to codec
 *   - Provides Line Echo Cancellation (LEC) functionality
 *   - Provides 8 KHz <-> 16 KHz upsample/downsample as necessary
 *   - Provides RX/TX circular buffers plus semaphore protected access routines for tone
 *     generation and voice
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
#ifndef AUDIO_TASK_H
#define AUDIO_TASK_H

#include <inttypes.h>
#include <stdbool.h>

//
// Constants
//

// Audio Task notifications
#define AUDIO_NOTIFY_DISABLE_MASK       0x00000001
#define AUDIO_NOTIFY_EN_TONE_MASK       0x00000002
#define AUDIO_NOTIFY_EN_VOICE_8_MASK    0x00000004
#define AUDIO_NOTIFY_EN_VOICE_16_MASK   0x00000008
#define AUDIO_NOTIFY_MUTE_MIC_MASK      0x00000010
#define AUDIO_NOTIFY_UNMUTE_MIC_MASK    0x00000020



//
// API
//
void audio_task(void* args);

int audioGetTxCount();   // Return the number of valid samples in the TX buffer
int audioGetRxCount();   // Return the number of valid samples in the RX buffer

// Interface for pots_task and tone generation/detection
int audioGetToneRx(int16_t* buf, int len); /* See note */
void audioPutToneTx(int16_t* buf, int len);


// Interface for voice audio task
int audioGetVoiceRx(int16_t* buf, int len);  /* See note */
void audioPutVoiceTx(int16_t* buf, int len);

// Note: Get routines returns number of valid entries but fill in zeros for data not
// present in buffer

#endif /* AUDIO_TASK_H */
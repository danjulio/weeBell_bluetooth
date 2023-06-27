/*
 * Audio sample recording - provides a mechanism to debug the I2S communication
 * RX/TX synchronization and line echo cancellation by recording audio samples
 * and then writing to files on a Micro-SD card.  This code is designed to be
 * conditionally compiled in (for debugging purposes).
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
#ifndef _SAMPLE_H__
#define _SAMPLE_H__

#include <stdbool.h>
#include <stdint.h>

//
// Constants
//
#define SAMPLE_SECS 5
#define SAMPLE_NUM (SAMPLE_SECS*8000)



//
// API
//
#if (CONFIG_AUDIO_SAMPLE_ENABLE == true)
void sample_mem_init();
bool sample_start();          // Returns false if no Micro-SD Card or card can't be initialized
bool sample_in_progress();    // Designed to be called by manager
void sample_end();            // Called after sampling finished to unmount card
void sample_record(int16_t tx, int16_t rx, int16_t ec);   // Designed to be called by audio_task
void sample_save();
#endif

#endif
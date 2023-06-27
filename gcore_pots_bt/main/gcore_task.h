/**
 *
 * gcore_task.h
 *  - Battery voltage and charge state monitoring
 *  - Critical battery voltage monitoring and auto shut down (with re-power on charge start)
 *  - Power button monitoring for manual power off
 *  - Backlight control
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
 */
#ifndef GCORE_TASK_H_
#define GCORE_TASK_H_

#include <stdint.h>
#include "power_utilities.h"


//
// Constants
//

// Task evaluation period (mSec)
#define GCORE_EVAL_MSEC                 100

// Button power-off press detection threshold (mSec)
#define GCORE_BTN_THRESH_MSEC           250

// GUI Power status update rate (mSec)
#define GCORE_PWR_UPDATE_MSEC           1000

// Voltage/Current level logging frequency
#define GCORE_LOG_IV_INFO_MSEC          60000

// Notifications
#define GCORE_NOTIFY_SHUTOFF_MASK       0x00000002
#define GCORE_NOTIFY_ACTIVITY_MASK      0x00000001
#define GCORE_NOTIFY_BRGHT_UPD_MASK     0x00000004



//
// API
//
void gcore_task();
void gcore_get_power_state(enum BATT_STATE_t* bs, enum CHARGE_STATE_t* cs);

#endif /* GCORE_TASK_H_ */

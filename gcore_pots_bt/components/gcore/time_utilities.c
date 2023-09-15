/*
 * Time related utilities
 *
 * Contains functions to interface the RTC to the system timekeeping
 * capabilities and provide application access to the system time.
 *
 * Copyright 2020-2021, 2023 Dan Julio
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
#include "time_utilities.h"
#include "esp_system.h"
#include "esp_log.h"
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>


//
// Time Utilities Constants
//

// Leap year calulator expects year argument as years offset from 1970
#define LEAP_YEAR(Y)     ( ((1970+(Y))>0) && !((1970+(Y))%4) && ( ((1970+(Y))%100) || !((1970+(Y))%400) ) )

// Useful time constants
#define SECS_PER_MIN  ((time_t)(60UL))
#define SECS_PER_HOUR ((time_t)(3600UL))
#define SECS_PER_DAY  ((time_t)(SECS_PER_HOUR * 24UL))
#define DAYS_PER_WEEK ((time_t)(7UL))
#define SECS_PER_WEEK ((time_t)(SECS_PER_DAY * DAYS_PER_WEEK))
#define SECS_PER_YEAR ((time_t)(SECS_PER_DAY * 365UL)) // TODO: ought to handle leap years
#define SECS_YR_2000  ((time_t)(946684800UL)) // the time at the start of y2k

#define MSECS_PER_DAY (SECS_PER_DAY * 1000UL)


// Minimum epoch time (12:00:00 AM Jan 1 2000)
#define MIN_EPOCH_TIME 946684800



//
// Time Utilities date related strings (related to tmElements)
//
static const char* day_strings[] = {
	"Err",
	"Sun",
	"Mon",
	"Tue",
	"Wed",
	"Thu",
	"Fri",
	"Sat"
};

static const char* mon_strings[] = {
	"Err",
	"Jan",
	"Feb",
	"Mar",
	"Apr",
	"May",
	"Jun",
	"Jul",
	"Aug",
	"Sep",
	"Oct",
	"Nov",
	"Dec"
};



//
// Time Utilities Variables
//
static const char* TAG = "time_utilities";


 
//
// Time Utilities API
//

/**
 * Initialize system time from the RTC
 */
void time_init()
{
	char buf[26];
	tmElements_t te;
	struct timeval tv;
	time_t secs;
	
	// Set the system time from the RTC
	secs = rtc_get_time_secs();
	if (secs < MIN_EPOCH_TIME) {
		secs = MIN_EPOCH_TIME;
		(void) rtc_set_time_secs(secs);
	}
	tv.tv_sec = secs;
	tv.tv_usec = 0;
	settimeofday((const struct timeval *) &tv, NULL);
	
	// Diagnostic display of time
	time_get(&te);
	time_get_disp_string(te, buf);
	ESP_LOGI(TAG, "Set time: %s  (epoch secs: %ld)", buf, secs);
}


/**
 * Set the system time and update the RTC
 */
void time_set(tmElements_t te)
{
	char buf[26];
	struct timeval tv;
	time_t secs;
	
	// Set the system time
	secs = rtc_makeTime(te);
	tv.tv_sec = secs;
	tv.tv_usec = 0;
	settimeofday((const struct timeval *) &tv, NULL);
	
	// Then attempt to set the RTC
	if (rtc_write_time(te)) {
		time_get_disp_string(te, buf);
		ESP_LOGI(TAG, "Set RTC time: %s", buf);
	} else {
		ESP_LOGE(TAG, "Update RTC failed");
	}
}


/**
 * Get the delta time between the RTC and the system time (seconds)
 *   delta = RTC Time - System Time 
 *    -> positive if the System is slow
 *    -> negative if the System is fast
 */
int time_delta()
{
	struct timeval tv;
	time_t rtc_secs;
	
	// Get the time from the rtc
	rtc_secs = rtc_get_time_secs();
	
	// Get the current system time from ESP32
	gettimeofday(&tv, NULL);
	if (tv.tv_usec >= 500000) {
		tv.tv_sec += 1;
	}
	
	return (int) (rtc_secs - tv.tv_sec);
}


/**
 * Get the system time
 */
void time_get(tmElements_t* te)
{
	time_t now;
	struct timeval tv;
	struct tm timeinfo;
	
	// Get the time and convert into our simplified tmElements format
	(void) gettimeofday(&tv, NULL);
	//time(&now);
	now = tv.tv_sec;
    localtime_r(&now, &timeinfo);  // Get the unix formatted timeinfo
    mktime(&timeinfo);             // Fill in the DOW and DOY fields
    te->Millisecond = (uint16_t) (tv.tv_usec / 1000);
    te->Second = (uint8_t) timeinfo.tm_sec;
    te->Minute = (uint8_t) timeinfo.tm_min;
    te->Hour = (uint8_t) timeinfo.tm_hour;
    te->Wday = (uint8_t) timeinfo.tm_wday + 1; // Sunday is 1 in our tmElements structure
    te->Day = (uint8_t) timeinfo.tm_mday;
    te->Month = (uint8_t) timeinfo.tm_mon + 1; // January is 1 in our tmElements structure
    te->Year = (uint8_t) timeinfo.tm_year - 70; // tmElements starts at 1970
}


/**
 * Return true if the system time (in seconds) has changed from the last time
 * this function returned true. Each calling task must maintain its own prev_time
 * variable (it can initialize it to 0).  Set te to NULL if you don't need the time.
 */
bool time_changed(tmElements_t* te, time_t* prev_time)
{
	time_t now;
	struct tm timeinfo;
	
	// Get the time and check if it is different
	time(&now);
	if (now != *prev_time) {
		*prev_time = now;
		
		if (te != NULL) {
			// convert time into our simplified tmElements format
    		localtime_r(&now, &timeinfo);  // Get the unix formatted timeinfo
    		mktime(&timeinfo);             // Fill in the DOW and DOY fields
    		te->Millisecond = 0;
    		te->Second = (uint8_t) timeinfo.tm_sec;
    		te->Minute = (uint8_t) timeinfo.tm_min;
    		te->Hour = (uint8_t) timeinfo.tm_hour;
    		te->Wday = (uint8_t) timeinfo.tm_wday + 1; // Sunday is 1 in our tmElements structure
    		te->Day = (uint8_t) timeinfo.tm_mday;
    		te->Month = (uint8_t) timeinfo.tm_mon + 1; // January is 1 in our tmElements structure
    		te->Year = (uint8_t) timeinfo.tm_year - 70; // tmElements starts at 1970
    	}
    	
    	return true;
    } else {
    	return false;
    }
}


/**
 * Load buf with a time & date string for display.
 *
 *   "DOW MON DAY, YEAR HH:MM:SS"
 *
 * buf must be at least 26 bytes long (to include null termination).
 */
void time_get_disp_string(tmElements_t te, char* buf)
{
	// Validate te to prevent illegal accesses to the constant string buffers
	if (te.Wday > 7) te.Wday = 0;
	if (te.Month > 12) te.Month = 0;
	
	// Build up the string
	sprintf(buf,"%s %s %2d, %4d %2d:%02d:%02d", 
		day_strings[te.Wday],
		mon_strings[te.Month],
		te.Day,
		te.Year + 1970,
		te.Hour,
		te.Minute,
		te.Second);
}


/**
 * Load buf with a time & date string for Caller ID
 *
 *   "MMDDHHMM"
 *
 * buf must be at least 9 characters long (to include null termination)
 */
void time_get_cid_string(tmElements_t te, char* buf)
{
	sprintf(buf, "%02d%02d%02d%02d", te.Month, te.Day, te.Hour, te.Minute);
}

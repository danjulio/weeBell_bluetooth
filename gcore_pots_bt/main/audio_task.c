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
#include <string.h>
#include "audio_hal.h"
#include "audio_task.h"
#include "gui_task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "driver/i2s.h"
#include "dc_restore.h"
#include "echo.h"
#include "gain.h"
#include "ps.h"
#include "sample.h"
#include "sys_common.h"


//
// Local constants
//

// SAMPLE RATE
#define AUDIO_SAMPLE_RATE 8000

// Uncomment to enable TX path high-pass filter (remove low frequency components that 
// cause the hybrid to operate in a non-linear fashion from http://www.rowetel.com/?p=33)
//#define ENABLE_ECHO_TX_HPF

// Number of samples in our circular buffers
#define BUF_SAMPLES 960

// Number of samples to read/write to the I2S subsystem at a time (bytes = 4x)
//   This should be a multiple of the FreeRTOS schedule increment
#define I2S_SAMPLES (10 * AUDIO_SAMPLE_RATE / 1000)

// Number of samples for the LEC
//   This should be big enough to hold both the line delay and a full I2S_SAMPLE delay
#define LEC_SAMPLES (16 * AUDIO_SAMPLE_RATE / 1000)

// Number of TX sample buffers to store to align TX/RX for LEC_SAMPLES
//   One would think that this should be 2*I2S_SAMPLES but testing shows the timing of the priming
//   requires 3*I2S_SAMPLES.
#define TX_ALIGN_BUFFERS  3
#define TX_ALIGN_BUF_SIZE ((TX_ALIGN_BUFFERS+1)*I2S_SAMPLES)



//
// Variables
//

static const char* TAG = "audio_task";

// State
static bool audio_enabled = false;
static bool audio_mux_to_tone = false;   // True to enable tone API, false to enable Voice API
static bool audio_mute_mic = false;

// Incoming audio circular buffer
static int16_t rx_buf[BUF_SAMPLES];
static int rx_buf_put;
static int rx_buf_pop;
static int rx_buf_count;
static SemaphoreHandle_t rx_buf_mutex;

// Outgoing audio circular buffer
static int16_t tx_buf[BUF_SAMPLES];
static int tx_buf_put;
static int tx_buf_pop;
static int tx_buf_count;
static SemaphoreHandle_t tx_buf_mutex;

// I2S buffers (2 entries per sample for both channels)
static int16_t i2s_rx_buf[2*I2S_SAMPLES];
static int16_t i2s_tx_buf[2*I2S_SAMPLES];

// TX alignment circular queue (for echo cancellation)
static int16_t i2s_tx_align_buf[TX_ALIGN_BUF_SIZE];
static int i2s_tx_buf_push;
static int i2s_tx_buf_pop;

// DC restore state
static dc_restore_state_t dc_restore_state;

// Echo cancel state
static echo_can_state_t *echo_can_state;

// I2S event queue
static QueueHandle_t i2s_event_queue;

// Sampling rate conversion
static bool ext_sr_16k = false;               // Sampling rate of data in the audio circular buffers
static uint16_t resample_buf[2*I2S_SAMPLES];  // Used by _audioGetTx/_audioPutRx when resampling data to minimize time lock held
static int16_t us_taps[6];                    // FIFO of samples used in upscaling filter
static const int32_t coef_a = 38400;
static const int32_t coef_b = -6400;
static const int32_t coef_c = 768;



//
// Forward declarations
//
static void _audioInitI2S();
static bool _audioInitCodec();
static void _audioInitBuffers();
static void _audioInitTxAlign();
static void _audioHandleNotifications();
static int _audioGetRx(int16_t* buf, int len);
static void _audioPutTx(int16_t* buf, int len);
static void _audioGetTx(int len, int16_t* i2s_txP);
static void _audioPutRx(int len, int16_t* i2s_rxP);
static void _audioPushTxAlign(int len, int16_t* txP);
static int16_t _audioGetTxAlign();
static __inline__ int16_t _audioDsFilter(int16_t s1, int16_t s2);
static __inline__ int16_t _audioUsFilter(int16_t i3, int16_t* i);

//
// API
//
void audio_task(void* args)
{
	int i;
	int16_t tx, rx;
	size_t bytes_written;
	size_t bytes_read;
	i2s_event_t i2s_evt;
	
	
	ESP_LOGI(TAG, "Start task");
	
	// Create circular buffer access mutex
	rx_buf_mutex = xSemaphoreCreateMutex();
	tx_buf_mutex = xSemaphoreCreateMutex();
	
	// configure i2s
	_audioInitI2S();
    
    // configure codec
    if (_audioInitCodec()) {
    	ESP_LOGI(TAG, "Codec initialized");
    } else {
    	ESP_LOGE(TAG, "Codec init failed");
    	gui_set_fatal_error("Codec init failed");
    	vTaskDelete(NULL);
    }
    (void) i2s_stop(I2S_NUM_0);
    
    // Line Echo Cancellation
    echo_can_state = echo_can_create(LEC_SAMPLES, ECHO_CAN_USE_ADAPTION | ECHO_CAN_USE_NLP | ECHO_CAN_USE_CLIP /*| ECHO_CAN_USE_RX_HPF*/);
    
    while (true) {
    	if (!audio_enabled) {
    		// Do nothing but wait to be enabled
    		_audioHandleNotifications();
    		vTaskDelay(pdMS_TO_TICKS(10));
    	} else {
    		// Reset the echo canceller
    		echo_can_flush(echo_can_state);
    		
    		// Start the codec
    		(void) audio_hal_ctrl_codec(AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);
    		
			// Prime TX by loading both DMA buffers
			_audioInitBuffers();
			_audioInitTxAlign();
			_audioGetTx(I2S_SAMPLES, i2s_tx_buf);
			(void) i2s_start(I2S_NUM_0);
			(void) i2s_write(I2S_NUM_0, (void*) i2s_tx_buf, I2S_SAMPLES * 4, &bytes_written, portMAX_DELAY);
			(void) i2s_write(I2S_NUM_0, (void*) i2s_tx_buf, I2S_SAMPLES * 4, &bytes_written, portMAX_DELAY);
	
			// Preset the alignment buffer push index to account for the TX priming
			i2s_tx_buf_push = TX_ALIGN_BUFFERS*I2S_SAMPLES;
    	
		   	while (audio_enabled) {
		   		while (xQueueReceive(i2s_event_queue, &i2s_evt, 0) && audio_enabled) {
					if (i2s_evt.type == I2S_EVENT_TX_DONE) {
				    	_audioGetTx(I2S_SAMPLES, i2s_tx_buf);
				    	(void) i2s_write(I2S_NUM_0, (void*) i2s_tx_buf, I2S_SAMPLES * 4, &bytes_written, portMAX_DELAY);
			    		_audioPushTxAlign(I2S_SAMPLES, i2s_tx_buf);
			    	} else if (i2s_evt.type == I2S_EVENT_RX_DONE) {
				    	(void) i2s_read(I2S_NUM_0, (void*) i2s_rx_buf, I2S_SAMPLES * 4, &bytes_read, portMAX_DELAY);
				
						// Process audio - only worry about channel 1
						if (audio_mux_to_tone) {
							// DC restoration to remove any DC offsets because they may interfere with DTMF detection
							for (i=0; i<(bytes_read/2); i+=2) {
								i2s_rx_buf[i] = dc_restore(&dc_restore_state, i2s_rx_buf[i]);
							}
						} else {
							// Echo cancellation for voice
					    	for (i=0; i<(bytes_read/2); i+=2) {
					    		rx = i2s_rx_buf[i] * -1;  // AG1171 echoed output is inverted so we invert it again
					    		tx = _audioGetTxAlign();
					    		i2s_rx_buf[i] = echo_can_update(echo_can_state, tx, rx);
#if (CONFIG_AUDIO_SAMPLE_ENABLE == true)
					    		sample_record(tx, rx, i2s_rx_buf[i]);
#endif
					    	}
				    	}
				    	
				    	// Store rx data (number of samples = 1/4 bytes read)
				    	_audioPutRx(bytes_read/4, i2s_rx_buf);
			    	} else if (i2s_evt.type == I2S_EVENT_TX_Q_OVF) {
			    		ESP_LOGE(TAG, "I2S TX UNFL");
			    	} else if (i2s_evt.type == I2S_EVENT_RX_Q_OVF) {
			    		ESP_LOGE(TAG, "I2S RX OVFL");
			    	} else if (i2s_evt.type == I2S_EVENT_DMA_ERROR) {
			    		ESP_LOGE(TAG, "I2S DMA ERROR");
			    	}
			    	
			    	_audioHandleNotifications();
		   		}
				
				vTaskDelay(pdMS_TO_TICKS(5));
			}
			
			(void) i2s_stop(I2S_NUM_0);
			(void) audio_hal_ctrl_codec(AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_STOP);
		}
	}
}


int audioGetTxCount()
{
	int n;
	
	xSemaphoreTake(tx_buf_mutex, portMAX_DELAY);
	n = tx_buf_count;
	xSemaphoreGive(tx_buf_mutex);
	
	return n;
}


int audioGetRxCount()
{
	int n;
	
	xSemaphoreTake(rx_buf_mutex, portMAX_DELAY);
	n = rx_buf_count;
	xSemaphoreGive(rx_buf_mutex);
	
	return n;
}


int audioGetToneRx(int16_t* buf, int len)
{
	if (audio_enabled && audio_mux_to_tone) {
		return _audioGetRx(buf, len);
	} else {
		for (int i=0; i<len; i++) buf[i] = 0;
		return 0;
	}
}


void audioPutToneTx(int16_t* buf, int len)
{
	if (audio_enabled && audio_mux_to_tone) {
		_audioPutTx(buf, len);
	}
}


int audioGetVoiceRx(int16_t* buf, int len)
{
	if (audio_enabled && !audio_mux_to_tone && !audio_mute_mic) {
		return _audioGetRx(buf, len);
	} else {
		for (int i=0; i<len; i++) buf[i] = 0;
		return 0;
	}
}


void audioPutVoiceTx(int16_t* buf, int len)
{
	if (audio_enabled && !audio_mux_to_tone) {
		_audioPutTx(buf, len);
	}
}



//
// Internal functions
//
static void _audioInitI2S()
{
	i2s_config_t i2s_config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX,
        .sample_rate =  AUDIO_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL2 | ESP_INTR_FLAG_IRAM,
		.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .dma_buf_count = 2,
        .dma_buf_len = I2S_SAMPLES,
        .use_apll = 1,
        .tx_desc_auto_clear = 1,
        .mclk_multiple = 256
	};
	i2s_pin_config_t i2s_pin_config = {
		.bck_io_num = GPIO_NUM_25,
		.ws_io_num = GPIO_NUM_19,
		.data_out_num = GPIO_NUM_26,
		.data_in_num = GPIO_NUM_34
	};
	
    // install i2s driver
    i2s_driver_install(I2S_NUM_0, &i2s_config, 8, &i2s_event_queue);
    i2s_set_pin(I2S_NUM_0, &i2s_pin_config);
}


static bool _audioInitCodec()
{
	float g;
	
	audio_hal_codec_config_t codec_config = AUDIO_HAL_ES8388_DEFAULT();
	
	if (!audio_hal_init(&codec_config, AUDIO_CODEC_ES8388)) {
		return false;
	}
	
	// Set initial volume
	g = ps_get_gain(PS_GAIN_MIC);
	if (!gainSetCodec(GAIN_TYPE_MIC, g)) {
		return false;
	}
	
	g = ps_get_gain(PS_GAIN_SPK);
	if (!gainSetCodec(GAIN_TYPE_SPK, g)) {
		return false;
	}
	
	return true;
}


static void _audioInitBuffers()
{
	rx_buf_put = 0;
	rx_buf_pop = 0;
	rx_buf_count = 0;
	tx_buf_put = 0;
	tx_buf_pop = 0;
	tx_buf_count = 0;
}


static void _audioInitTxAlign()
{
	for (int i=0; i<TX_ALIGN_BUF_SIZE; i++) {
		i2s_tx_align_buf[i] = 0;
	}
	i2s_tx_buf_push = 0;
	i2s_tx_buf_pop = 0;
}


static void _audioHandleNotifications()
{
	uint32_t notification_value = 0;
	
	// Handle notifications (clear them upon reading)
	if (xTaskNotifyWait(0x00, 0xFFFFFFFF, &notification_value, 0)) {
		if (Notification(notification_value, AUDIO_NOTIFY_DISABLE_MASK)) {
			ESP_LOGI(TAG, "Disable stream");
			audio_enabled = false;
		}
		
		if (Notification(notification_value, AUDIO_NOTIFY_EN_TONE_MASK)) {
			ESP_LOGI(TAG, "Enable Tone stream (8k)");
			audio_enabled = true;
			audio_mux_to_tone = true;
			ext_sr_16k = false;           // Tones always 8k
			
			// Initialize zero DC restoration machine
			dc_restore_init(&dc_restore_state);
		}
		
		if (Notification(notification_value, AUDIO_NOTIFY_EN_VOICE_8_MASK)) {
			ESP_LOGI(TAG, "Enable Voice stream (8k)");
			audio_enabled = true;
			audio_mux_to_tone = false;
			ext_sr_16k = false;
		}
		
		if (Notification(notification_value, AUDIO_NOTIFY_EN_VOICE_16_MASK)) {
			ESP_LOGI(TAG, "Enable Voice stream (16k)");
			audio_enabled = true;
			audio_mux_to_tone = false;
			ext_sr_16k = true;
			
			// Reset the 2X upsample filter
			for (int i=0; i<6; i++) us_taps[i] = 0;
		}
		
		if (Notification(notification_value, AUDIO_NOTIFY_MUTE_MIC_MASK)) {
			audio_mute_mic = true;
		}
		
		if (Notification(notification_value, AUDIO_NOTIFY_UNMUTE_MIC_MASK)) {
			audio_mute_mic = false;
		}
	}
}


static int _audioGetRx(int16_t* buf, int len)
{
	int i;
	int read_len;
	
	xSemaphoreTake(rx_buf_mutex, portMAX_DELAY);
	read_len = (len > rx_buf_count) ? rx_buf_count : len;
	
	for (i=0; i<read_len; i++) {
		*(buf+i) = rx_buf[rx_buf_pop++];
		if (rx_buf_pop >= BUF_SAMPLES) rx_buf_pop = 0;
	}
	
	rx_buf_count -= read_len;
	xSemaphoreGive(rx_buf_mutex);
	
	// Fill remaining with zero if necessary
	if (len > read_len) {
		for (i=read_len; i<len; i++) {
			*(buf+i) = 0;
		}
	}
	
	return read_len;
}


static void _audioPutTx(int16_t* buf, int len)
{
	xSemaphoreTake(tx_buf_mutex, portMAX_DELAY);
	for (int i=0; i<len; i++) {
		tx_buf[tx_buf_put++] = *(buf+i);
		if (tx_buf_put >= BUF_SAMPLES) tx_buf_put = 0;
	}
	
	tx_buf_count += len;
	if (tx_buf_count > BUF_SAMPLES) tx_buf_count = tx_buf_count % BUF_SAMPLES;
	xSemaphoreGive(tx_buf_mutex);
}


// Returns 2x sample data (L/R for 2-channel codec stream), handles 16k -> 8k conversion
// and TX HPF filtering if necessary
static void _audioGetTx(int len, int16_t* i2s_txP)
{
	int i;
	int read_len;
	int16_t t1, t2;
	
	// Quickly get the data out of the circular buffer.  We want to minimize time access
	// to it is blocked because the other end may be incredibly constrained in time to
	// load it (e.g. I saw nasty crashes if the Bluedroid task was held up for any time).
	xSemaphoreTake(tx_buf_mutex, portMAX_DELAY);
	if (ext_sr_16k) {
		read_len = (2*len > tx_buf_count) ? tx_buf_count : 2*len;
	} else {
		read_len = (len > tx_buf_count) ? tx_buf_count : len;
	}
	
	for (i=0; i<read_len; i++) {
		resample_buf[i] = tx_buf[tx_buf_pop++];
		if (tx_buf_pop >= BUF_SAMPLES) tx_buf_pop = 0;
	}
	
	tx_buf_count -= read_len;
	xSemaphoreGive(tx_buf_mutex);
	
	// Process the data
	if (ext_sr_16k) {
		// 2X Downsample:
		//   1. Apply a slight (but fast) low-pass filter
		//   2. Take every other sample
		//
		// Bytes 0 1    2 3     4 5     6 7     8 9    10 11    12 13   14 15     [16 bytes -> 8 samples]
		// Filt       0      1       2      3       4        5        6     
		// Sav        0              1              2                 3           [4 samples]
		for (i=0; i<read_len; i+=2) {
			t1 = resample_buf[i];
			t2 = resample_buf[i+1];
			t1 = _audioDsFilter(t1, t2);
#ifdef ENABLE_ECHO_TX_HPF
			t1 = echo_can_hpf_tx(echo_can_state, t1);
#endif
			*i2s_txP++ = t1;    // Channel 1
			*i2s_txP++ = t1;    // Channel 2
		}
		
		// Fill remaining with zero if necessary
		for (i=read_len/2; i<len; i++) {
			*i2s_txP++ = 0;
			*i2s_txP++ = 0;
		}
	} else {
		for (i=0; i<read_len; i++) {
			t1 = resample_buf[i];
#ifdef ENABLE_ECHO_TX_HPF
			t1 = echo_can_hpf_tx(echo_can_state, t1);
#endif
			*i2s_txP++ = t1;    // Channel 1
			*i2s_txP++ = t1;    // Channel 2
		}
		
		// Fill remaining with zero if necessary
		for (i=read_len; i<len; i++) {
			*i2s_txP++ = 0;
			*i2s_txP++ = 0;
		}
	}
}


static void _audioPutRx(int len, int16_t* i2s_rxP)
{
	int i;
	int actual_len;
	int16_t x;             // Original [previous] sample from filter tap
	int16_t f;             // Filter generated 2x sample (replacing the stuffed 0)
	
	// Process the data
	if (ext_sr_16k) {
		// 2X Upsample using zero-stuffing:
		//   - Insert 0's between original samples
		//   - Low pass filter the 0's using surrounding samples using a half-band filter algorithm
		//     (this function is always 3 samples behind called value - x contains the historical sample)
		actual_len = 2 * len;
		for (i=0; i<actual_len; i += 2) {
			f = _audioUsFilter((int16_t) *i2s_rxP, &x);
			i2s_rxP += 2;                   // Skip other channel
			resample_buf[i] = x;
			resample_buf[i+1] = f;
		}
	} else {
		actual_len = len;
		for (i=0; i<actual_len; i++) {
			resample_buf[i] = *i2s_rxP;
			i2s_rxP += 2;                   // Skip other channel
		}
	}
	
	// Finally, quickly load the processed data
	xSemaphoreTake(rx_buf_mutex, portMAX_DELAY);
	for (i=0; i<actual_len; i++) {
		rx_buf[rx_buf_put++] = resample_buf[i];
		if (rx_buf_put >= BUF_SAMPLES) rx_buf_put = 0;
	}
	
	rx_buf_count += actual_len;
	if (rx_buf_count > BUF_SAMPLES) rx_buf_count = rx_buf_count % BUF_SAMPLES;
	xSemaphoreGive(rx_buf_mutex);
}


static void _audioPushTxAlign(int len, int16_t* txP)
{
	while (len--) {
		i2s_tx_align_buf[i2s_tx_buf_push++] = *txP;
		txP += 2;
		if (i2s_tx_buf_push == TX_ALIGN_BUF_SIZE) i2s_tx_buf_push = 0;
	}
}


static int16_t _audioGetTxAlign()
{
	int16_t t;
	
	t = i2s_tx_align_buf[i2s_tx_buf_pop++];
	if (i2s_tx_buf_pop == TX_ALIGN_BUF_SIZE) i2s_tx_buf_pop = 0;
	
	return t;
}


// Implement a very simple averaging filter
//   From https://dobrian.github.io/cmp/topics/filters/lowpassfilter.html
static __inline__ int16_t _audioDsFilter(int16_t s1, int16_t s2)
{
	int32_t t = s1 + s2;
	return (t/2);
}


// Implement a half-band weighted averaging filter for a zero-stuffing 2x upsampling algorithm
//   From https://hydrogenaud.io/index.php?topic=48889.0 & https://hydrogenaud.io/index.php?topic=46157.0
//
// for each of the 0 entries, f(index) = 
//   a * (X[index-0] + X[index+1]) +
//   b * (x[index-1] + X[index+2]) +
//   c * (X[index-2] + X[index+3])
//
// Input s sample is actually the [index+3] sample
// Returns filtered value for [index] sample's subsequent 0, *i set to [index] sample
static __inline__ int16_t _audioUsFilter(int16_t i3, int16_t* i)
{
	int32_t t;
	
	us_taps[5] = i3;   // Push sample at the end of the tap list
	*i = us_taps[2];   // Value at index
	
	t = coef_a * (us_taps[2] + us_taps[3]);
	t += coef_b * (us_taps[1] + us_taps[4]);
	t += coef_c * (us_taps[0] + us_taps[5]);
	
	us_taps[0] = us_taps[1];
	us_taps[1] = us_taps[2];
	us_taps[2] = us_taps[3];
	us_taps[3] = us_taps[4];
	us_taps[4] = us_taps[5];
	
	return ((t + 32768) / 65536);
}
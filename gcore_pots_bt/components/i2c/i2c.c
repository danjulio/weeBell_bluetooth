/*
 * I2C Module
 *
 * Provides I2C Access routines for other modules/tasks.  Provides a locking mechanism
 * since the underlying ESP IDF routines are not thread safe.
 *
 * Copyright 2020-2021 Dan Julio
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
#include "i2c.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

//
// Constants
//
static const int i2c_master_port = 1;



//
// I2C variables
//
static bool is_initialized = false;
static SemaphoreHandle_t i2c_mutex;


//
// I2C API
//

/**
 * i2c master initialization
 */
esp_err_t i2c_master_init()
{
	esp_err_t ret;
    i2c_config_t conf;
    
    if (is_initialized) {
    	return ESP_OK;
    }
    
    if (i2c_mutex == NULL) {
    	i2c_mutex = xSemaphoreCreateMutex();
    }
    
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = I2C_SDA_PIN;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_io_num = I2C_SCL_PIN;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = I2S_BAUDRATE;
    conf.clk_flags = 0;
    
    i2c_param_config(i2c_master_port, &conf);
    
    ret = i2c_driver_install(i2c_master_port, conf.mode,
                             I2C_MASTER_RX_BUF_LEN,
                             I2C_MASTER_TX_BUF_LEN,
                             0);
    
    is_initialized = (ret == ESP_OK);
    
    return ret;
}


/**
 * i2c master de-initialization
 */
esp_err_t i2c_master_deinit()
{
    esp_err_t ret;

    ret = i2c_driver_delete(i2c_master_port);
    is_initialized = false;

    return ret;
}


/**
 * Get initialization status
 */
bool i2c_master_is_initialized()
{
	return is_initialized;
}



/**
 * i2c master lock
 */
void i2c_lock()
{
	if (i2c_mutex != NULL) {
		xSemaphoreTake(i2c_mutex, portMAX_DELAY);
	}
}


/**
 * i2c master unlock
 */
void i2c_unlock()
{
	if (i2c_mutex != NULL) {
		xSemaphoreGive(i2c_mutex);
	}
}


/**
 * Read esp-i2c-slave
 *
 * _______________________________________________________________________________________
 * | start | slave_addr + rd_bit +ack | read n-1 bytes + ack | read 1 byte + nack | stop |
 * --------|--------------------------|----------------------|--------------------|------|
 *
 */
esp_err_t i2c_master_read_slave(uint8_t addr7, uint8_t *data_rd, size_t size)
{
	if (!is_initialized) {
    	return ESP_FAIL;
    }
    if (size == 0) {
        return ESP_OK;
    }
    
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr7 << 1) | I2C_MASTER_READ, ACK_CHECK_EN);
    if (size > 1) {
        i2c_master_read(cmd, data_rd, size - 1, ACK_VAL);
    }
    i2c_master_read_byte(cmd, data_rd + size - 1, NACK_VAL);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(i2c_master_port, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
    
    return ret;
}


/**
 * Write esp-i2c-slave
 *
 * ___________________________________________________________________
 * | start | slave_addr + wr_bit + ack | write n bytes + ack  | stop |
 * --------|---------------------------|----------------------|------|
 *
 */
esp_err_t i2c_master_write_slave(uint8_t addr7, uint8_t *data_wr, size_t size)
{
	if (!is_initialized) {
    	return ESP_FAIL;
    }
    
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr7 << 1) | I2C_MASTER_WRITE, ACK_CHECK_EN);
    i2c_master_write(cmd, data_wr, size, ACK_CHECK_EN);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(i2c_master_port, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
    
    return ret;
}

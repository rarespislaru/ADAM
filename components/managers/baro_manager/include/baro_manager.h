#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "bmp280_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

// BMP280 manager data sample
typedef struct {
	float temperature_c;
	float pressure_pa;
	float altitude_m;
} bmp280_manager_data_t;

// BMP280 manager config
typedef struct {
	bmp280_hal_config_t hal;
	float				sea_pressure;	// Pa
	uint32_t			sample_period;	// Sampling rate (mS)
} bmp280_manager_config_t;

// BMP280 manager internal struct (opaque handle)
typedef struct bmp280_manager bmp280_manager;
typedef bmp280_manager *bmp280_manager_handle_t;

// Default config
#define BMP280_MANAGER_CONFIG_DEFAULT(bus_handle) {     \
	.hal = BMP280_HAL_CONFIG_DEFAULT(bus_handle),       \
	.sea_pressure = 101325.0f,                          \
	.sample_period = 100,                               \
}

// Initialize the BMP280 manager
esp_err_t bmp280_manager_create(const bmp280_manager_config_t *cfg, bmp280_manager_handle_t *out);

// Destroy the BMP280 manager
esp_err_t bmp280_manager_destroy(bmp280_manager_handle_t mgr);

// Get the data blocking
esp_err_t baro_get_data(bmp280_manager_handle_t mgr, bmp280_manager_data_t *out);

// Get the last cached data non-blocking
esp_err_t baro_get_data_last(bmp280_manager_handle_t mgr, bmp280_manager_data_t *out);

// Get the altitude blocking
esp_err_t baro_get_altitude(bmp280_manager_handle_t mgr, float *out);

// Get the last cached altitude non-blocking
esp_err_t baro_get_altitude_last(bmp280_manager_handle_t mgr, float *out);

// Set the current altitude as ground (zero reference)
esp_err_t baro_set_ground(bmp280_manager_handle_t mgr);

#ifdef __cplusplus
}
#endif
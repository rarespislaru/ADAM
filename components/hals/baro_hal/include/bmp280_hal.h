// BMP280 HAL layer for the EVA board

#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "esp_check.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bmp280_hal *bmp280_hal_handle_t;

// BMP280 oversampling settings
typedef enum {
	BMP280_OSRS_SKIP = 0,
	BMP280_OSRS_X1   = 1,
	BMP280_OSRS_X2   = 2,
	BMP280_OSRS_X4   = 3,
	BMP280_OSRS_X8   = 4,
	BMP280_OSRS_X16  = 5,
} bmp280_osrs_t;

// BMP280 modes
typedef enum {
	BMP280_MODE_SLEEP  = 0,
	BMP280_MODE_FORCED = 1,   // one measurement, then back to sleep
	BMP280_MODE_NORMAL = 3,   // continuous
} bmp280_mode_t;

// BMP280 filters
typedef enum {
	BMP280_FILTER_OFF = 0,
	BMP280_FILTER_2   = 1,
	BMP280_FILTER_4   = 2,
	BMP280_FILTER_8   = 3,
	BMP280_FILTER_16  = 4,
} bmp280_filter_t;

// BMP280 standby time
// Only in normal mode 
typedef enum {
	BMP280_STANDBY_0_5_MS  = 0,
	BMP280_STANDBY_62_5_MS = 1,
	BMP280_STANDBY_125_MS  = 2,
	BMP280_STANDBY_250_MS  = 3,
	BMP280_STANDBY_500_MS  = 4,
	BMP280_STANDBY_1000_MS = 5,
	BMP280_STANDBY_2000_MS = 6,
	BMP280_STANDBY_4000_MS = 7,
} bmp280_standby_t;

// BMP280 main struct
typedef struct {
	i2c_master_bus_handle_t bus;        // created by the app, shared with other sensors
	uint8_t          address;           // 0x76 (SDO low) or 0x77 (SDO high)
	uint32_t         scl_speed_hz;
	bmp280_mode_t    mode;
	bmp280_osrs_t    osrs_temp;
	bmp280_osrs_t    osrs_press;
	bmp280_filter_t  filter;
	bmp280_standby_t standby;
} bmp280_hal_config_t;

// Default config
#define BMP280_HAL_CONFIG_DEFAULT(bus_handle) { \
	.bus          = (bus_handle),               \
	.address      = 0x76,                       \
	.scl_speed_hz = 400000,                     \
	.mode         = BMP280_MODE_NORMAL,         \
	.osrs_temp    = BMP280_OSRS_X2,             \
	.osrs_press   = BMP280_OSRS_X16,            \
	.filter       = BMP280_FILTER_4,            \
	.standby      = BMP280_STANDBY_62_5_MS,     \
}

// BMP280 data structure
typedef struct {
	float temperature_c;
	float pressure_pa;
} bmp280_data_t;

// BMP280 HAL initialization
esp_err_t bmp280_hal_init(const bmp280_hal_config_t *cfg, bmp280_hal_handle_t *out);

// BMP280 HAL destroy
esp_err_t bmp280_hal_destroy(bmp280_hal_handle_t h);

// BMP280 data receive 
esp_err_t bmp280_hal_read(bmp280_hal_handle_t h, bmp280_data_t *out);

// BMP280 resets the chip
esp_err_t bmp280_hal_soft_reset(bmp280_hal_handle_t h);

#ifdef __cplusplus
}
#endif
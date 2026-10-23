#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "baro_manager.h"

// Internal struct definition (opaque to callers)
struct bmp280_manager {
	bmp280_manager_config_t cfg;
	bmp280_hal_handle_t     hal;    // HAL handle for hardware access

	SemaphoreHandle_t       lock;   // Mutex protecting `last`
	bmp280_manager_data_t   *last;  // Cached sample (heap allocated)
	bool                    has_data;
	float                   offset; // Set by baro_set_ground()
};

// Barometric formula: converts pressure (Pa) to altitude (m) above sea level
static float pressure_to_altitude(float pressure_pa, float sea_pressure_pa)
{
	return 44330.0f * (1.0f - powf(pressure_pa / sea_pressure_pa, 1.0f / 5.255f));
}

esp_err_t bmp280_manager_create(const bmp280_manager_config_t *cfg, bmp280_manager_handle_t *out)
{
	if (!cfg || !out) return ESP_ERR_INVALID_ARG;

	struct bmp280_manager *mgr = calloc(1, sizeof(*mgr));
	if (!mgr) return ESP_ERR_NO_MEM;

	mgr->cfg = *cfg;

	esp_err_t ret = bmp280_hal_init(&cfg->hal, &mgr->hal);
	if (ret != ESP_OK) {
		free(mgr);
		return ret;
	}

	mgr->lock = xSemaphoreCreateMutex();
	if (!mgr->lock) {
		bmp280_hal_destroy(mgr->hal);
		free(mgr);
		return ESP_ERR_NO_MEM;
	}

	mgr->last = calloc(1, sizeof(*mgr->last));
	if (!mgr->last) {
		vSemaphoreDelete(mgr->lock);
		bmp280_hal_destroy(mgr->hal);
		free(mgr);
		return ESP_ERR_NO_MEM;
	}

	*out = mgr;
	return ESP_OK;
}

esp_err_t bmp280_manager_destroy(bmp280_manager_handle_t mgr)
{
	if (!mgr) return ESP_ERR_INVALID_ARG;

	if (mgr->lock) vSemaphoreDelete(mgr->lock);
	if (mgr->hal)  bmp280_hal_destroy(mgr->hal);
	free(mgr->last);
	free(mgr);

	return ESP_OK;
}

esp_err_t baro_get_altitude(bmp280_manager_handle_t mgr, float *out)
{
	if (!mgr || !out) return ESP_ERR_INVALID_ARG;

	bmp280_data_t raw;
	esp_err_t ret = bmp280_hal_read(mgr->hal, &raw);
	if (ret != ESP_OK) return ret;

	float alt = pressure_to_altitude(raw.pressure_pa, mgr->cfg.sea_pressure);

	*out = alt - mgr->offset;
	return ESP_OK;
}

esp_err_t baro_get_altitude_last(bmp280_manager_handle_t mgr, float *out)
{
	if (!mgr || !out) return ESP_ERR_INVALID_ARG;

	if (xSemaphoreTake(mgr->lock, pdMS_TO_TICKS(100)) != pdTRUE)
		return ESP_ERR_TIMEOUT;

	if (!mgr->has_data) {
		xSemaphoreGive(mgr->lock);
		return ESP_ERR_INVALID_STATE;
	}

	*out = mgr->last->altitude_m - mgr->offset;

	xSemaphoreGive(mgr->lock);
	return ESP_OK;
}

esp_err_t baro_set_ground(bmp280_manager_handle_t mgr)
{
	if (!mgr) return ESP_ERR_INVALID_ARG;

	bmp280_data_t raw;
	esp_err_t ret = bmp280_hal_read(mgr->hal, &raw);
	if (ret != ESP_OK) return ret;

	mgr->offset = pressure_to_altitude(raw.pressure_pa, mgr->cfg.sea_pressure);

	return ESP_OK;
}
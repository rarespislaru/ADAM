#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "uart_manager.h"
#include "debug.h"
#include "bmp280_hal.h"
#include <math.h>

static const char *TAG = "usb_serial_test";

float altitude(float p, float p0) {
    return 44330.0f * (1.0f - powf(p / p0, 0.1903f));
}

void app_main(void)
{
	/* UART (also the debug channel) */
	uart_manager_config_t cfg = UART_MANAGER_CONFIG_DEFAULT(UART_NUM_0);
	cfg.hal.baud_rate = 115200;
	cfg.rx_cb = NULL;

	uart_manager_handle_t uart = NULL;
	ESP_ERROR_CHECK(uart_manager_create(&cfg, &uart));
	ESP_ERROR_CHECK(uart_manager_start(uart));
	debug_init(uart);

	/* I2C bus: the BMP280 HAL borrows it */
	i2c_master_bus_config_t bus_cfg = {
		.i2c_port          = I2C_NUM_0,
		.sda_io_num        = GPIO_NUM_21,       /* change to your wiring */
		.scl_io_num        = GPIO_NUM_22,
		.clk_source        = I2C_CLK_SRC_DEFAULT,
		.glitch_ignore_cnt = 7,
		.flags.enable_internal_pullup = true,
	};
	i2c_master_bus_handle_t bus = NULL;
	ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

	/* BMP280: its own config and handle */
	bmp280_hal_config_t bmp280_cfg = BMP280_HAL_CONFIG_DEFAULT(bus);
	bmp280_hal_handle_t bmp280 = NULL;
	ESP_ERROR_CHECK(bmp280_hal_init(&bmp280_cfg, &bmp280));

	ESP_LOGI(TAG, "Starting EVA test");

	bmp280_data_t bmp280_data = {0};
	int counter = 0;

	while (1) {
		esp_err_t err = bmp280_hal_read(bmp280, &bmp280_data);
		if (err == ESP_OK) {
			debug_printf("EVA count %d,H=%f m\r\n",
			             counter, altitude(bmp280_data.pressure_pa, 101325.0f));
		} else {
			debug_printf("EVA count %d, baro read failed: %s\r\n",
			             counter, esp_err_to_name(err));
		}
		counter++;
		vTaskDelay(pdMS_TO_TICKS(250));
	}
}
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "uart_manager.h"
#include "debug.h"
#include "baro_manager.h"

static const char *TAG = "usb_serial_test";

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

	/* I2C bus */
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

	/* Baro manager */
	bmp280_manager_config_t baro_cfg = BMP280_MANAGER_CONFIG_DEFAULT(bus);
	bmp280_manager_handle_t baro = NULL;
	ESP_ERROR_CHECK(bmp280_manager_create(&baro_cfg, &baro));

	ESP_LOGI(TAG, "Starting EVA test");

	int counter = 0;

	while (1) {
		float altitude_m = 0.0f;
		esp_err_t err = baro_get_altitude(baro, &altitude_m);
		if (err == ESP_OK) {
			debug_printf("EVA count %d, H=%f m\r\n", counter, altitude_m);
		} else {
			debug_printf("EVA count %d, baro read failed: %s\r\n",
			             counter, esp_err_to_name(err));
		}
		counter++;
		vTaskDelay(pdMS_TO_TICKS(250));
	}
}
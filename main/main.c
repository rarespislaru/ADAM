/*
 * ESP32 UART test using serial_hal + uart_manager.
 *
 * UART0 is wired to the on-board USB-to-UART bridge, so whatever is
 * written here shows up on the PC:  idf.py -p /dev/ttyUSB0 monitor
 *
 * Periodically sends a counter and echoes back anything received.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "uart_manager.h"

static const char *TAG = "usb_serial_test";

static void on_rx(const uint8_t *data, size_t len, void *ctx)
{
    uart_manager_handle_t mgr = ctx;
    uart_manager_send(mgr, data, len);      /* echo */
}

void app_main(void)
{
    uart_manager_config_t cfg = UART_MANAGER_CONFIG_DEFAULT(UART_NUM_0);
    cfg.hal.baud_rate = 115200;
    cfg.rx_cb = on_rx;

    uart_manager_handle_t uart = NULL;
    ESP_ERROR_CHECK(uart_manager_create(&cfg, &uart));
    uart_manager_set_rx_callback(uart, on_rx, uart);   /* ctx = handle, known only now */
    ESP_ERROR_CHECK(uart_manager_start(uart));

    ESP_LOGI(TAG, "Starting USB (UART0) write test");

    int counter = 0;
    while (1) {
        uart_manager_printf(uart, "Hello from ESP32! count=%d\r\n", counter);
        ESP_LOGI(TAG, "Sent: count=%d", counter);
        counter++;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
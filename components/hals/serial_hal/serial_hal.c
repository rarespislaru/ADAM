#include <stdlib.h>
#include "serial_hal.h"
#include "esp_check.h"

static const char *TAG = "serial_hal";

struct serial_hal {
    uart_port_t  port;
    QueueHandle_t event_queue;
};

esp_err_t serial_hal_init(const serial_hal_config_t *cfg, serial_hal_handle_t *out_handle)
{
    ESP_RETURN_ON_FALSE(cfg && out_handle, ESP_ERR_INVALID_ARG, TAG, "null argument");
    ESP_RETURN_ON_FALSE(cfg->port < UART_NUM_MAX, ESP_ERR_INVALID_ARG, TAG, "bad port");
    ESP_RETURN_ON_FALSE(cfg->rx_buf_size > UART_HW_FIFO_LEN(cfg->port),
                        ESP_ERR_INVALID_ARG, TAG, "rx_buf_size too small");
    ESP_RETURN_ON_FALSE(!uart_is_driver_installed(cfg->port) || cfg->port == UART_NUM_0,
                        ESP_ERR_INVALID_STATE, TAG, "driver already installed on UART%d", cfg->port);

    struct serial_hal *h = calloc(1, sizeof(*h));
    ESP_RETURN_ON_FALSE(h, ESP_ERR_NO_MEM, TAG, "no memory");
    h->port = cfg->port;

    const uart_config_t uc = {
        .baud_rate  = cfg->baud_rate,
        .data_bits  = cfg->data_bits,
        .parity     = cfg->parity,
        .stop_bits  = cfg->stop_bits,
        .flow_ctrl  = cfg->flow_ctrl,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_param_config(cfg->port, &uc);
    if (err != ESP_OK) goto fail;

    err = uart_set_pin(cfg->port, cfg->tx_pin, cfg->rx_pin, cfg->rts_pin, cfg->cts_pin);
    if (err != ESP_OK) goto fail;

    /* UART0 may already have a driver from the console; replace it. */
    if (uart_is_driver_installed(cfg->port)) {
        uart_driver_delete(cfg->port);
    }

    err = uart_driver_install(cfg->port,
                              (int)cfg->rx_buf_size,
                              (int)cfg->tx_buf_size,
                              cfg->event_queue_size,
                              cfg->event_queue_size ? &h->event_queue : NULL,
                              0);
    if (err != ESP_OK) goto fail;

    *out_handle = h;
    return ESP_OK;

fail:
    ESP_LOGE(TAG, "init UART%d failed: %s", cfg->port, esp_err_to_name(err));
    free(h);
    return err;
}

esp_err_t serial_hal_deinit(serial_hal_handle_t h)
{
    ESP_RETURN_ON_FALSE(h, ESP_ERR_INVALID_ARG, TAG, "null handle");
    esp_err_t err = uart_driver_delete(h->port);
    free(h);
    return err;
}

int serial_hal_write(serial_hal_handle_t h, const void *data, size_t len)
{
    if (!h || !data) return -1;
    return uart_write_bytes(h->port, data, len);
}

int serial_hal_read(serial_hal_handle_t h, void *buf, size_t len, uint32_t timeout_ms)
{
    if (!h || !buf) return -1;
    return uart_read_bytes(h->port, buf, len, pdMS_TO_TICKS(timeout_ms));
}

esp_err_t serial_hal_rx_available(serial_hal_handle_t h, size_t *out_len)
{
    ESP_RETURN_ON_FALSE(h && out_len, ESP_ERR_INVALID_ARG, TAG, "null argument");
    return uart_get_buffered_data_len(h->port, out_len);
}

esp_err_t serial_hal_flush_rx(serial_hal_handle_t h)
{
    ESP_RETURN_ON_FALSE(h, ESP_ERR_INVALID_ARG, TAG, "null handle");
    return uart_flush_input(h->port);
}

esp_err_t serial_hal_wait_tx_done(serial_hal_handle_t h, uint32_t timeout_ms)
{
    ESP_RETURN_ON_FALSE(h, ESP_ERR_INVALID_ARG, TAG, "null handle");
    return uart_wait_tx_done(h->port, pdMS_TO_TICKS(timeout_ms));
}

esp_err_t serial_hal_set_baud(serial_hal_handle_t h, int baud_rate)
{
    ESP_RETURN_ON_FALSE(h, ESP_ERR_INVALID_ARG, TAG, "null handle");
    return uart_set_baudrate(h->port, baud_rate);
}

QueueHandle_t serial_hal_event_queue(serial_hal_handle_t h)
{
    return h ? h->event_queue : NULL;
}

uart_port_t serial_hal_port(serial_hal_handle_t h)
{
    return h ? h->port : UART_NUM_MAX;
}
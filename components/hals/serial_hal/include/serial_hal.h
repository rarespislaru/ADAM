#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Thin hardware abstraction over the ESP-IDF UART driver.
 * No tasks, no locking: that is the manager's job.
 */

typedef struct serial_hal *serial_hal_handle_t;

typedef struct {
    uart_port_t         port;
    int                 baud_rate;
    uart_word_length_t  data_bits;
    uart_parity_t       parity;
    uart_stop_bits_t    stop_bits;
    uart_hw_flowcontrol_t flow_ctrl;
    int                 tx_pin;             /* UART_PIN_NO_CHANGE = keep default */
    int                 rx_pin;
    int                 rts_pin;
    int                 cts_pin;
    size_t              rx_buf_size;        /* must be > UART_HW_FIFO_LEN (128) */
    size_t              tx_buf_size;        /* 0 = blocking writes, no TX ring buffer */
    int                 event_queue_size;   /* 0 = no driver event queue */
} serial_hal_config_t;

#define SERIAL_HAL_CONFIG_DEFAULT(port_num) {          \
    .port             = (port_num),                  \
    .baud_rate        = 115200,                      \
    .data_bits        = UART_DATA_8_BITS,            \
    .parity           = UART_PARITY_DISABLE,         \
    .stop_bits        = UART_STOP_BITS_1,            \
    .flow_ctrl        = UART_HW_FLOWCTRL_DISABLE,    \
    .tx_pin           = UART_PIN_NO_CHANGE,          \
    .rx_pin           = UART_PIN_NO_CHANGE,          \
    .rts_pin          = UART_PIN_NO_CHANGE,          \
    .cts_pin          = UART_PIN_NO_CHANGE,          \
    .rx_buf_size      = 1024,                        \
    .tx_buf_size      = 1024,                        \
    .event_queue_size = 0,                           \
}

/** Configure pins/params and install the driver. */
esp_err_t serial_hal_init(const serial_hal_config_t *cfg, serial_hal_handle_t *out_handle);

/** Uninstall the driver and free the handle. */
esp_err_t serial_hal_deinit(serial_hal_handle_t handle);

/** Write bytes. Returns number of bytes queued/written, or negative on error. */
int serial_hal_write(serial_hal_handle_t handle, const void *data, size_t len);

/** Read up to len bytes, waiting up to timeout_ms. Returns bytes read, or negative on error. */
int serial_hal_read(serial_hal_handle_t handle, void *buf, size_t len, uint32_t timeout_ms);

/** Number of bytes waiting in the RX buffer. */
esp_err_t serial_hal_rx_available(serial_hal_handle_t handle, size_t *out_len);

/** Discard everything in the RX buffer. */
esp_err_t serial_hal_flush_rx(serial_hal_handle_t handle);

/** Block until all queued TX data has physically left the UART. */
esp_err_t serial_hal_wait_tx_done(serial_hal_handle_t handle, uint32_t timeout_ms);

/** Change baud rate at runtime. */
esp_err_t serial_hal_set_baud(serial_hal_handle_t handle, int baud_rate);

/** Driver event queue (NULL unless event_queue_size > 0). */
QueueHandle_t serial_hal_event_queue(serial_hal_handle_t handle);

uart_port_t serial_hal_port(serial_hal_handle_t handle);

#ifdef __cplusplus
}
#endif
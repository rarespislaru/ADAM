#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "serial_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * UART manager: owns a HAL instance and adds
 *   - a mutex-protected, thread-safe TX path (send / printf)
 *   - a background RX task that delivers data through a callback
 *
 * One manager per UART port. Create several for several ports.
 */

typedef struct uart_manager *uart_manager_handle_t;

/**
 * Called from the manager's RX task. `data` is only valid during the call.
 * Keep it short, or hand the data off to a queue.
 */
typedef void (*uart_manager_rx_cb_t)(const uint8_t *data, size_t len, void *user_ctx);

typedef struct {
    serial_hal_config_t    hal;
    uart_manager_rx_cb_t rx_cb;             /* may be NULL (TX only) */
    void                *user_ctx;
    size_t               rx_chunk_size;     /* max bytes per callback */
    uint32_t             task_stack_size;   /* bytes */
    unsigned             task_priority;
    int                  task_core;         /* <0 = no affinity */
} uart_manager_config_t;

#define UART_MANAGER_CONFIG_DEFAULT(port_num) {      \
    .hal             = SERIAL_HAL_CONFIG_DEFAULT(port_num), \
    .rx_cb           = NULL,                         \
    .user_ctx        = NULL,                         \
    .rx_chunk_size   = 128,                          \
    .task_stack_size = 3072,                         \
    .task_priority   = 5,                            \
    .task_core       = -1,                           \
}

/** Initialise the UART (via the HAL) and allocate resources. Does not start RX. */
esp_err_t uart_manager_create(const uart_manager_config_t *cfg, uart_manager_handle_t *out);

/** Start the RX task. No-op (ESP_OK) if already running. */
esp_err_t uart_manager_start(uart_manager_handle_t mgr);

/** Stop the RX task and wait for it to exit. */
esp_err_t uart_manager_stop(uart_manager_handle_t mgr);

/** Stop, deinit the UART and free everything. */
esp_err_t uart_manager_destroy(uart_manager_handle_t mgr);

/** Replace the RX callback (call while stopped, or accept a brief race). */
esp_err_t uart_manager_set_rx_callback(uart_manager_handle_t mgr,
                                       uart_manager_rx_cb_t cb, void *user_ctx);

/** Thread-safe write. ESP_OK only if all bytes were accepted. */
esp_err_t uart_manager_send(uart_manager_handle_t mgr, const void *data, size_t len);

/** Thread-safe printf-style write. Returns bytes written or negative on error. */
int uart_manager_printf(uart_manager_handle_t mgr, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/** Direct access to the underlying HAL (e.g. to change the baud rate). */
serial_hal_handle_t uart_manager_hal(uart_manager_handle_t mgr);

#ifdef __cplusplus
}
#endif
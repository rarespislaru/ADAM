#include <stdio.h>
#include <stdlib.h>
#include "uart_manager.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "uart_mgr";

#define TX_LOCK_TIMEOUT_MS   1000
#define RX_POLL_TIMEOUT_MS   100
#define PRINTF_STACK_BUF     128

struct uart_manager {
    uart_manager_config_t cfg;
    serial_hal_handle_t     hal;
    SemaphoreHandle_t     tx_lock;
    SemaphoreHandle_t     task_done;
    TaskHandle_t          task;
    uint8_t              *rx_buf;
    volatile bool         running;
};

static void rx_task(void *arg)
{
    struct uart_manager *m = arg;

    while (m->running) {
        int n = serial_hal_read(m->hal, m->rx_buf, m->cfg.rx_chunk_size, RX_POLL_TIMEOUT_MS);
        if (n > 0 && m->cfg.rx_cb) {
            m->cfg.rx_cb(m->rx_buf, (size_t)n, m->cfg.user_ctx);
        } else if (n < 0) {
            ESP_LOGE(TAG, "read error, backing off");
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    xSemaphoreGive(m->task_done);
    vTaskDelete(NULL);
}

esp_err_t uart_manager_create(const uart_manager_config_t *cfg, uart_manager_handle_t *out)
{
    ESP_RETURN_ON_FALSE(cfg && out, ESP_ERR_INVALID_ARG, TAG, "null argument");
    ESP_RETURN_ON_FALSE(cfg->rx_chunk_size > 0, ESP_ERR_INVALID_ARG, TAG, "rx_chunk_size is 0");

    struct uart_manager *m = calloc(1, sizeof(*m));
    ESP_RETURN_ON_FALSE(m, ESP_ERR_NO_MEM, TAG, "no memory");
    m->cfg = *cfg;

    m->tx_lock   = xSemaphoreCreateMutex();
    m->task_done = xSemaphoreCreateBinary();
    if (!m->tx_lock || !m->task_done) {
        uart_manager_destroy(m);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = serial_hal_init(&m->cfg.hal, &m->hal);
    if (err != ESP_OK) {
        uart_manager_destroy(m);
        return err;
    }

    *out = m;
    return ESP_OK;
}

esp_err_t uart_manager_start(uart_manager_handle_t m)
{
    ESP_RETURN_ON_FALSE(m, ESP_ERR_INVALID_ARG, TAG, "null handle");
    if (m->running) return ESP_OK;

    m->rx_buf = malloc(m->cfg.rx_chunk_size);
    ESP_RETURN_ON_FALSE(m->rx_buf, ESP_ERR_NO_MEM, TAG, "no memory for rx buffer");

    m->running = true;
    BaseType_t ok;
    if (m->cfg.task_core < 0) {
        ok = xTaskCreate(rx_task, "uart_rx", m->cfg.task_stack_size, m,
                         m->cfg.task_priority, &m->task);
    } else {
        ok = xTaskCreatePinnedToCore(rx_task, "uart_rx", m->cfg.task_stack_size, m,
                                     m->cfg.task_priority, &m->task, m->cfg.task_core);
    }
    if (ok != pdPASS) {
        m->running = false;
        free(m->rx_buf);
        m->rx_buf = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t uart_manager_stop(uart_manager_handle_t m)
{
    ESP_RETURN_ON_FALSE(m, ESP_ERR_INVALID_ARG, TAG, "null handle");
    if (!m->running) return ESP_OK;

    m->running = false;
    xSemaphoreTake(m->task_done, portMAX_DELAY);   /* task exits within RX_POLL_TIMEOUT_MS */
    m->task = NULL;
    free(m->rx_buf);
    m->rx_buf = NULL;
    return ESP_OK;
}

esp_err_t uart_manager_destroy(uart_manager_handle_t m)
{
    ESP_RETURN_ON_FALSE(m, ESP_ERR_INVALID_ARG, TAG, "null handle");

    uart_manager_stop(m);
    if (m->hal)       serial_hal_deinit(m->hal);
    if (m->tx_lock)   vSemaphoreDelete(m->tx_lock);
    if (m->task_done) vSemaphoreDelete(m->task_done);
    free(m);
    return ESP_OK;
}

esp_err_t uart_manager_set_rx_callback(uart_manager_handle_t m,
                                       uart_manager_rx_cb_t cb, void *user_ctx)
{
    ESP_RETURN_ON_FALSE(m, ESP_ERR_INVALID_ARG, TAG, "null handle");
    m->cfg.user_ctx = user_ctx;
    m->cfg.rx_cb    = cb;
    return ESP_OK;
}

esp_err_t uart_manager_send(uart_manager_handle_t m, const void *data, size_t len)
{
    ESP_RETURN_ON_FALSE(m && data, ESP_ERR_INVALID_ARG, TAG, "null argument");
    if (len == 0) return ESP_OK;

    if (xSemaphoreTake(m->tx_lock, pdMS_TO_TICKS(TX_LOCK_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    int n = serial_hal_write(m->hal, data, len);
    xSemaphoreGive(m->tx_lock);

    return (n == (int)len) ? ESP_OK : ESP_FAIL;
}

int uart_manager_printf(uart_manager_handle_t m, const char *fmt, ...)
{
    if (!m || !fmt) return -1;

    char stack_buf[PRINTF_STACK_BUF];
    char *buf = stack_buf;

    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int len = vsnprintf(stack_buf, sizeof(stack_buf), fmt, ap);
    va_end(ap);

    if (len < 0) {
        va_end(ap2);
        return -1;
    }

    if ((size_t)len >= sizeof(stack_buf)) {          /* didn't fit: use heap */
        buf = malloc((size_t)len + 1);
        if (!buf) {
            va_end(ap2);
            return -1;
        }
        vsnprintf(buf, (size_t)len + 1, fmt, ap2);
    }
    va_end(ap2);

    esp_err_t err = uart_manager_send(m, buf, (size_t)len);
    if (buf != stack_buf) free(buf);

    return (err == ESP_OK) ? len : -1;
}

serial_hal_handle_t uart_manager_hal(uart_manager_handle_t m)
{
    return m ? m->hal : NULL;
}
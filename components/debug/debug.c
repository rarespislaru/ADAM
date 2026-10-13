#include <stdarg.h>
#include <stdio.h>
#include "debug.h"

static uart_manager_handle_t s_mgr;

void debug_init(uart_manager_handle_t mgr) {
    s_mgr = mgr;
}

int debug_printf(const char *fmt, ...)
{
    if (!s_mgr) return -1;

    char buf[128];

    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n < 0) return -1;

    if (n >= (int)sizeof(buf)) n = sizeof(buf) - 1;   /* truncated */

    // Sending Debugging data via the uart and checking if it failed
    return uart_manager_send(s_mgr, buf, n) == ESP_OK ? n : -1;
}
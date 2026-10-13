#pragma once
#include "uart_manager.h"

// Requires an uart_manager_handle_t
void debug_init(uart_manager_handle_t mgr);

// Prints to usb uart all data
// It has a 128 buffer and can is limited like this 
int  debug_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#ifdef CONFIG_DEBUG_ENABLED
#define DBG(fmt, ...) debug_printf("[%s] " fmt "\r\n", __func__, ##__VA_ARGS__)
#else
#define DBG(fmt, ...) ((void)0)
#endif
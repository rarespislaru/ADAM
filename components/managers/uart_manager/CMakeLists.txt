idf_component_register(SRCS "uart_manager.c"
                       INCLUDE_DIRS "include"
                       REQUIRES serial_hal
                       PRIV_REQUIRES freertos log)
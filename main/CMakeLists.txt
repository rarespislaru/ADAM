idf_component_register(SRCS "main.c"
                        INCLUDE_DIRS "."
                        REQUIRES baro_manager uart_manager debug freertos)
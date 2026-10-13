idf_component_register(SRCS "main.c"
                        INCLUDE_DIRS "."
                        REQUIRES baro_hal debug freertos)
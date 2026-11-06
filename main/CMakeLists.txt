# Select the source file with app_main
set(MAIN_SRC "main.c")

idf_component_register(SRCS "${MAIN_SRC}"
                    INCLUDE_DIRS "."
                    REQUIRES esp_driver_gpio esp_driver_ledc esp_driver_pcnt log freertos)

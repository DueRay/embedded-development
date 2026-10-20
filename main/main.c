#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PIN 5

void app_main ()
{
    gpio_reset_pin(PIN);
    gpio_set_direction(PIN, GPIO_MODE_OUTPUT);

    while (1) {
        gpio_set_level(PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(500));
        gpio_set_level(PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

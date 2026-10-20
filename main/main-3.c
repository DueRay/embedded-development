#include <stdbool.h>
#include <stdio.h>
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BUTTON_PIN 5

#define DEBOUNCE_TIME_MS 50

static volatile bool button_event = false;

static void IRAM_ATTR on_button_press(void *arg)
{
    button_event = true;
}

void app_main ()
{
    gpio_reset_pin(BUTTON_PIN);
    gpio_set_direction(BUTTON_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_PIN, GPIO_PULLUP_ONLY);
    gpio_set_intr_type(BUTTON_PIN, GPIO_INTR_NEGEDGE);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_PIN, on_button_press, NULL);

    bool checking = false;
    int64_t event_millis = 0;
    int press_counter = 0;

    while (1) {
        if (button_event) {
            button_event = false;
            event_millis = esp_timer_get_time() / 1000;
            checking = true;
        }

        if (checking && esp_timer_get_time() / 1000 - event_millis >= DEBOUNCE_TIME_MS) {
            checking = false;

            if (gpio_get_level(BUTTON_PIN) == 0) {
                press_counter = press_counter + 1;
                printf("presses: %d\n", press_counter);
            }
        }

        vTaskDelay(1);
    }
}

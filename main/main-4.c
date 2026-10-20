#include <stdbool.h>
#include <stdio.h>
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BUTTON_PIN 5

#define POLL_INTERVAL_MS 5
#define STABLE_SAMPLES 4

typedef enum {
    RELEASED,
    MAYBE_PRESSED,
    PRESSED,
    MAYBE_RELEASED
} button_state_t;

void app_main ()
{
    gpio_reset_pin(BUTTON_PIN);
    gpio_set_direction(BUTTON_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_PIN, GPIO_PULLUP_ONLY);

    button_state_t state = RELEASED;
    int stable_samples = 0;
    int64_t last_poll_millis = 0;
    int press_counter = 0;

    while (1) {
        int64_t now = esp_timer_get_time() / 1000;

        if (now - last_poll_millis >= POLL_INTERVAL_MS) {
            last_poll_millis = now;
            bool low = gpio_get_level(BUTTON_PIN) == 0;

            switch (state) {
                case RELEASED:
                    if (low) {
                        state = MAYBE_PRESSED;
                        stable_samples = 1;
                    }
                    break;

                case MAYBE_PRESSED:
                    if (!low) {
                        state = RELEASED;
                    } else {
                        stable_samples = stable_samples + 1;

                        if (stable_samples >= STABLE_SAMPLES) {
                            state = PRESSED;
                            press_counter = press_counter + 1;
                            printf("presses: %d\n", press_counter);
                        }
                    }
                    break;

                case PRESSED:
                    if (!low) {
                        state = MAYBE_RELEASED;
                        stable_samples = 1;
                    }
                    break;

                case MAYBE_RELEASED:
                    if (low) {
                        state = PRESSED;
                    } else {
                        stable_samples = stable_samples + 1;

                        if (stable_samples >= STABLE_SAMPLES) {
                            state = RELEASED;
                        }
                    }
                    break;
            }
        }

        vTaskDelay(1);
    }
}

#include <stdio.h>
#include "driver/gpio.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BUTTON_PIN 5

static volatile int counter = 0;

static void IRAM_ATTR on_boot_press(void *arg) {
    counter = counter + 1;
}

void app_main () {
    gpio_reset_pin(BUTTON_PIN);
    gpio_set_direction(BUTTON_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_PIN, GPIO_PULLUP_ONLY);
    gpio_set_intr_type(BUTTON_PIN, GPIO_INTR_NEGEDGE);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_PIN, on_boot_press, NULL);

    int loop_counter = 0;

    while (1) {
        int current = counter;

        if (current != loop_counter) {
            printf("interrupts: %d (+%d)\n", current, current - loop_counter);
            loop_counter = current;
        }

        vTaskDelay(1);
    }
}

/*
 * Lesson 4.1: UART link ESP32-S3 <-> STM32F411.
 *
 * Each board has a button that controls the LED on the other board.
 * A click toggles the state and sends one byte:
 *   'L' - turn the LED on
 *   'O' - turn the LED off
 *
 * ESP32-S3 BOOT button -> STM32 LED (PC13)
 * STM32 KEY button     -> ESP32-S3 LED (GPIO16)
 *
 * Wiring (3.3 V logic on both boards):
 *   ESP32-S3 GPIO17 (UART1 TX) -> STM32 PA10 (USART1 RX)
 *   ESP32-S3 GPIO18 (UART1 RX) <- STM32 PA9  (USART1 TX)
 *   ESP32-S3 GND               -- STM32 GND
 *   ESP32-S3 GPIO16 -> 330 Ohm -> LED anode, LED cathode -> GND
 */
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"

static const char *TAG = "uart_led";

/* ---------- Кнопка BOOT ---------- */
#define BUTTON_GPIO         GPIO_NUM_0         // активний низький рівень, на платі є підтяжка
#define DEBOUNCE_MS         30

/* ---------- Світлодіод (керує STM32) ---------- */
#define LED_GPIO            GPIO_NUM_16        // через резистор 330 Ом на анод

/* ---------- UART ---------- */
#define LINK_UART           UART_NUM_1         // UART0 зайнятий консоллю
#define LINK_TX_GPIO        GPIO_NUM_17
#define LINK_RX_GPIO        GPIO_NUM_18
#define LINK_BAUD           115200
#define LINK_RX_BUF_SIZE    256                // має бути більшим за апаратний FIFO (128)

/* ---------- Цикл ---------- */
#define LOOP_PERIOD_MS      10

/* ---------- Команди ---------- */
#define CMD_LED_ON          'L'
#define CMD_LED_OFF         'O'

/* ================= UART ================= */

static void link_uart_init(void)
{
    uart_config_t cfg = {
        .baud_rate  = LINK_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(LINK_UART, LINK_RX_BUF_SIZE, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(LINK_UART, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(LINK_UART, LINK_TX_GPIO, LINK_RX_GPIO,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

static void send_led_state(bool led_on)
{
    const char cmd = led_on ? CMD_LED_ON : CMD_LED_OFF;
    int written = uart_write_bytes(LINK_UART, &cmd, 1);
    if (written != 1) {
        ESP_LOGE(TAG, "Не вдалося надіслати '%c'", cmd);
        return;
    }
    ESP_LOGI(TAG, "Надіслано '%c' (LED %s)", cmd, led_on ? "ON" : "OFF");
}

static void handle_command(uint8_t cmd)
{
    if (cmd == CMD_LED_ON) {
        gpio_set_level(LED_GPIO, 1);
        ESP_LOGI(TAG, "Отримано '%c' (LED ON)", cmd);
    } else if (cmd == CMD_LED_OFF) {
        gpio_set_level(LED_GPIO, 0);
        ESP_LOGI(TAG, "Отримано '%c' (LED OFF)", cmd);
    } else {
        ESP_LOGW(TAG, "Невідомий байт 0x%02X", cmd);
    }
}

/* ================= Світлодіод і кнопка ================= */

static void led_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << LED_GPIO,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    gpio_set_level(LED_GPIO, 0);
}

static void button_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
}

/* ================= Головний цикл ================= */

void app_main(void)
{
    led_init();
    button_init();
    link_uart_init();

    ESP_LOGI(TAG, "Старт. UART%d %d бод, TX GPIO%d, RX GPIO%d. Натисніть BOOT.",
             LINK_UART, LINK_BAUD, LINK_TX_GPIO, LINK_RX_GPIO);

    bool remote_led_on = false;            // стан світлодіода STM32
    int btn_last = 1;                      // 1 = відпущена (підтяжка)
    int btn_stable = 1;
    TickType_t btn_changed_at = xTaskGetTickCount();

    // Синхронізуємо STM32 з початковим станом
    send_led_state(remote_led_on);

    while (1) {
        // UART: чекаємо байт не довше LOOP_PERIOD_MS, це і є пауза циклу
        uint8_t cmd;
        if (uart_read_bytes(LINK_UART, &cmd, 1, pdMS_TO_TICKS(LOOP_PERIOD_MS)) == 1) {
            handle_command(cmd);
        }

        // Кнопка: новий рівень приймаємо, лише коли він стабільний DEBOUNCE_MS
        int btn = gpio_get_level(BUTTON_GPIO);
        TickType_t now = xTaskGetTickCount();

        if (btn != btn_last) {
            btn_last = btn;
            btn_changed_at = now;
        } else if (btn != btn_stable && now - btn_changed_at >= pdMS_TO_TICKS(DEBOUNCE_MS)) {
            btn_stable = btn;
            if (btn_stable == 0) {
                remote_led_on = !remote_led_on;
                send_led_state(remote_led_on);
            }
        }
    }
}

#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define LDR_ADC_UNIT        ADC_UNIT_1
#define LDR_ADC_CHANNEL     ADC_CHANNEL_3
#define LDR_ADC_ATTEN       ADC_ATTEN_DB_12
#define LED_PIN             GPIO_NUM_16

#define SMA_ALPHA           0.1f
#define SAMPLE_PERIOD_MS    50
#define LOG_EVERY_N         10

#define DARK_THRESHOLD      1100
#define LIGHT_THRESHOLD     1900

static const char *TAG = "ldr_night_light";

static float sma_update(float prev, int raw)
{
    return SMA_ALPHA * raw + (1.0f - SMA_ALPHA) * prev;
}

static bool adc_calibration_init(adc_cali_handle_t *out_handle)
{
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = LDR_ADC_UNIT,
        .chan = LDR_ADC_CHANNEL,
        .atten = LDR_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    esp_err_t ret = adc_cali_create_scheme_curve_fitting(&cali_config, out_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Калібрування ADC недоступне (%s), лог буде без мВ", esp_err_to_name(ret));
        return false;
    }
    return true;
}

void app_main(void)
{
    gpio_config_t led_conf = {
        .pin_bit_mask = 1ULL << LED_PIN,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&led_conf));
    gpio_set_level(LED_PIN, 0);

    adc_oneshot_unit_handle_t adc_handle;
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = LDR_ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = LDR_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, LDR_ADC_CHANNEL, &chan_cfg));

    adc_cali_handle_t cali_handle = NULL;
    bool calibrated = adc_calibration_init(&cali_handle);

    int raw = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, LDR_ADC_CHANNEL, &raw));
    float sma = raw;
    bool led_on = false;
    int tick = 0;

    ESP_LOGI(TAG, "Старт: пороги темно < %d, світло > %d", DARK_THRESHOLD, LIGHT_THRESHOLD);

    while (1) {
        if (adc_oneshot_read(adc_handle, LDR_ADC_CHANNEL, &raw) != ESP_OK) {
            ESP_LOGW(TAG, "Помилка зчитування ADC");
            vTaskDelay(pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
            continue;
        }

        sma = sma_update(sma, raw);
        int filtered = (int)sma;

        if (!led_on && filtered < DARK_THRESHOLD) {
            led_on = true;
            gpio_set_level(LED_PIN, 1);
            ESP_LOGI(TAG, "Темно (%d) -> світлодіод увімкнено", filtered);
        } else if (led_on && filtered > LIGHT_THRESHOLD) {
            led_on = false;
            gpio_set_level(LED_PIN, 0);
            ESP_LOGI(TAG, "Світло (%d) -> світлодіод вимкнено", filtered);
        }

        if (++tick % LOG_EVERY_N == 0) {
            int mv = 0;
            if (calibrated && adc_cali_raw_to_voltage(cali_handle, filtered, &mv) == ESP_OK) {
                ESP_LOGI(TAG, "raw=%4d  sma=%4d  (%d мВ)  LED=%s", raw, filtered, mv, led_on ? "ON" : "OFF");
            } else {
                ESP_LOGI(TAG, "raw=%4d  sma=%4d  LED=%s", raw, filtered, led_on ? "ON" : "OFF");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
    }
}

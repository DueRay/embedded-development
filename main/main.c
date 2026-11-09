#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "driver/touch_sens.h"
#include "esp_log.h"

/* ---------- Зумер (LEDC) ---------- */
#define BUZZER_PIN          GPIO_NUM_7
#define LEDC_TIMER          LEDC_TIMER_0
#define LEDC_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL        LEDC_CHANNEL_0
#define LEDC_DUTY_RES       LEDC_TIMER_10_BIT // Роздільна здатність 10 біт (0 - 1023)
#define LEDC_DUTY           512               // 50% заповнення (меандр для максимальної гучності)

/* ---------- Частоти нот (октава 4) ---------- */
#define NOTE_C4             262
#define NOTE_D4             294
#define NOTE_E4             330

/* ---------- Сенсорні клавіші ---------- */
// На ESP32-S3 канал TOUCHn підключений до GPIOn: TOUCH4 = GPIO4 і т.д.
#define KEY_COUNT           3
// Апаратний поріг: дотик = дані вище benchmark (значення без дотику) більше ніж на це число.
// Benchmark сенсор вимірює сам, тому не торкайтеся клавіш під час старту
#define TOUCH_ACTIVE_THRESH 4000
#define LOOP_PERIOD_MS      10

typedef struct {
    int chan_id;        // номер каналу сенсора (= номер GPIO на ESP32-S3)
    uint32_t freq_hz;   // нота клавіші
    const char *name;
} touch_key_t;

static const touch_key_t keys[KEY_COUNT] = {
    { 4, NOTE_C4, "C" },
    { 5, NOTE_D4, "D" },
    { 6, NOTE_E4, "E" },
};

static const char *TAG = "touch_synth";

static touch_sensor_handle_t s_touch;
static touch_channel_handle_t s_chan[KEY_COUNT];

static volatile int s_key = -1;

/* ================= Зумер ================= */

static void buzzer_init(void) {
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = LEDC_DUTY_RES,
        .freq_hz          = 1000, // Початкова частота
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = BUZZER_PIN,
        .duty           = 0, // Початково вимкнено
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

static void buzzer_tone(uint32_t freq_hz) {
    ledc_set_freq(LEDC_MODE, LEDC_TIMER, freq_hz);
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, LEDC_DUTY);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

static void buzzer_silence(void) {
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

/* ================= Сенсорні клавіші ================= */

static int chan_to_key(int chan_id) {
    for (int i = 0; i < KEY_COUNT; i++) {
        if (keys[i].chan_id == chan_id) {
            return i;
        }
    }
    return -1;
}

static bool on_touch_active(touch_sensor_handle_t sens, const touch_active_event_data_t *event, void *ctx) {
    int key = chan_to_key(event->chan_id);
    if (key >= 0) {
        s_key = key;
    }
    return false;
}

static bool on_touch_inactive(touch_sensor_handle_t sens, const touch_inactive_event_data_t *event, void *ctx) {
    int key = chan_to_key(event->chan_id);
    if (key >= 0 && key == s_key) {
        s_key = -1;
    }
    return false;
}

static void touch_init(void) {
    touch_sensor_sample_config_t sample_cfg[TOUCH_SAMPLE_CFG_NUM] = {
        TOUCH_SENSOR_V2_DEFAULT_SAMPLE_CONFIG(500, TOUCH_VOLT_LIM_L_0V5, TOUCH_VOLT_LIM_H_2V2),
    };
    touch_sensor_config_t sens_cfg = TOUCH_SENSOR_DEFAULT_BASIC_CONFIG(1, sample_cfg);
    ESP_ERROR_CHECK(touch_sensor_new_controller(&sens_cfg, &s_touch));

    touch_channel_config_t chan_cfg = {
        .active_thresh    = { TOUCH_ACTIVE_THRESH },
        .charge_speed     = TOUCH_CHARGE_SPEED_7,
        .init_charge_volt = TOUCH_INIT_CHARGE_VOLT_DEFAULT,
    };
    for (int i = 0; i < KEY_COUNT; i++) {
        ESP_ERROR_CHECK(touch_sensor_new_channel(s_touch, keys[i].chan_id, &chan_cfg, &s_chan[i]));

        touch_chan_info_t info = {};
        ESP_ERROR_CHECK(touch_sensor_get_channel_info(s_chan[i], &info));
        ESP_LOGI(TAG, "Клавіша %s: TOUCH%d на GPIO%d", keys[i].name, keys[i].chan_id, info.chan_gpio);
    }

    touch_sensor_filter_config_t filter_cfg = TOUCH_SENSOR_DEFAULT_FILTER_CONFIG();
    ESP_ERROR_CHECK(touch_sensor_config_filter(s_touch, &filter_cfg));

    touch_event_callbacks_t callbacks = {
        .on_active   = on_touch_active,
        .on_inactive = on_touch_inactive,
    };
    ESP_ERROR_CHECK(touch_sensor_register_callbacks(s_touch, &callbacks, NULL));

    ESP_ERROR_CHECK(touch_sensor_enable(s_touch));
    ESP_ERROR_CHECK(touch_sensor_start_continuous_scanning(s_touch));
}

/* ================= Синтезатор ================= */

void app_main(void) {
    ESP_LOGI(TAG, "Ініціалізація синтезатора...");
    buzzer_init();
    touch_init();
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "Готово. Торкніться клавіш: GPIO4 = C, GPIO5 = D, GPIO6 = E");

    int playing = -1;       // клавіша, що звучить зараз (-1 = тиша)

    while (1) {
        int key = s_key;

        if (key != playing) {
            if (key < 0) {
                buzzer_silence();
            } else {
                buzzer_tone(keys[key].freq_hz);
                ESP_LOGI(TAG, "Нота %s (%" PRIu32 " Гц)", keys[key].name, keys[key].freq_hz);
            }
            playing = key;
        }

        vTaskDelay(pdMS_TO_TICKS(LOOP_PERIOD_MS));
    }
}

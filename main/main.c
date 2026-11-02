#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

static const char *TAG = "SERVO";

/* ---------- Піни ---------- */
#define SERVO_GPIO          4
#define POT_GPIO            6                  // ADC1_CH5 на ESP32-S3

/* ---------- PWM (LEDC) ---------- */
#define SERVO_TIMER         LEDC_TIMER_0
#define SERVO_CHANNEL       LEDC_CHANNEL_0
#define SERVO_MODE          LEDC_LOW_SPEED_MODE
#define SERVO_FREQ_HZ       50                 // період 20 мс
#define SERVO_RES           LEDC_TIMER_14_BIT
#define SERVO_DUTY_MAX      ((1 << 14) - 1)    // 16383
#define SERVO_PERIOD_US     20000

/* ---------- Параметри SG90 ---------- */
#define SERVO_MIN_US        500                // імпульс для 0°
#define SERVO_MAX_US        2400               // імпульс для SERVO_ANGLE_DEG
#define SERVO_ANGLE_DEG     180.0f             // повний хід сервоприводу

/* ---------- Параметри потенціометра ---------- */
#define POT_ANGLE_DEG       270.0f             // повний хід потенціометра
#define POT_VREF_MV         3300               // напруга на крайньому правому положенні
#define POT_ADC_ATTEN       ADC_ATTEN_DB_12    // діапазон ~0..3.1 В
#define POT_RAW_MAX         4095               // 12 біт (ADC_BITWIDTH_DEFAULT на ESP32-S3)

/* ---------- Спільний діапазон (1:1, решта обрізається) ---------- */
#define WORK_ANGLE_DEG      (POT_ANGLE_DEG < SERVO_ANGLE_DEG ? POT_ANGLE_DEG : SERVO_ANGLE_DEG)

/* ---------- Параметри циклу ---------- */
#define ADC_SAMPLES         32                 // усереднення для зменшення шуму
#define LOOP_DELAY_MS       20                 // один період PWM
#define ANGLE_THRESHOLD_DEG 0.2f               // мінімальна зміна кута для оновлення

static adc_oneshot_unit_handle_t s_adc;
static adc_channel_t s_adc_channel;
static adc_cali_handle_t s_adc_cali;           // NULL, якщо калібрування недоступне

/* ================= Сервопривід ================= */

static uint32_t angle_to_duty(float angle)
{
    float pulse_us = SERVO_MIN_US + (SERVO_MAX_US - SERVO_MIN_US) * angle / SERVO_ANGLE_DEG;
    return (uint32_t)lroundf(pulse_us * SERVO_DUTY_MAX / SERVO_PERIOD_US);
}

static void servo_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode      = SERVO_MODE,
        .timer_num       = SERVO_TIMER,
        .duty_resolution = SERVO_RES,
        .freq_hz         = SERVO_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t channel = {
        .gpio_num   = SERVO_GPIO,
        .speed_mode = SERVO_MODE,
        .channel    = SERVO_CHANNEL,
        .timer_sel  = SERVO_TIMER,
        .duty       = angle_to_duty(0.0f),
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
}

static void servo_set_angle(float angle)
{
    ESP_ERROR_CHECK(ledc_set_duty(SERVO_MODE, SERVO_CHANNEL, angle_to_duty(angle)));
    ESP_ERROR_CHECK(ledc_update_duty(SERVO_MODE, SERVO_CHANNEL));
}

/* ================= Потенціометр (ADC) ================= */

static adc_cali_handle_t adc_cali_init(adc_unit_t unit, adc_channel_t chan, adc_atten_t atten)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t err = ESP_ERR_NOT_SUPPORTED;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cfg = {
        .unit_id  = unit,
        .chan     = chan,
        .atten    = atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_cali_create_scheme_curve_fitting(&cfg, &handle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    (void)chan;
    adc_cali_line_fitting_config_t cfg = {
        .unit_id  = unit,
        .atten    = atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_cali_create_scheme_line_fitting(&cfg, &handle);
#endif

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Калібрування ADC недоступне, лінійне наближення");
        return NULL;
    }
    return handle;
}

static void pot_init(void)
{
    adc_unit_t unit;
    ESP_ERROR_CHECK(adc_oneshot_io_to_channel(POT_GPIO, &unit, &s_adc_channel));

    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = unit,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &s_adc));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = POT_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, s_adc_channel, &chan_cfg));

    s_adc_cali = adc_cali_init(unit, s_adc_channel, POT_ADC_ATTEN);
}

static int pot_read_mv(void)
{
    int sum = 0;
    for (int i = 0; i < ADC_SAMPLES; i++) {
        int sample;
        ESP_ERROR_CHECK(adc_oneshot_read(s_adc, s_adc_channel, &sample));
        sum += sample;
    }
    int raw = sum / ADC_SAMPLES;

    int mv;
    if (s_adc_cali) {
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(s_adc_cali, raw, &mv));
    } else {
        mv = raw * POT_VREF_MV / POT_RAW_MAX;
    }
    return mv;
}

static float pot_read_angle(void)
{
    float angle = pot_read_mv() * POT_ANGLE_DEG / POT_VREF_MV;

    if (angle < 0.0f) {
        angle = 0.0f;
    } else if (angle > WORK_ANGLE_DEG) {
        angle = WORK_ANGLE_DEG;
    }
    return angle;
}

/* ================= Головний цикл ================= */

void app_main(void)
{
    servo_init();
    pot_init();
    ESP_LOGI(TAG, "Старт. Потенціометр %.0f°, серво %.0f°, робочий діапазон 0..%.0f°",
             POT_ANGLE_DEG, SERVO_ANGLE_DEG, WORK_ANGLE_DEG);

    float angle = pot_read_angle();
    servo_set_angle(angle);

    while (1) {
        float new_angle = pot_read_angle();

        if (fabsf(new_angle - angle) > ANGLE_THRESHOLD_DEG) {
            angle = new_angle;
            servo_set_angle(angle);
            ESP_LOGI(TAG, "Кут: %.1f°", angle);
        }

        vTaskDelay(pdMS_TO_TICKS(LOOP_DELAY_MS));
    }
}

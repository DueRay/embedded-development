#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char *TAG = "PWM_3_3";

/* ---------- Піни ---------- */
#define POT_GPIO            6                  // ADC1_CH5: повзунок потенціометра
#define LED_GPIO            5                  // через резистор 330 Ом на анод світлодіода
#define MOTOR_GPIO          7                  // через резистор 220 Ом на базу 2N2222A

/* ---------- PWM світлодіода (LEDC) ---------- */
#define LEDC_MODE           LEDC_LOW_SPEED_MODE
#define LED_TIMER           LEDC_TIMER_0
#define LED_CHANNEL         LEDC_CHANNEL_0
#define LED_FREQ_HZ         5000               // око не бачить мерехтіння

/* ---------- PWM двигуна (LEDC) ---------- */
#define MOTOR_TIMER         LEDC_TIMER_1       // окремий таймер: частота двигуна не змінює частоту світлодіода
#define MOTOR_CHANNEL       LEDC_CHANNEL_1
#define MOTOR_FREQ_HZ       20000              // вище за межу слуху: двигун не пищить
#define MOTOR_MIN_PCT       15                 // нижче цього заповнення вал не рушає з місця (підберіть для свого двигуна)

/* ---------- Спільні параметри PWM ---------- */
#define PWM_RES             LEDC_TIMER_10_BIT
#define PWM_DUTY_MAX        ((1 << 10) - 1)    // 1023

/* ---------- ADC ---------- */
#define POT_ADC_ATTEN       ADC_ATTEN_DB_12    // діапазон ~0..3.1 В
#define POT_RAW_MAX         4095               // 12 біт
#define ADC_SAMPLES         16                 // усереднення для зменшення шуму
#define POT_DEADZONE        100                // нижче цього значення вихід = 0 (повністю вимкнено)
#define DUTY_THRESHOLD      4                  // мінімальна зміна заповнення для оновлення

/* ---------- Цикл ---------- */
#define LOOP_DELAY_MS       20
#define LOG_EVERY_N         25                 // лог кожні 25 × 20 мс = 0.5 с

static adc_oneshot_unit_handle_t s_adc;
static adc_channel_t s_pot_chan;

/* ================= PWM ================= */

static void pwm_channel_init(ledc_timer_t timer, ledc_channel_t channel, uint32_t freq_hz, int gpio)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_MODE,
        .timer_num       = timer,
        .duty_resolution = PWM_RES,
        .freq_hz         = freq_hz,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t channel_cfg = {
        .gpio_num   = gpio,
        .speed_mode = LEDC_MODE,
        .channel    = channel,
        .timer_sel  = timer,
        .intr_type  = LEDC_INTR_DISABLE,
        .duty       = 0,                       // на старті все вимкнено
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_cfg));
}

static void pwm_set_duty(ledc_channel_t channel, uint32_t duty)
{
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, channel, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, channel));
}

/* ================= Потенціометри (ADC) ================= */

static void pot_init(void)
{
    adc_unit_t unit;
    ESP_ERROR_CHECK(adc_oneshot_io_to_channel(POT_GPIO, &unit, &s_pot_chan));

    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = unit,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &s_adc));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = POT_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, s_pot_chan, &chan_cfg));
}

static int pot_read_raw(void)
{
    int sum = 0;
    for (int i = 0; i < ADC_SAMPLES; i++) {
        int sample;
        ESP_ERROR_CHECK(adc_oneshot_read(s_adc, s_pot_chan, &sample));
        sum += sample;
    }
    return sum / ADC_SAMPLES;
}

/* ================= Перетворення положення ручки у заповнення ================= */

// Світлодіод: 0 у мертвій зоні, далі лінійно 0..100 %
static uint32_t led_raw_to_duty(int raw)
{
    if (raw < POT_DEADZONE) {
        return 0;
    }
    float x = (float)(raw - POT_DEADZONE) / (POT_RAW_MAX - POT_DEADZONE);
    return (uint32_t)lroundf(x * PWM_DUTY_MAX);
}

// Двигун: 0 у мертвій зоні, далі MOTOR_MIN_PCT..100 %
static uint32_t motor_raw_to_duty(int raw)
{
    if (raw < POT_DEADZONE) {
        return 0;
    }
    const uint32_t duty_min = PWM_DUTY_MAX * MOTOR_MIN_PCT / 100;
    float x = (float)(raw - POT_DEADZONE) / (POT_RAW_MAX - POT_DEADZONE);
    return duty_min + (uint32_t)lroundf(x * (PWM_DUTY_MAX - duty_min));
}

/* ================= Головний цикл ================= */

void app_main(void)
{
    pwm_channel_init(LED_TIMER, LED_CHANNEL, LED_FREQ_HZ, LED_GPIO);
    pwm_channel_init(MOTOR_TIMER, MOTOR_CHANNEL, MOTOR_FREQ_HZ, MOTOR_GPIO);
    pot_init();
    ESP_LOGI(TAG, "Старт: LED %d Гц на GPIO%d, двигун %d Гц на GPIO%d",
             LED_FREQ_HZ, LED_GPIO, MOTOR_FREQ_HZ, MOTOR_GPIO);

    uint32_t led_duty = 0;
    uint32_t motor_duty = 0;
    int tick = 0;

    while (1) {
        int raw = pot_read_raw();

        uint32_t new_led_duty = led_raw_to_duty(raw);
        if (abs((int)new_led_duty - (int)led_duty) > DUTY_THRESHOLD || (new_led_duty == 0 && led_duty != 0)) {
            led_duty = new_led_duty;
            pwm_set_duty(LED_CHANNEL, led_duty);
        }

        uint32_t new_motor_duty = motor_raw_to_duty(raw);
        if (abs((int)new_motor_duty - (int)motor_duty) > DUTY_THRESHOLD || (new_motor_duty == 0 && motor_duty != 0)) {
            motor_duty = new_motor_duty;
            pwm_set_duty(MOTOR_CHANNEL, motor_duty);
        }

        if (++tick >= LOG_EVERY_N) {
            tick = 0;
            ESP_LOGI(TAG, "pot=%4d | LED: duty=%4lu (%3lu%%) | Двигун: duty=%4lu (%3lu%%)",
                     raw, led_duty, led_duty * 100 / PWM_DUTY_MAX,
                     motor_duty, motor_duty * 100 / PWM_DUTY_MAX);
        }

        vTaskDelay(pdMS_TO_TICKS(LOOP_DELAY_MS));
    }
}

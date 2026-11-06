#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"

static const char *TAG = "SAFE";

/* ---------- Піни ---------- */
#define ENC_GPIO_CLK         5          // сигнал A енкодера
#define ENC_GPIO_DT          6          // сигнал B енкодера
#define ENC_GPIO_SW          7          // кнопка енкодера (натиснута = 0)
#define SERVO_GPIO           15
#define BUZZER_GPIO          16

/* ---------- Енкодер ---------- */
#define ENC_EDGES_PER_DETENT 4          // фронтів A+B на один клік: 4 (повний цикл) або 2 (пів циклу)
#define ENC_PCNT_LIMIT       1000       // межа апаратного лічильника, далі значення накопичує драйвер
#define ENC_GLITCH_NS        10000      // апаратний фільтр завад, нс (максимум ~12.7 мкс)
#define POLL_MS              5          // період опитування входів
#define BTN_DEBOUNCE_MS      30         // час стабільного рівня кнопки

/* ---------- Код сейфу ---------- */
#define CODE_LEN             4          // кількість цифр коду (не більше 8)
#define MAX_ATTEMPTS         3          // кількість спроб до блокування
static const uint8_t SECRET_CODE[CODE_LEN] = {3, 7, 0, 5};

/* ---------- Сервопривід SG90 (LEDC) ---------- */
#define SERVO_TIMER          LEDC_TIMER_0
#define SERVO_CHANNEL        LEDC_CHANNEL_0
#define LEDC_MODE            LEDC_LOW_SPEED_MODE
#define SERVO_FREQ_HZ        50                 // період 20 мс
#define SERVO_RES            LEDC_TIMER_14_BIT
#define SERVO_DUTY_MAX       ((1 << 14) - 1)
#define SERVO_PERIOD_US      20000
#define SERVO_MIN_US         500                // імпульс для 0°
#define SERVO_MAX_US         2400               // імпульс для SERVO_RANGE_DEG
#define SERVO_RANGE_DEG      180.0f
#define SERVO_LOCKED_DEG     0.0f               // замок закрито
#define SERVO_OPEN_DEG       90.0f              // замок відкрито

/* ---------- Пасивний зумер (LEDC) ---------- */
#define BUZZER_TIMER         LEDC_TIMER_1
#define BUZZER_CHANNEL       LEDC_CHANNEL_1
#define BUZZER_RES           LEDC_TIMER_10_BIT  // мінімальна частота ~77 Гц
#define BUZZER_DUTY_ON       (1 << 9)           // скважність 50 %
#define NOTE_GAP_MS          20                 // пауза між нотами
#define ALARM_REPEAT         5                  // кількість повторів сирени

/* ================= Мелодії ================= */

typedef struct {
    uint16_t freq_hz;   // 0 = пауза
    uint16_t ms;
} note_t;

static const note_t MELODY_OPEN[] = {
    {523, 120}, {659, 120}, {784, 120}, {1047, 350},
};

static const note_t MELODY_WRONG[] = {
    {392, 150}, {0, 50}, {262, 400},
};

static const note_t MELODY_ALARM[] = {
    {988, 250}, {494, 250}, {988, 250}, {494, 250},
};

#define MELODY_LEN(m) (sizeof(m) / sizeof((m)[0]))

/* ================= Сервопривід ================= */

static uint32_t angle_to_duty(float angle)
{
    float pulse_us = SERVO_MIN_US + (SERVO_MAX_US - SERVO_MIN_US) * angle / SERVO_RANGE_DEG;
    return (uint32_t)lroundf(pulse_us * SERVO_DUTY_MAX / SERVO_PERIOD_US);
}

static void servo_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_MODE,
        .timer_num       = SERVO_TIMER,
        .duty_resolution = SERVO_RES,
        .freq_hz         = SERVO_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t channel = {
        .gpio_num   = SERVO_GPIO,
        .speed_mode = LEDC_MODE,
        .channel    = SERVO_CHANNEL,
        .timer_sel  = SERVO_TIMER,
        .duty       = angle_to_duty(SERVO_LOCKED_DEG),
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
}

static void servo_set_angle(float angle)
{
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, SERVO_CHANNEL, angle_to_duty(angle)));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, SERVO_CHANNEL));
}

/* ================= Зумер ================= */

static void buzzer_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_MODE,
        .timer_num       = BUZZER_TIMER,
        .duty_resolution = BUZZER_RES,
        .freq_hz         = 1000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t channel = {
        .gpio_num   = BUZZER_GPIO,
        .speed_mode = LEDC_MODE,
        .channel    = BUZZER_CHANNEL,
        .timer_sel  = BUZZER_TIMER,
        .duty       = 0,                // тиша
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
}

static void buzzer_tone(uint32_t freq_hz, uint32_t ms)
{
    if (freq_hz > 0) {
        ESP_ERROR_CHECK(ledc_set_freq(LEDC_MODE, BUZZER_TIMER, freq_hz));
        ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, BUZZER_CHANNEL, BUZZER_DUTY_ON));
        ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, BUZZER_CHANNEL));
    }
    vTaskDelay(pdMS_TO_TICKS(ms));
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, BUZZER_CHANNEL, 0));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, BUZZER_CHANNEL));
}

static void play_melody(const note_t *melody, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        buzzer_tone(melody[i].freq_hz, melody[i].ms);
        vTaskDelay(pdMS_TO_TICKS(NOTE_GAP_MS));
    }
}

/* ================= Енкодер (PCNT, режим X4) ================= */

static pcnt_unit_handle_t s_pcnt;
static int s_enc_pos;   // значення лічильника в останньому обробленому положенні фіксації

static void encoder_init(void)
{
    // accum_count: драйвер додає межу до накопиченого значення, коли лічильник її досягає
    pcnt_unit_config_t unit_cfg = {
        .high_limit = ENC_PCNT_LIMIT,
        .low_limit  = -ENC_PCNT_LIMIT,
        .flags.accum_count = 1,
    };
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_cfg, &s_pcnt));

    pcnt_glitch_filter_config_t filter_cfg = { .max_glitch_ns = ENC_GLITCH_NS };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(s_pcnt, &filter_cfg));

    // Канал A рахує фронти A за рівнем B, канал B рахує фронти B за рівнем A.
    // Брязкіт одного контакту дає +1/-1, і ці значення компенсують одне одне.
    pcnt_chan_config_t chan_a_cfg = { .edge_gpio_num = ENC_GPIO_CLK, .level_gpio_num = ENC_GPIO_DT };
    pcnt_chan_config_t chan_b_cfg = { .edge_gpio_num = ENC_GPIO_DT,  .level_gpio_num = ENC_GPIO_CLK };
    pcnt_channel_handle_t chan_a = NULL;
    pcnt_channel_handle_t chan_b = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(s_pcnt, &chan_a_cfg, &chan_a));
    ESP_ERROR_CHECK(pcnt_new_channel(s_pcnt, &chan_b_cfg, &chan_b));

    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(chan_a,
        PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(chan_a,
        PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(chan_b,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(chan_b,
        PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    // Драйвер PCNT не вмикає підтяжку, тому вмикаємо внутрішню
    ESP_ERROR_CHECK(gpio_pullup_en(ENC_GPIO_CLK));
    ESP_ERROR_CHECK(gpio_pullup_en(ENC_GPIO_DT));

    // Точки спостереження на межах потрібні для накопичення (accum_count)
    ESP_ERROR_CHECK(pcnt_unit_add_watch_point(s_pcnt, ENC_PCNT_LIMIT));
    ESP_ERROR_CHECK(pcnt_unit_add_watch_point(s_pcnt, -ENC_PCNT_LIMIT));

    ESP_ERROR_CHECK(pcnt_unit_enable(s_pcnt));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(s_pcnt));
    ESP_ERROR_CHECK(pcnt_unit_start(s_pcnt));
    s_enc_pos = 0;
}

static int encoder_get_count(void)
{
    int count = 0;
    ESP_ERROR_CHECK(pcnt_unit_get_count(s_pcnt, &count));
    return count;
}

// Повертає +1 або -1 за один повний клік, 0 якщо кліку немає.
// Клік зараховується тільки після повного кроку від останнього положення (гістерезис),
// тому брязкіт на межі не дає хибної зміни напрямку.
static int encoder_take_tick(void)
{
    int count = encoder_get_count();
    if (count - s_enc_pos >= ENC_EDGES_PER_DETENT) {
        s_enc_pos += ENC_EDGES_PER_DETENT;
        return +1;
    }
    if (s_enc_pos - count >= ENC_EDGES_PER_DETENT) {
        s_enc_pos -= ENC_EDGES_PER_DETENT;
        return -1;
    }
    return 0;
}

// Ігнорує обертання, яке відбулося під час мелодії
static void encoder_discard(void)
{
    int count = encoder_get_count();
    int half = ENC_EDGES_PER_DETENT / 2;
    int detent = count >= 0 ? (count + half) / ENC_EDGES_PER_DETENT
                            : -((-count + half) / ENC_EDGES_PER_DETENT);
    s_enc_pos = detent * ENC_EDGES_PER_DETENT;
}

/* ================= Кнопка ================= */

static void button_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << ENC_GPIO_SW,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
}

// Повертає true один раз на кожне натискання (після антибрязкоту)
static bool button_take_press(void)
{
    static int stable = 1;      // підтверджений рівень (1 = відпущена)
    static int last_raw = 1;
    static int same_ms = 0;

    int raw = gpio_get_level(ENC_GPIO_SW);
    if (raw != last_raw) {
        last_raw = raw;
        same_ms = 0;
        return false;
    }
    if (raw == stable) {
        return false;
    }
    same_ms += POLL_MS;
    if (same_ms < BTN_DEBOUNCE_MS) {
        return false;
    }
    stable = raw;
    return stable == 0;
}

/* ================= Скінченний автомат ================= */

typedef enum {
    ST_IDLE,    // спроба почалася, цифр ще немає
    ST_ENTER,   // введення цифр
    ST_OPEN,    // код правильний, сейф відкрито
    ST_ALARM,   // спроби вичерпано, блокування до перезавантаження
} safe_state_t;

typedef enum {
    EV_TICK_CW,
    EV_TICK_CCW,
    EV_BUTTON,
} safe_event_t;

static safe_state_t s_state;
static int s_attempt;               // номер поточної спроби, 1..MAX_ATTEMPTS
static uint8_t s_code[CODE_LEN];    // введені цифри
static int s_index;                 // індекс поточної цифри
static safe_event_t s_dir;          // напрямок обертання для поточної цифри

static void attempt_begin(void)
{
    s_state = ST_IDLE;
    s_index = 0;
    printf("Спроба %d/%d. Код: ", s_attempt, MAX_ATTEMPTS);
    fflush(stdout);
}

static void digit_begin(safe_event_t dir)
{
    s_dir = dir;
    s_code[s_index] = 0;
    printf(s_index == 0 ? "0" : " 0");
    fflush(stdout);
}

static void digit_increment(void)
{
    s_code[s_index] = (s_code[s_index] + 1) % 10;   // циклічна зміна 9 -> 0
    printf("\b%d", s_code[s_index]);                // перезаписуємо останню цифру
    fflush(stdout);
}

static void go_alarm(void)
{
    s_state = ST_ALARM;
    ESP_LOGE(TAG, "Спроби вичерпано. Сейф заблоковано до перезавантаження");
    for (int i = 0; i < ALARM_REPEAT; i++) {
        play_melody(MELODY_ALARM, MELODY_LEN(MELODY_ALARM));
    }
    // Блокування: автомат більше не обробляє події
    while (1) {
        vTaskDelay(portMAX_DELAY);
    }
}

static void go_open(void)
{
    s_state = ST_OPEN;
    servo_set_angle(SERVO_OPEN_DEG);
    ESP_LOGI(TAG, "Код правильний. Сейф відкрито. Натисніть кнопку, щоб закрити");
    play_melody(MELODY_OPEN, MELODY_LEN(MELODY_OPEN));
    encoder_discard();
}

// Неправильний код або RESET: спробу використано
static void attempt_failed(void)
{
    int left = MAX_ATTEMPTS - s_attempt;
    if (left <= 0) {
        go_alarm();
    }
    ESP_LOGW(TAG, "Залишилось спроб: %d", left);
    play_melody(MELODY_WRONG, MELODY_LEN(MELODY_WRONG));
    encoder_discard();
    s_attempt++;
    attempt_begin();
}

static void safe_handle_event(safe_event_t ev)
{
    switch (s_state) {
    case ST_IDLE:
        // Перший клік у будь-якому напрямку починає першу цифру зі значенням 0
        if (ev != EV_BUTTON) {
            s_state = ST_ENTER;
            digit_begin(ev);
        }
        break;

    case ST_ENTER:
        if (ev == EV_BUTTON) {
            printf(" [RESET]\n");
            attempt_failed();
        } else if (ev == s_dir) {
            digit_increment();
        } else {
            // Зміна напрямку підтверджує поточну цифру
            s_index++;
            if (s_index < CODE_LEN) {
                digit_begin(ev);
            } else {
                printf("\n");
                if (memcmp(s_code, SECRET_CODE, CODE_LEN) == 0) {
                    go_open();
                } else {
                    ESP_LOGW(TAG, "Неправильний код");
                    attempt_failed();
                }
            }
        }
        break;

    case ST_OPEN:
        if (ev == EV_BUTTON) {
            servo_set_angle(SERVO_LOCKED_DEG);
            ESP_LOGI(TAG, "Сейф закрито");
            s_attempt = 1;
            attempt_begin();
        }
        break;

    case ST_ALARM:
        break;
    }
}

/* ================= Головний цикл ================= */

void app_main(void)
{
    servo_init();
    buzzer_init();
    button_init();
    encoder_init();

    ESP_LOGI(TAG, "Сейф закрито. Код: %d цифри, спроб: %d", CODE_LEN, MAX_ATTEMPTS);
    ESP_LOGI(TAG, "Обертання - зміна цифри, зміна напрямку - наступна цифра, кнопка - RESET");

    s_attempt = 1;
    attempt_begin();

    while (1) {
        int tick;
        while ((tick = encoder_take_tick()) != 0) {
            safe_handle_event(tick > 0 ? EV_TICK_CW : EV_TICK_CCW);
        }
        if (button_take_press()) {
            safe_handle_event(EV_BUTTON);
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

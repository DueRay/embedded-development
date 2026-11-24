#include <stdio.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"

#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

static const char *TAG = "clock";

/* ---------- Конфігурація ---------- */
#define I2C_PORT        I2C_NUM_0
#define PIN_SDA         GPIO_NUM_6
#define PIN_SCL         GPIO_NUM_7
#define I2C_FREQ_HZ     100000          /* DS1307 максимум 100 кГц */

#define OLED_ADDR       0x3C
#define OLED_W          128
#define OLED_H          64

#define DS1307_ADDR     0x68
#define DS1307_REG_SEC  0x00

/* Група "текст + кільце" по центру екрана по горизонталі.
 * TEXT_W - типова ширина часу шрифтом montserrat_20 (від 53 до 90 px) */
#define TEXT_W          80
#define TEXT_RING_GAP   8               /* відстань між текстом і кільцем, px */
#define GROUP_W         (TEXT_W + TEXT_RING_GAP + RING_D)
#define GROUP_X         ((OLED_W - GROUP_W) / 2)
#define TEXT_CX         (GROUP_X + TEXT_W / 2)          /* вісь часу і дати */

/* Кільце з 6 сегментів: кожен сегмент = 10 секунд */
#define RING_SEGMENTS   6
#define RING_D          32              /* зовнішній діаметр, px */
#define RING_CX         (GROUP_X + TEXT_W + TEXT_RING_GAP + RING_D / 2)
#define RING_CY         32
#define RING_W          5               /* товщина сегмента, що горить, px */
#define RING_TRACK_W    1               /* товщина контуру сегмента, що не горить, px */
#define RING_GAP_DEG    16              /* проміжок між сегментами, градуси */

/* Текст: висота рядків шрифтів montserrat_20 і montserrat_10 */
#define TIME_LINE_H     22
#define DATE_LINE_H     11
#define TEXT_GAP        3               /* відстань між часом і датою, px */

/* ---------- Глобальні дескриптори ---------- */
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_rtc;
static lv_obj_t *s_lbl_time;
static lv_obj_t *s_lbl_date;
static lv_obj_t *s_ring[RING_SEGMENTS];

/* ---------- BCD ---------- */
static inline uint8_t bcd2bin(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static inline uint8_t bin2bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

/* ====================================================================
 *  I2C шина
 * ==================================================================== */
static void i2c_bus_init(void)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_PORT,
        .sda_io_num = PIN_SDA,
        .scl_io_num = PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &s_bus));
}

/* ====================================================================
 *  DS1307
 * ==================================================================== */
static esp_err_t rtc_write_time(const struct tm *t)
{
    uint8_t buf[8] = {
        DS1307_REG_SEC,
        bin2bcd(t->tm_sec) & 0x7F,              /* CH = 0 -> годинник іде */
        bin2bcd(t->tm_min),
        bin2bcd(t->tm_hour),                    /* 24-годинний режим */
        bin2bcd(t->tm_wday + 1),                /* 1..7 */
        bin2bcd(t->tm_mday),
        bin2bcd(t->tm_mon + 1),
        bin2bcd(t->tm_year - 100),              /* 2000..2099 */
    };
    return i2c_master_transmit(s_rtc, buf, sizeof(buf), 100);
}

/* Зчитування часу. Повертає ESP_ERR_INVALID_STATE, якщо годинник зупинений (CH=1) */
static esp_err_t rtc_read_time(struct tm *t)
{
    uint8_t reg = DS1307_REG_SEC;
    uint8_t d[7];

    esp_err_t err = i2c_master_transmit(s_rtc, &reg, 1, 200);
    if (err != ESP_OK) return err;

    err = i2c_master_receive(s_rtc, d, sizeof(d), 200);
    if (err != ESP_OK) return err;

    if (d[0] & 0x80) return ESP_ERR_INVALID_STATE;

    memset(t, 0, sizeof(*t));
    t->tm_sec  = bcd2bin(d[0] & 0x7F);
    t->tm_min  = bcd2bin(d[1] & 0x7F);
    t->tm_hour = bcd2bin(d[2] & 0x3F);
    t->tm_mday = bcd2bin(d[4] & 0x3F);
    t->tm_mon  = bcd2bin(d[5] & 0x1F) - 1;
    t->tm_year = bcd2bin(d[6]) + 100;

    /* День тижня обчислюємо самі (регістр DS1307 може бути невірним) */
    struct tm tmp = *t;
    tmp.tm_isdst = -1;
    mktime(&tmp);
    t->tm_wday = tmp.tm_wday;
    t->tm_yday = tmp.tm_yday;
    return ESP_OK;
}

/* Встановлення часу компіляції (використовується, якщо годинник зупинений) */
static void rtc_set_from_build_time(void)
{
    static const char *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char mon[4] = {0};
    int d, y, hh, mm, ss;
    sscanf(__DATE__, "%3s %d %d", mon, &d, &y);
    sscanf(__TIME__, "%d:%d:%d", &hh, &mm, &ss);

    struct tm t = {
        .tm_sec = ss, .tm_min = mm, .tm_hour = hh,
        .tm_mday = d, .tm_mon = (strstr(months, mon) - months) / 3,
        .tm_year = y - 1900, .tm_isdst = -1,
    };
    mktime(&t);                                  /* заповнює tm_wday */
    ESP_ERROR_CHECK(rtc_write_time(&t));
    ESP_LOGW(TAG, "RTC був зупинений -> встановлено час збірки: %s %s", __DATE__, __TIME__);
}

static void rtc_init(void)
{
    i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = DS1307_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_bus, &dev, &s_rtc));

    struct tm t;
    esp_err_t err = rtc_read_time(&t);
    if (err == ESP_ERR_INVALID_STATE) {
        rtc_set_from_build_time();
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "DS1307 не відповідає: %s", esp_err_to_name(err));
    }
}

/* ====================================================================
 *  Дисплей SSD1306 (esp_lcd + LVGL)
 * ==================================================================== */
static void display_init(void)
{
    /* 1. Панель I2C IO */
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = {
        .dev_addr = OLED_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
        .control_phase_bytes = 1,
        .dc_bit_offset = 6,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(s_bus, &io_cfg, &io));

    /* 2. Драйвер SSD1306 */
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_ssd1306_config_t ssd_cfg = { .height = OLED_H };
    esp_lcd_panel_dev_config_t panel_cfg = {
        .bits_per_pixel = 1,
        .reset_gpio_num = GPIO_NUM_NC,
        .vendor_config = &ssd_cfg,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(io, &panel_cfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

    /* 3. LVGL */
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io,
        .panel_handle = panel,
        .buffer_size = OLED_W * OLED_H,
        .double_buffer = true,
        .hres = OLED_W,
        .vres = OLED_H,
        .monochrome = true,
        .rotation = { .swap_xy = false, .mirror_x = false, .mirror_y = false },
    };
    lv_disp_t *disp = lvgl_port_add_disp(&disp_cfg);
    ESP_ERROR_CHECK(disp ? ESP_OK : ESP_FAIL);

    /* 4. Інтерфейс: час і дата зліва, кільце блоків справа */
    lvgl_port_lock(0);
    lv_obj_t *scr = lv_disp_get_scr_act(disp);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Час і дата: центровані по осі TEXT_CX, група - по центру екрана по вертикалі.
     * Ширина написів - за вмістом, LVGL перераховує позицію при зміні тексту. */
    const int grp_h = TIME_LINE_H + TEXT_GAP + DATE_LINE_H;
    const int grp_y = (OLED_H - grp_h) / 2;

    s_lbl_time = lv_label_create(scr);
    lv_obj_set_style_text_font(s_lbl_time, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_lbl_time, lv_color_white(), 0);
    lv_label_set_text(s_lbl_time, "--:--:--");
    lv_obj_align(s_lbl_time, LV_ALIGN_TOP_MID, TEXT_CX - OLED_W / 2, grp_y);

    s_lbl_date = lv_label_create(scr);
    lv_obj_set_style_text_font(s_lbl_date, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_lbl_date, lv_color_white(), 0);
    lv_label_set_text(s_lbl_date, "--- --.--.----");
    lv_obj_align(s_lbl_date, LV_ALIGN_TOP_MID, TEXT_CX - OLED_W / 2,
                 grp_y + TIME_LINE_H + TEXT_GAP);

    /* Кільце з 6 дуг. Кут LVGL: 0° = праворуч, зростає за годинниковою стрілкою.
     * Сегмент 0 починається зверху (270°), далі за годинниковою стрілкою. */
    const int seg = 360 / RING_SEGMENTS;
    for (int i = 0; i < RING_SEGMENTS; i++) {
        uint16_t start = (270 + i * seg + RING_GAP_DEG / 2) % 360;
        uint16_t end   = (start + seg - RING_GAP_DEG) % 360;

        lv_obj_t *a = lv_arc_create(scr);
        lv_obj_remove_style_all(a);                       /* без теми, без ручки */
        lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(a, RING_D, RING_D);
        lv_obj_set_pos(a, RING_CX - RING_D / 2, RING_CY - RING_D / 2);

        /* Фон: тонка лінія, щоб було видно весь контур кільця */
        lv_obj_set_style_arc_width(a, RING_TRACK_W, LV_PART_MAIN);
        lv_obj_set_style_arc_color(a, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_arc_opa(a, LV_OPA_COVER, LV_PART_MAIN);

        /* Індикатор: товстий сегмент, видимий тільки коли горить */
        lv_obj_set_style_arc_width(a, RING_W, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(a, lv_color_white(), LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_INDICATOR);

        lv_arc_set_bg_angles(a, start, end);
        lv_arc_set_angles(a, start, end);
        s_ring[i] = a;
    }
    lvgl_port_unlock();
}

static void display_update(const struct tm *t)
{
    char time_str[16];
    char date_str[24];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", t);       /* 15:33:59 */
    strftime(date_str, sizeof(date_str), "%a %d.%m.%Y", t);    /* Sat 14.02.2026 */

    /* Сегмент i загоряється на секунді i*10 і горить до кінця хвилини */
    int lit = t->tm_sec / 10;                                  /* 0..5 */

    if (lvgl_port_lock(100)) {
        lv_label_set_text(s_lbl_time, time_str);
        lv_label_set_text(s_lbl_date, date_str);
        for (int i = 0; i < RING_SEGMENTS; i++) {
            lv_obj_set_style_arc_opa(s_ring[i], i <= lit ? LV_OPA_COVER : LV_OPA_TRANSP,
                                     LV_PART_INDICATOR);
        }
        lvgl_port_unlock();
    }
}

static void i2c_scan(void)
{
    for (uint8_t a = 0x08; a < 0x78; a++) {
        if (i2c_master_probe(s_bus, a, 50) == ESP_OK) {
            ESP_LOGI(TAG, "Знайдено пристрій: 0x%02X", a);
        }
    }
}

/* ====================================================================
 *  main
 * ==================================================================== */
void app_main(void)
{
    i2c_bus_init();
    i2c_scan();
    rtc_init();
    display_init();

    struct tm now, last = { .tm_sec = -1 };
    while (1) {
        esp_err_t err = rtc_read_time(&now);
        if (err == ESP_OK) {
            if (now.tm_sec != last.tm_sec) {     /* оновлюємо рівно при зміні секунди */
                display_update(&now);
                last = now;
            }
        } else {
            ESP_LOGE(TAG, "Помилка читання RTC: %s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(200));          /* опитування 5 разів/с -> ≥1 оновлення/с */
    }
}
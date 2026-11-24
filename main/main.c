// SSD1306 128x64 по I2C, ESP32-S3, ESP-IDF v5.x (новий драйвер i2c_master)
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

static const char *TAG = "SSD1306";

// Налаштування апаратної шини I2C
#define I2C_SDA_GPIO   6      // Пін даних SDA (змініть під свою плату)
#define I2C_SCL_GPIO   7      // Пін тактування SCL
#define I2C_FREQ_HZ    400000 // Частота шини: 400 кГц (Fast Mode)
#define SSD1306_ADDR   0x3C   // 7-бітна I2C-адреса дисплея (драйвер сам додасть біт R/W)

// Роздільна здатність екрана та організація пам'яті
#define OLED_W 128            // Ширина дисплея в пікселях
#define OLED_H 64             // Висота дисплея в пікселях
#define OLED_PAGES (OLED_H / 8) // Кількість сторінок пам'яті (64 / 8 = 8 сторінок по 8 рядків пікселів)

// Керуючі байти протоколу SSD1306 (перший байт після адреси пристрою)
#define CTRL_CMD  0x00   // Co=0, D/C#=0 -> наступні байти є командами налаштування
#define CTRL_DATA 0x40   // Co=0, D/C#=1 -> наступні байти є графічними даними для відеопам'яті (GDDRAM)

static i2c_master_dev_handle_t oled;             // Дескриптор (handle) зареєстрованого пристрою на шині
static uint8_t fb[OLED_W * OLED_PAGES];          // Локальний кадровий буфер у RAM мікроконтролера (1024 байти)
static uint8_t tx[1 + OLED_W * OLED_PAGES];      // Буфер відправки по I2C: 1 керуючий байт (0x40) + 1024 байти кадру

// ---------- низький рівень (обмін по I2C) ----------

// Відправка однієї однобайтової команди дисплею
static esp_err_t oled_cmd(uint8_t c)
{
    uint8_t buf[2] = {CTRL_CMD, c};
    // Передаємо 2 байти з таймаутом очікування 100 мс
    return i2c_master_transmit(oled, buf, 2, 100);
}

// Відправка команди разом з її аргументами (наприклад, команда + 1 або 2 байти параметрів)
static esp_err_t oled_cmds(const uint8_t *c, size_t n)
{
    uint8_t buf[16];
    buf[0] = CTRL_CMD;          // Повідомляємо контролер дисплея, що далі йдуть команди
    memcpy(&buf[1], c, n);      // Копіюємо послідовність команд у буфер
    return i2c_master_transmit(oled, buf, n + 1, 100);
}

// Початкова ініціалізація контролера SSD1306
static void oled_init(void)
{
    static const uint8_t init_seq[] = {
        0xAE,             // Display OFF — вимкнути екран на час налаштування
        0xD5, 0x80,       // Set Display Clock Divide Ratio — стандартна частота тактування генератора
        0xA8, 0x3F,       // Set Multiplex Ratio — висота екрана (0x3F = 63 для 64 рядків, 0x1F для 32)
        0xD3, 0x00,       // Set Display Offset — вертикальний зсув зображення = 0
        0x40,             // Set Display Start Line — початковий рядок відображення = 0
        0x8D, 0x14,       // Charge Pump Setting — увімкнути внутрішній перетворювач напруги (обов'язково для живлення OLED)
        0x20, 0x00,       // Memory Addressing Mode — горизонтальний режим адресації (автоперехід по стовпцях і сторінках)
        0xA1,             // Segment Re-map — віддзеркалити по горизонталі (стовпець 127 стає SEG0)
        0xC8,             // COM Output Scan Direction — сканування зверху вниз (від COM63 до COM0)
        0xDA, 0x12,       // Set COM Pins Hardware Configuration — апаратна конфігурація виводів (0x12 для 128x64, 0x02 для 128x32)
        0x81, 0xCF,       // Set Contrast Control — яскравість/контрастність (0x00..0xFF)
        0xD9, 0xF1,       // Set Pre-charge Period — період попереднього заряду пікселів
        0xDB, 0x40,       // Set VCOMH Deselect Level — рівень напруги вимкнення пікселів
        0xA4,             // Entire Display ON — виводити зображення з відеопам'яті RAM (0xA5 засвітить усі пікселі)
        0xA6,             // Set Normal Display — звичайний режим (1 = піксель світиться, 0 = вимкнений)
        0xAF              // Display ON — увімкнути екран після завершення налаштувань
    };

    // Розбираємо масив і відправляємо кожну команду (1 байт або 2 байти з аргументом) окремою транзакцією
    size_t i = 0;
    while (i < sizeof(init_seq)) {
        uint8_t c = init_seq[i];
        size_t len = 1;
        // Перелік двобайтових команд, які потребують 1 байт параметра після коду команди
        if (c == 0xD5 || c == 0xA8 || c == 0xD3 || c == 0x8D || c == 0x20 ||
            c == 0xDA || c == 0x81 || c == 0xD9 || c == 0xDB) {
            len = 2;
        }
        ESP_ERROR_CHECK(oled_cmds(&init_seq[i], len));
        i += len;
    }
}

// Відправка всього кадрового буфера (fb) у відеопам'ять дисплея (GDDRAM)
static void oled_flush(void)
{
    const uint8_t col[] = {0x21, 0, OLED_W - 1};      // Задаємо діапазон стовпців: від 0 до 127
    const uint8_t page[] = {0x22, 0, OLED_PAGES - 1}; // Задаємо діапазон сторінок: від 0 до 7
    oled_cmds(col, sizeof(col));
    oled_cmds(page, sizeof(page));

    tx[0] = CTRL_DATA;                                // Вказуємо, що далі йдуть піксельні дані
    memcpy(&tx[1], fb, sizeof(fb));                   // Копіюємо 1024 байти кадру слідом за керуючим байтом
    // Передаємо весь кадр однією I2C-транзакцією (таймаут 1000 мс, оскільки пакет великий)
    ESP_ERROR_CHECK(i2c_master_transmit(oled, tx, sizeof(tx), 1000));
}

// Поворот зображення на 180° (додаткова команда)
// flipped = false -> 0xA1 + 0xC8 (як в oled_init, звичайна орієнтація)
// flipped = true  -> 0xA0 + 0xC0 (зображення перевернуте на 180°)
static void oled_set_rotation(bool flipped)
{
    ESP_ERROR_CHECK(oled_cmd(flipped ? 0xA0 : 0xA1)); // Segment Re-map: напрямок стовпців (SEG0 = стовпець 0 або 127)
    ESP_ERROR_CHECK(oled_cmd(flipped ? 0xC0 : 0xC8)); // COM Output Scan Direction: напрямок рядків (COM0 -> COM63 або навпаки)
    // Segment Re-map діє лише на нові дані (datasheet, розділ 10.1.12),
    // тому треба повторно надіслати кадр у GDDRAM
    oled_flush();
}

// ---------- графіка (робота з локальним буфером fb) ----------

// Встановлення або очищення одного пікселя з координатами (x, y)
static void px(int x, int y, bool on)
{
    // Ігноруємо координати, що виходять за межі екрана
    if (x < 0 || x >= OLED_W || y < 0 || y >= OLED_H) return;
    // Кожен байт буфера відповідає за вертикальний стовпчик із 8 пікселів (1 сторінка)
    if (on) fb[x + (y / 8) * OLED_W] |=  (1 << (y % 8)); // Вмикаємо потрібний біт
    else    fb[x + (y / 8) * OLED_W] &= ~(1 << (y % 8)); // Вимикаємо потрібний біт
}

// Компактний растровий шрифт 5x7 пікселів (кожен байт — вертикальний стовпець, біт 0 — верхній піксель)
typedef struct { char ch; uint8_t col[5]; } glyph_t;
static const glyph_t font[] = {
    {' ', {0x00,0x00,0x00,0x00,0x00}},
    {'0', {0x3E,0x51,0x49,0x45,0x3E}},
    {'1', {0x00,0x42,0x7F,0x40,0x00}},
    {'2', {0x42,0x61,0x51,0x49,0x46}},
    {'3', {0x21,0x41,0x45,0x4B,0x31}},
    {'6', {0x3C,0x4A,0x49,0x49,0x30}},
    {'C', {0x3E,0x41,0x41,0x41,0x22}},
    {'D', {0x7F,0x41,0x41,0x22,0x1C}},
    {'I', {0x00,0x41,0x7F,0x41,0x00}},
    {'K', {0x7F,0x08,0x14,0x22,0x41}},
    {'O', {0x3E,0x41,0x41,0x41,0x3E}},
    {'S', {0x46,0x49,0x49,0x49,0x31}},
    {'Z', {0x0F,0x78,0x1C,0x78,0x0F}},
};

// Малювання одного символу за координатами верхнього лівого кута (x, y)
static void draw_char(int x, int y, char c)
{
    for (size_t g = 0; g < sizeof(font) / sizeof(font[0]); g++) {
        if (font[g].ch != c) continue;
        // Проходимо по 5 стовпцях і 7 рядках символу
        for (int cx = 0; cx < 5; cx++)
            for (int cy = 0; cy < 7; cy++)
                px(x + cx, y + cy, font[g].col[cx] & (1 << cy));
        return;
    }
}

// Виведення рядка тексту (крок 6 пікселів: 5 пікселів символ + 1 піксель пробіл між літерами)
static void draw_text(int x, int y, const char *s)
{
    for (; *s; s++, x += 6) draw_char(x, y, *s);
}

// Малювання контуру прямокутника за координатами протилежних кутів
static void draw_rect(int x0, int y0, int x1, int y1)
{
    for (int x = x0; x <= x1; x++) { px(x, y0, 1); px(x, y1, 1); } // Верхня та нижня грані
    for (int y = y0; y <= y1; y++) { px(x0, y, 1); px(x1, y, 1); } // Ліва та права грані
}

// ---------- головна функція програми ----------
void app_main(void)
{
    // 1. Конфігурація та створення шини I2C Master
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .glitch_ignore_cnt = 7,                 // Фільтрація коротких імпульсних завад на лінії
        .flags.enable_internal_pullup = true,   // Внутрішня підтяжка GPIO (для надійності краще додати зовнішні 4.7 кОм)
    };
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    // 2. Перевірка наявності дисплея на шині (очікуємо ACK від адреси 0x3C протягом 100 мс)
    ESP_ERROR_CHECK(i2c_master_probe(bus, SSD1306_ADDR, 100));
    ESP_LOGI(TAG, "SSD1306 знайдено за адресою 0x%02X", SSD1306_ADDR);

    // 3. Реєстрація дисплея як пристрою на створеній шині I2C
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SSD1306_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &oled));

    // 4. Ініціалізація дисплея та малювання стартового кадру
    oled_init();

    memset(fb, 0, sizeof(fb));                      // Очищаємо кадровий буфер (усі пікселі вимкнені)
    draw_rect(0, 0, OLED_W - 1, OLED_H - 1);        // Малюємо рамку по периметру екрана
    draw_text(20, 20, "SSD1306 I2C OK Z");            // Виводимо текстовий напис
    // Малюємо декоративну пунктирну/східчасту лінію під текстом
    //for (int x = 4; x < OLED_W - 4; x++) px(x, 40 + (x % 8 < 4 ? 0 : 1), 1);
    oled_flush();                                   // Відправляємо сформований кадр на екран

    // 5. Нескінченний цикл: поворот зображення на 180° кожні 2 секунди
    bool flipped = false;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        flipped = !flipped;
        oled_set_rotation(flipped);
        ESP_LOGI(TAG, "Орієнтація: %s", flipped ? "180°" : "0°");
    }
}
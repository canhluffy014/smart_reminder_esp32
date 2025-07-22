#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/spi_common.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "string.h"
#include "time.h"
#include "esp_sntp.h"
#include "esp_err.h"
#include "stdint.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "driver/ledc.h"
#include "driver/adc.h"

/* Pin Definitions */
#define PIN_NUM_MISO   -1
#define PIN_NUM_MOSI   23
#define PIN_NUM_CLK    18
#define PIN_NUM_CS     5
#define PIN_NUM_DC     2
#define PIN_NUM_RST    4
#define PIN_NUM_BL     15

/* Screen Dimensions */
#define TFT_WIDTH   128
#define TFT_HEIGHT  160

/* Colors (RGB565) */
#define COLOR_BLACK  0x0000
#define COLOR_WHITE  0xFFFF
#define COLOR_RED    0xF800
#define COLOR_GREEN  0x07E0
#define COLOR_BLUE   0x001F
#define COLOR_YELLOW 0xFFE0

/* Font Definitions */
#define FONT_WIDTH   5
#define FONT_HEIGHT  8
#define FONT_SPACING 1
#define FONT_NUM_CHARS 95

#define ST7735_NOP     0x00
#define ST7735_SWRESET 0x01
#define ST7735_SLPOUT  0x11
#define ST7735_DISPON  0x29
#define ST7735_COLMOD  0x3A
#define ST7735_MADCTL  0x36
#define TAG "TFT"

static spi_device_handle_t spi;
const uint8_t font[] = {
    0x00,0x00,0x00,0x00,0x00, // ' '
    0x00,0x00,0x5F,0x00,0x00, // '!'
    0x00,0x07,0x00,0x07,0x00, // '"'
    0x14,0x7F,0x14,0x7F,0x14, // '#'
    0x24,0x2A,0x7F,0x2A,0x12, // '$'
    0x23,0x13,0x08,0x64,0x62, // '%'
    0x36,0x49,0x55,0x22,0x50, // '&'
    0x00,0x05,0x03,0x00,0x00, // '''
    0x00,0x1C,0x22,0x41,0x00, // '('
    0x00,0x41,0x22,0x1C,0x00, // ')'
    0x14,0x08,0x3E,0x08,0x14, // '*'
    0x08,0x08,0x3E,0x08,0x08, // '+'
    0x00,0x50,0x30,0x00,0x00, // ','
    0x08,0x08,0x08,0x08,0x08, // '-'
    0x00,0x60,0x60,0x00,0x00, // '.'
    0x20,0x10,0x08,0x04,0x02, // '/'
    0x3E,0x51,0x49,0x45,0x3E, // '0'
    0x00,0x42,0x7F,0x40,0x00, // '1'
    0x42,0x61,0x51,0x49,0x46, // '2'
    0x21,0x41,0x45,0x4B,0x31, // '3'
    0x18,0x14,0x12,0x7F,0x10, // '4'
    0x27,0x45,0x45,0x45,0x39, // '5'
    0x3C,0x4A,0x49,0x49,0x30, // '6'
    0x01,0x71,0x09,0x05,0x03, // '7'
    0x36,0x49,0x49,0x49,0x36, // '8'
    0x06,0x49,0x49,0x29,0x1E, // '9'
    0x00,0x36,0x36,0x00,0x00, // ':'
    0x00,0x56,0x36,0x00,0x00, // ';'
    0x08,0x14,0x22,0x41,0x00, // '<'
    0x14,0x14,0x14,0x14,0x14, // '='
    0x00,0x41,0x22,0x14,0x08, // '>'
    0x02,0x01,0x51,0x09,0x06, // '?'
    0x32,0x49,0x79,0x41,0x3E, // '@'
    0x7E,0x11,0x11,0x11,0x7E, // 'A'
    0x7F,0x49,0x49,0x49,0x36, // 'B'
    0x3E,0x41,0x41,0x41,0x22, // 'C'
    0x7F,0x41,0x41,0x22,0x1C, // 'D'
    0x7F,0x49,0x49,0x49,0x41, // 'E'
    0x7F,0x09,0x09,0x09,0x01, // 'F'
    0x3E,0x41,0x49,0x49,0x7A, // 'G'
    0x7F,0x08,0x08,0x08,0x7F, // 'H'
    0x00,0x41,0x7F,0x41,0x00, // 'I'
    0x20,0x40,0x41,0x3F,0x01, // 'J'
    0x7F,0x08,0x14,0x22,0x41, // 'K'
    0x7F,0x40,0x40,0x40,0x40, // 'L'
    0x7F,0x02,0x0C,0x02,0x7F, // 'M'
    0x7F,0x04,0x08,0x10,0x7F, // 'N'
    0x3E,0x41,0x41,0x41,0x3E, // 'O'
    0x7F,0x09,0x09,0x09,0x06, // 'P'
    0x3E,0x41,0x51,0x21,0x5E, // 'Q'
    0x7F,0x09,0x19,0x29,0x46, // 'R'
    0x46,0x49,0x49,0x49,0x31, // 'S'
    0x01,0x01,0x7F,0x01,0x01, // 'T'
    0x3F,0x40,0x40,0x40,0x3F, // 'U'
    0x1F,0x20,0x40,0x20,0x1F, // 'V'
    0x3F,0x40,0x38,0x40,0x3F, // 'W'
    0x63,0x14,0x08,0x14,0x63, // 'X'
    0x07,0x08,0x70,0x08,0x07, // 'Y'
    0x61,0x51,0x49,0x45,0x43, // 'Z'
    0x00,0x7F,0x41,0x41,0x00, // '['
    0x02,0x04,0x08,0x10,0x20, // '\'
    0x00,0x41,0x41,0x7F,0x00, // ']'
    0x04,0x02,0x01,0x02,0x04, // '^'
    0x40,0x40,0x40,0x40,0x40, // '_'
    0x00,0x01,0x02,0x04,0x00, // '`'
    0x20,0x54,0x54,0x54,0x78, // 'a'
    0x7F,0x48,0x44,0x44,0x38, // 'b'
    0x38,0x44,0x44,0x44,0x20, // 'c'
    0x38,0x44,0x44,0x48,0x7F, // 'd'
    0x38,0x54,0x54,0x54,0x18, // 'e'
    0x08,0x7E,0x09,0x01,0x02, // 'f'
    0x0C,0x52,0x52,0x52,0x3E, // 'g'
    0x7F,0x08,0x04,0x04,0x78, // 'h'
    0x00,0x44,0x7D,0x40,0x00, // 'i'
    0x20,0x40,0x44,0x3D,0x00, // 'j'
    0x7F,0x10,0x28,0x44,0x00, // 'k'
    0x00,0x41,0x7F,0x40,0x00, // 'l'
    0x7C,0x04,0x18,0x04,0x78, // 'm'
    0x7C,0x08,0x04,0x04,0x78, // 'n'
    0x38,0x44,0x44,0x44,0x38, // 'o'
    0x7C,0x14,0x14,0x14,0x08, // 'p'
    0x08,0x14,0x14,0x18,0x7C, // 'q'
    0x7C,0x08,0x04,0x04,0x08, // 'r'
    0x48,0x54,0x54,0x54,0x20, // 's'
    0x04,0x3F,0x44,0x40,0x20, // 't'
    0x3C,0x40,0x40,0x20,0x7C, // 'u'
    0x1C,0x20,0x40,0x20,0x1C, // 'v'
    0x3C,0x40,0x30,0x40,0x3C, // 'w'
    0x44,0x28,0x10,0x28,0x44, // 'x'
    0x0C,0x50,0x50,0x50,0x3C, // 'y'
    0x44,0x64,0x54,0x4C,0x44, // 'z'
    0x00,0x08,0x36,0x41,0x00, // '{'
    0x00,0x00,0x7F,0x00,0x00, // '|'
    0x00,0x41,0x36,0x08,0x00, // '}'
    0x08,0x08,0x2A,0x1C,0x08, // '~'
};

static const uint8_t* font_table[FONT_NUM_CHARS];

static void init_font_table() {
    for (int i = 0; i < FONT_NUM_CHARS; i++) {
        font_table[i] = &font[i * FONT_WIDTH];
    }
}

void send_cmd(uint8_t cmd) {
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
        .user = (void*)0,
    };
    spi_device_polling_transmit(spi, &t);
}

void send_data(uint8_t *data, uint16_t len) {
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
        .user = (void*)1,
    };
    spi_device_polling_transmit(spi, &t);
}

void set_addr_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    uint8_t data[4];
    send_cmd(0x2A);
    data[0] = x0 >> 8; data[1] = x0 & 0xFF;
    data[2] = x1 >> 8; data[3] = x1 & 0xFF;
    send_data(data, 4);

    send_cmd(0x2B);
    data[0] = y0 >> 8; data[1] = y0 & 0xFF;
    data[2] = y1 >> 8; data[3] = y1 & 0xFF;
    send_data(data, 4);

    send_cmd(0x2C);
}

void test_gpio() {
    ESP_LOGI("TEST", "GPIO DC=%d, RST=%d, BL=%d", PIN_NUM_DC, PIN_NUM_RST, PIN_NUM_BL);
}

void lcd_spi_pre_transfer_callback(spi_transaction_t *t) {
    int dc = (int)t->user;
    gpio_set_level(PIN_NUM_DC, dc);
}

void init_spi() {
    spi_bus_config_t buscfg = {
        .miso_io_num = PIN_NUM_MISO,
        .mosi_io_num = PIN_NUM_MOSI,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 20 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = PIN_NUM_CS,
        .queue_size = 7,
        .pre_cb = lcd_spi_pre_transfer_callback,  // required for DC pin!
    };

    ESP_ERROR_CHECK(spi_bus_initialize(HSPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
    ESP_ERROR_CHECK(spi_bus_add_device(HSPI_HOST, &devcfg, &spi));
}

void draw_pixel(uint16_t x, uint16_t y, uint16_t color) {
    if (x >= TFT_WIDTH || y >= TFT_HEIGHT) return;
    uint8_t data[2] = {color >> 8, color & 0xFF};
    set_addr_window(x, y, x+1, y+1);
    send_data(data, 2);
}

void draw_char(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg) {
    if (c < 32 || c >= 32 + FONT_NUM_CHARS) return;
    const uint8_t *chr = font_table[c - 32];
    for (uint8_t i = 0; i < FONT_WIDTH; i++) {
        uint8_t line = chr[i];
        for (uint8_t j = 0; j < FONT_HEIGHT; j++) {
            if (line & 0x1) {
                draw_pixel(x+i, y+j, color);
            } else if (bg != color) {
                draw_pixel(x+i, y+j, bg);
            }
            line >>= 1;
        }
    }
}

void draw_string(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg) {
    while (*str) {
        draw_char(x, y, *str++, color, bg);
        x += FONT_WIDTH + FONT_SPACING;
        if (x > TFT_WIDTH - FONT_WIDTH) {
            x = 0;
            y += FONT_HEIGHT;
        }
    }
}

void fill_screen(uint16_t color) {
    uint8_t hi = color >> 8, lo = color & 0xFF;
    set_addr_window(0, 0, TFT_WIDTH-1, TFT_HEIGHT-1);
    for (int i = 0; i < TFT_WIDTH * TFT_HEIGHT; i++) {
        send_data(&hi, 1);
        send_data(&lo, 1);
    }
}

void init_display() {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_NUM_DC) | (1ULL << PIN_NUM_RST) | (1ULL << PIN_NUM_BL),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "GPIO config: OK");
    }
    ESP_ERROR_CHECK(ret);
    gpio_set_level(PIN_NUM_BL, 1);
    ESP_LOGI(TAG, "Backlight set to HIGH (GPIO %d)", PIN_NUM_BL);
    test_gpio();
    ESP_LOGI(TAG, "Resetting display (GPIO %d)", PIN_NUM_RST);
    gpio_set_level(PIN_NUM_RST, 0);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    gpio_set_level(PIN_NUM_RST, 1);
    vTaskDelay(120 / portTICK_PERIOD_MS);
    
    init_spi();
    send_cmd(ST7735_NOP);
    send_cmd(ST7735_SWRESET);
    vTaskDelay(150 / portTICK_PERIOD_MS);

    send_cmd(ST7735_SLPOUT);
    vTaskDelay(120 / portTICK_PERIOD_MS);

    // Frame Rate Control (ST7735S)
    send_cmd(0xB1);
    send_data((uint8_t[]){0x05, 0x3C, 0x3C}, 3);
    send_cmd(0xB2);
    send_data((uint8_t[]){0x05, 0x3C, 0x3C}, 3);
    send_cmd(0xB3);
    send_data((uint8_t[]){0x05, 0x3C, 0x3C, 0x05, 0x3C, 0x3C}, 6);

    // Inverter Control
    send_cmd(0xB4);
    send_data((uint8_t[]){0x03}, 1);

    // Power Control
    send_cmd(0xC0);
    send_data((uint8_t[]){0xA2, 0x02, 0x84}, 3);
    send_cmd(0xC1);
    send_data((uint8_t[]){0xC5}, 1);
    send_cmd(0xC2);
    send_data((uint8_t[]){0x0A, 0x00}, 2);
    send_cmd(0xC3);
    send_data((uint8_t[]){0x8A, 0x2A}, 2);
    send_cmd(0xC4);
    send_data((uint8_t[]){0x8A, 0xEE}, 2);
    send_cmd(0xC5);
    send_data((uint8_t[]){0x0E}, 1);
    send_cmd(ST7735_COLMOD);
    send_data((uint8_t[]){0x05}, 1);
    send_cmd(ST7735_MADCTL);
    send_data((uint8_t[]){0xC0}, 1); // RGB, xoay khác (thử 0x08, 0x48, 0x88 nếu cần)
    send_cmd(0xE0);
    send_data((uint8_t[]){0x0F, 0x1A, 0x0F, 0x18, 0x2F, 0x28, 0x20, 0x22,
                          0x1F, 0x1B, 0x23, 0x37, 0x00, 0x07, 0x02, 0x10}, 16);
    send_cmd(0xE1);
    send_data((uint8_t[]){0x0F, 0x1B, 0x0F, 0x17, 0x33, 0x2C, 0x29, 0x2E,
                          0x30, 0x30, 0x39, 0x3F, 0x00, 0x07, 0x03, 0x10}, 16);
    send_cmd(ST7735_DISPON);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "Display initialized successfully");
}

void initialize_sntp() {
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
}

void app_main() {
    init_font_table();
    init_display();
    initialize_sntp();

    fill_screen(COLOR_BLACK);
    draw_string(0, 0, "Lich nhac!", COLOR_YELLOW, COLOR_BLACK);
    draw_string(0, 12, "Uong thuoc luc: ", COLOR_WHITE, COLOR_BLACK);

    while (1) {
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);

        char time_str[16];
        snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

        draw_string(0, 30, time_str, COLOR_GREEN, COLOR_BLACK);

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

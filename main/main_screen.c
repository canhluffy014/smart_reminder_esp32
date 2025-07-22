#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "string.h"
#include "time.h"
#include "esp_sntp.h"

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

static spi_device_handle_t spi;
extern const uint8_t font[];
// Make font_table const since it won't change
static const uint8_t* const font_table[FONT_NUM_CHARS];

static void init_font_table() {
    for (int i = 0; i < FONT_NUM_CHARS; i++) {
        font_table[i] = &font[i * FONT_WIDTH];
    }
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

void draw_pixel(uint16_t x, uint16_t y, uint16_t color) {
    if (x >= TFT_WIDTH || y >= TFT_HEIGHT) return;
    uint8_t data[2] = {color >> 8, color & 0xFF};
    set_addr_window(x, y, x+1, y+1);
    send_data(data, 2);
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
    };
    gpio_config(&io_conf);

    gpio_set_level(PIN_NUM_RST, 0);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    gpio_set_level(PIN_NUM_RST, 1);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    gpio_set_level(PIN_NUM_BL, 1);

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
    };

    ESP_ERROR_CHECK(spi_bus_initialize(HSPI_HOST, &buscfg, 1));
    ESP_ERROR_CHECK(spi_bus_add_device(HSPI_HOST, &devcfg, &spi));

    send_cmd(0x01);
    vTaskDelay(150 / portTICK_PERIOD_MS);
    send_cmd(0x11);
    vTaskDelay(255 / portTICK_PERIOD_MS);
    send_cmd(0x3A);
    send_data((uint8_t[]){0x05}, 1);
    send_cmd(0x29);
    vTaskDelay(100 / portTICK_PERIOD_MS);
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

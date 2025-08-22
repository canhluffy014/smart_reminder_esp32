#define TAG "TimeSync"

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "font.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define PIN_NUM_MISO   -1  
#define PIN_NUM_MOSI   9   
#define PIN_NUM_CLK    10  
#define PIN_NUM_CS     11  
#define PIN_NUM_DC     8   
#define PIN_NUM_RST    18  
#define PIN_NUM_BL     17  

#define TFT_WIDTH   128
#define TFT_HEIGHT  160
#define OFFSET_X    2   
#define OFFSET_Y    1   
#define FONT_W  6
#define FONT_H  8

#define COLOR_RED    0xF800
#define COLOR_GREEN  0x07E0
#define COLOR_BLUE   0x001F
#define COLOR_BLACK  0x0000
#define COLOR_WHITE  0xFFFF

#define ST7735_NOP      0x00
#define ST7735_SWRESET  0x01
#define ST7735_SLPOUT   0x11
#define ST7735_COLMOD   0x3A
#define ST7735_MADCTL   0x36
#define ST7735_CASET    0x2A
#define ST7735_RASET    0x2B
#define ST7735_RAMWR    0x2C
#define ST7735_DISPON   0x29

static spi_device_handle_t spi;

void send_cmd(uint8_t cmd) {
    esp_err_t ret;
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
        .flags = 0
    };
    gpio_set_level(PIN_NUM_DC, 0); 
    ret = spi_device_polling_transmit(spi, &t);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Write command 0x%02X failed: %s", cmd, esp_err_to_name(ret));
    }
}

void send_data(uint8_t *data, uint16_t len) {
    if (len == 0) return;
    esp_err_t ret;
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
        .flags = 0
    };
    gpio_set_level(PIN_NUM_DC, 1); 
    ret = spi_device_polling_transmit(spi, &t);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Write data (%d bytes) failed: %s", len, esp_err_to_name(ret));
    }
}

void set_addr_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    uint8_t data[4];
    send_cmd(ST7735_CASET);
    data[0] = (x0 + OFFSET_X) >> 8; data[1] = (x0 + OFFSET_X) & 0xFF;
    data[2] = (x1 + OFFSET_X) >> 8; data[3] = (x1 + OFFSET_X) & 0xFF;
    send_data(data, 4);

    send_cmd(ST7735_RASET);
    data[0] = (y0 + OFFSET_Y) >> 8; data[1] = (y0 + OFFSET_Y) & 0xFF;
    data[2] = (y1 + OFFSET_Y) >> 8; data[3] = (y1 + OFFSET_Y) & 0xFF;
    send_data(data, 4);

    send_cmd(ST7735_RAMWR);
}

static void push_color_repeat_chunked(uint16_t color, size_t px_count) {
    static uint16_t buf[256];
    size_t chunk_px = sizeof(buf)/sizeof(buf[0]);

    uint16_t c_be = (color << 8) | (color >> 8);
    for (size_t i = 0; i < chunk_px; i++) buf[i] = c_be;

    size_t sent = 0;
    while (px_count > 0) {
        size_t n = (px_count > chunk_px) ? chunk_px : px_count;

        spi_transaction_t t = {
            .length    = (int)(n * 2 * 8),
            .tx_buffer = buf,
            .flags     = 0
        };
        gpio_set_level(PIN_NUM_DC, 1);
        esp_err_t ret = spi_device_polling_transmit(spi, &t);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "push_color_repeat_chunked fail: %s", esp_err_to_name(ret));
            return;
        }

        px_count -= n;
        sent += n;
        if ((sent & (chunk_px * 8 - 1)) == 0) {
            taskYIELD();
        }
    }
}

void fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    if (x >= TFT_WIDTH || y >= TFT_HEIGHT) return;
    if (x + w > TFT_WIDTH)  w = TFT_WIDTH  - x;
    if (y + h > TFT_HEIGHT) h = TFT_HEIGHT - y;
    set_addr_window(x, y, x + w - 1, y + h - 1);
    push_color_repeat_chunked(color, (size_t)w * h);
}

void fill_screen(uint16_t color) {
    set_addr_window(0, 0, TFT_WIDTH - 1, TFT_HEIGHT - 1);
    push_color_repeat_chunked(color, (size_t)TFT_WIDTH * TFT_HEIGHT);
}

void draw_pixel(uint16_t x, uint16_t y, uint16_t color) {
    if (x >= TFT_WIDTH || y >= TFT_HEIGHT) return;
    uint8_t data[2] = {color >> 8, color & 0xFF};
    set_addr_window(x, y, x, y);
    send_data(data, 2);
}

void draw_char(char c, int x, int y, uint16_t color) {
    const uint8_t *bitmap;
    if (c >= 'A' && c <= 'Z') {
        bitmap = font5x7[c - 'A'];
    }
    else if (c >= '0' && c <= '9') {
        bitmap = font5x7[c - '0' + 26]; 
    }
    else {
        return;
    }
    for (int col = 0; col < 5; col++) {
        for (int row = 0; row < 7; row++) {
            if (bitmap[col] & (1 << row)) {
                draw_pixel(x + col, y + row, color);
            }
        }
    }
}

void draw_string(uint16_t x, uint16_t y, const char *str, uint16_t color) {
    int count = 0;
    while (*str) {
        draw_char(*str, x, y, color);
        x += 6;
        str++;
        if ((++count & 15) == 0) {
            taskYIELD();   
        }
    }
}

void init_spi() {
    ESP_LOGI(TAG, "Initializing SPI");

    esp_err_t ret = spi_bus_free(SPI2_HOST);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SPI bus freed successfully");
    } else {
        ESP_LOGW(TAG, "SPI bus free failed: %s", esp_err_to_name(ret));
    }


    spi_bus_config_t buscfg = {
        .miso_io_num = PIN_NUM_MISO,
        .mosi_io_num = PIN_NUM_MOSI,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = TFT_WIDTH * TFT_HEIGHT * 2 + 8
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 10 * 1000 * 1000,  
        .mode = 0,                         
        .spics_io_num = PIN_NUM_CS,
        .queue_size = 10,                  
        .flags = 0,                        
        .pre_cb = NULL,
        .post_cb = NULL
    };

     ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "SPI bus init: OK");
    }
    ESP_ERROR_CHECK(ret);

    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "SPI device add: OK");
    }
    ESP_ERROR_CHECK(ret);
}

void test_gpio() {
    ESP_LOGI(TAG, "Testing GPIO signals");
    for (int i = 0; i < 3; i++) {
        gpio_set_level(PIN_NUM_DC, 1);
        gpio_set_level(PIN_NUM_RST, 1);
        ESP_LOGI(TAG, "DC and RST set to HIGH");
        vTaskDelay(500 / portTICK_PERIOD_MS);
        gpio_set_level(PIN_NUM_DC, 0);
        gpio_set_level(PIN_NUM_RST, 0);
        ESP_LOGI(TAG, "DC and RST set to LOW");
        vTaskDelay(500 / portTICK_PERIOD_MS);
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
    vTaskDelay(120 / portTICK_PERIOD_MS);;

    send_cmd(0xB1);
    send_data((uint8_t[]){0x05, 0x3C, 0x3C}, 3);
    send_cmd(0xB2);
    send_data((uint8_t[]){0x05, 0x3C, 0x3C}, 3);
    send_cmd(0xB3);
    send_data((uint8_t[]){0x05, 0x3C, 0x3C, 0x05, 0x3C, 0x3C}, 6);
    
    send_cmd(0xB4);
    send_data((uint8_t[]){0x03}, 1);

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
    send_data((uint8_t[]){0xC0}, 1); 
    
    send_cmd(0xE0);
    send_data((uint8_t[]){0x0F, 0x1A, 0x0F, 0x18, 0x2F, 0x28, 0x20, 0x22, 0x1F, 0x1B, 0x23, 0x37, 0x00, 0x07, 0x02, 0x10}, 16);                     
    send_data((uint8_t[]){0x0F, 0x1B, 0x0F, 0x17, 0x33, 0x2C, 0x29, 0x2E, 0x30, 0x30, 0x39, 0x3F, 0x00, 0x07, 0x03, 0x10}, 16);

    send_cmd(ST7735_DISPON);
    vTaskDelay(10 / portTICK_PERIOD_MS);

    ESP_LOGI(TAG, "Display initialized successfully");
}
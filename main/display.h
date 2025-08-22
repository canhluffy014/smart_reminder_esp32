#pragma once
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "font.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

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
#define COLOR_YELLOW 0xFFE0

void send_cmd(uint8_t cmd);
void send_data(uint8_t *data, uint16_t len);
void set_addr_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void fill_screen(uint16_t color);
void fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void draw_pixel(uint16_t x, uint16_t y, uint16_t color);
void draw_char(char c, int x, int y, uint16_t color);
void draw_string(uint16_t x, uint16_t y, const char *str, uint16_t color);
void init_spi(void);
void test_gpio(void);
void init_display(void);
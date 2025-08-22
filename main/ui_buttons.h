#pragma once
#define BTN_OK_PIN     7
#define BTN_BACK_PIN   16
#define BTN_NEXT_PIN   15
#define BTN_CANCEL_PIN 6
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_err.h"

typedef struct {
    int ok_edge;
    int back_edge;
    int next_edge;
    int cancel_edge;
} BtnEdges;

void buttons_init(void);
void scan_buttons(BtnEdges *e);

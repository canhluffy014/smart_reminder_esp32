#define BTN_OK_PIN     7
#define BTN_BACK_PIN   16
#define BTN_NEXT_PIN   15
#define BTN_CANCEL_PIN 6
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_err.h"

typedef struct {
    gpio_num_t pin;
    int stable;    
    int last_raw;  
    TickType_t tstamp;
} DebBtn;

static DebBtn db_ok    = {BTN_OK_PIN, 0, 1, 0};
static DebBtn db_back  = {BTN_BACK_PIN, 0, 1, 0};
static DebBtn db_next  = {BTN_NEXT_PIN, 0, 1, 0};
static DebBtn db_cancel= {BTN_CANCEL_PIN, 0, 1, 0};

void buttons_init(void) {
    gpio_config_t io = {
        .pin_bit_mask = (1ULL<<BTN_OK_PIN) | (1ULL<<BTN_BACK_PIN) | (1ULL<<BTN_NEXT_PIN) | (1ULL<<BTN_CANCEL_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&io));
}

static inline int read_btn(gpio_num_t pin) {
    return (gpio_get_level(pin) == 0); 
}

static int debounce_once(DebBtn *b, TickType_t now, int *rising_edge) {
    int raw = !gpio_get_level(b->pin); 
    *rising_edge = 0;
    if (raw != b->last_raw) {
        b->tstamp = now;
        b->last_raw = raw;
    } else {
        if ((now - b->tstamp) >= pdMS_TO_TICKS(10)) { 
            if (raw != b->stable) {
                if (raw == 1) *rising_edge = 1;
                b->stable = raw;
            }
        }
    }
    return b->stable;
}

typedef struct {
    int ok_edge;
    int back_edge;
    int next_edge;
    int cancel_edge;
} BtnEdges;

void scan_buttons(BtnEdges *e) {
    TickType_t now = xTaskGetTickCount();
    debounce_once(&db_ok, now,     &e->ok_edge);
    debounce_once(&db_back, now,   &e->back_edge);
    debounce_once(&db_next, now,   &e->next_edge);
    debounce_once(&db_cancel, now, &e->cancel_edge);
}
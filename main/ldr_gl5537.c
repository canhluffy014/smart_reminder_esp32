#include "ldr_gl5537.h"
#include "soc/gpio_num.h"
#include "esp_log.h"

static const char *TAG = "LDR_GL5537";

#define LDR_CHECK_VOID(condition, fmt, ...) \
    do { \
        if (!(condition)) { \
            ESP_LOGE(TAG, fmt, ##__VA_ARGS__); \
            return; \
        } \
    } while (0)

#define LDR_CHECK_RET(condition, ret, fmt, ...) \
    do { \
        if (!(condition)) { \
            ESP_LOGE(TAG, fmt, ##__VA_ARGS__); \
            return ret; \
        } \
    } while (0)

#define BUZZER_PULSE_COUNT 3
#define BUZZER_PULSE_DURATION_MS 200

static void buzzer_task(void *pvParameters) {
    gpio_num_t buzzer_pin = *(gpio_num_t *)pvParameters;
    for (int i = 0; i < BUZZER_PULSE_COUNT; i++) {
        gpio_set_level(buzzer_pin, 1);
        vTaskDelay(pdMS_TO_TICKS(BUZZER_PULSE_DURATION_MS));
        gpio_set_level(buzzer_pin, 0);
        vTaskDelay(pdMS_TO_TICKS(BUZZER_PULSE_DURATION_MS));
    }
    vTaskDelete(NULL);
}

esp_err_t ldr_gl5537_init(ldr_gl5537_t *ldr, gpio_num_t led_pin, gpio_num_t buzzer_pin, adc_channel_t adc_channel, SemaphoreHandle_t mutex) {
    LDR_CHECK_RET(ldr && mutex, ESP_ERR_INVALID_ARG, "Invalid LDR or mutex pointer");

    ldr->led_pin = led_pin;
    ldr->buzzer_pin = buzzer_pin;
    ldr->adc_channel = adc_channel;
    ldr->adc_handle = NULL;
    ldr->light_threshold = LIGHT_THRESHOLD;
    ldr->debounce_time_ms = DEBOUNCE_TIME_MS;
    ldr->double_swipe_time_ms = DOUBLE_SWIPE_TIME;
    ldr->scan_interval_ms = SCAN_INTERVAL_MS;
    ldr->adc_avg_samples = ADC_AVG_SAMPLES;
    ldr->swipe_count = 0;
    ldr->last_swipe_time = 0;
    ldr->led_state = 0;
    ldr->is_below_threshold = false;
    ldr->enabled = false;
    ldr->mutex = mutex;
    ldr->gesture_callback = NULL;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << led_pin) | (1ULL << buzzer_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io_conf);
    LDR_CHECK_RET(err == ESP_OK, err, "Failed to configure GPIO: %s", esp_err_to_name(err));

    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = LDR_ADC_UNIT,
        .clk_src = ADC_DIGI_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    err = adc_oneshot_new_unit(&init_config, &ldr->adc_handle);
    LDR_CHECK_RET(err == ESP_OK, err, "Failed to initialize ADC: %s", esp_err_to_name(err));

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,
    };
    err = adc_oneshot_config_channel(ldr->adc_handle, adc_channel, &config);
    if (err != ESP_OK) {
        adc_oneshot_del_unit(ldr->adc_handle);
        LDR_CHECK_RET(false, err, "Failed to configure ADC channel: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "LDR GL5537 initialized successfully");
    return ESP_OK;
}

int ldr_gl5537_read_adc(ldr_gl5537_t *ldr) {
    LDR_CHECK_RET(ldr && ldr->adc_handle, -1, "Invalid LDR or ADC handle");

    uint32_t sum = 0;
    int valid_samples = 0;
    for (int i = 0; i < ldr->adc_avg_samples; i++) {
        int adc_value;
        if (adc_oneshot_read(ldr->adc_handle, ldr->adc_channel, &adc_value) == ESP_OK) {
            sum += adc_value;
            valid_samples++;
        }
    }
    if (valid_samples == 0) {
        ESP_LOGW(TAG, "Failed to read ADC");
        return -1;
    }
    int avg_value = sum / valid_samples;
    ESP_LOGD(TAG, "LDR ADC Value: %d", avg_value);
    return avg_value;
}

void ldr_gl5537_control_outputs(ldr_gl5537_t *ldr, int led_state, int buzzer_state) {
    LDR_CHECK_VOID(ldr, "Invalid LDR pointer");

    ESP_ERROR_CHECK(gpio_set_level(ldr->led_pin, led_state));
    if (buzzer_state) {
        TaskHandle_t task_handle = NULL;
        xTaskCreate(buzzer_task, "buzzer_task", 896, &ldr->buzzer_pin, 6, &task_handle);
        LDR_CHECK_VOID(task_handle, "Failed to create buzzer task");
    }
    ESP_LOGI(TAG, "Gesture: %s, LED: %s, Buzzer: %s",
             led_state ? "Single swipe" : (buzzer_state ? "Double swipe" : "Timeout"),
             led_state ? "ON" : "OFF",
             buzzer_state ? "ON" : "OFF");
}

void ldr_gl5537_set_enabled(ldr_gl5537_t *ldr, bool enabled) {
    LDR_CHECK_VOID(ldr, "Invalid LDR pointer");
    if (xSemaphoreTake(ldr->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take mutex");
        return;
    }
    ldr->enabled = enabled;
    xSemaphoreGive(ldr->mutex);
    ESP_LOGI(TAG, "LDR scanning %s", enabled ? "enabled" : "disabled");
}

void ldr_gl5537_set_callback(ldr_gl5537_t *ldr, ldr_gesture_callback_t callback) {
    LDR_CHECK_VOID(ldr, "Invalid LDR pointer");
    if (xSemaphoreTake(ldr->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take mutex");
        return;
    }
    ldr->gesture_callback = callback;
    xSemaphoreGive(ldr->mutex);
    ESP_LOGI(TAG, "Gesture callback %s", callback ? "set" : "cleared");
}

void ldr_gl5537_handle_gesture(ldr_gl5537_t *ldr, int ldr_value) {
    LDR_CHECK_VOID(ldr, "Invalid LDR pointer");
    if (xSemaphoreTake(ldr->mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take mutex");
        return;
    }
    if (!ldr->enabled) {
        xSemaphoreGive(ldr->mutex);
        return;
    }

    if (ldr->swipe_count == 1 && ldr->led_state == 1) {
        TickType_t current_time = xTaskGetTickCount();
        if ((current_time - ldr->last_swipe_time) * portTICK_PERIOD_MS >= ldr->double_swipe_time_ms) {
            ldr->led_state = 0;
            ldr->swipe_count = 0;
            ldr_gl5537_control_outputs(ldr, ldr->led_state, 0);
            ESP_LOGI(TAG, "Timeout after %dms: LED off", ldr->double_swipe_time_ms);
            if (ldr->gesture_callback) {
                ldr->gesture_callback(0); 
            }
        }
    }

    if (ldr_value < ldr->light_threshold && !ldr->is_below_threshold) {
        vTaskDelay(pdMS_TO_TICKS(ldr->debounce_time_ms));
        ldr->is_below_threshold = true;
    }

    else if (ldr_value >= ldr->light_threshold && ldr->is_below_threshold) {
        vTaskDelay(pdMS_TO_TICKS(ldr->debounce_time_ms));
        ldr->is_below_threshold = false;
        TickType_t current_time = xTaskGetTickCount();

        if (ldr->swipe_count == 0 || 
            ((current_time - ldr->last_swipe_time) * portTICK_PERIOD_MS <= ldr->double_swipe_time_ms &&
             ldr->last_swipe_time <= current_time)) {
            ldr->swipe_count++;
            ldr->last_swipe_time = current_time;
        } else {
            ldr->swipe_count = 1;
            ldr->last_swipe_time = current_time;
        }

        if (ldr->swipe_count == 1) {
            ldr->led_state = 1;
            ldr_gl5537_control_outputs(ldr, ldr->led_state, 0);
            ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_35, 0)); 
            if (ldr->gesture_callback) {
                ldr->gesture_callback(1); 
            }
        } else if (ldr->swipe_count == 2) {
            ldr->led_state = 0;
            ldr_gl5537_control_outputs(ldr, ldr->led_state, 1);
            ldr->swipe_count = 0;
            if (ldr->gesture_callback) {
                ldr->gesture_callback(2); 
            }
        }
    }
    xSemaphoreGive(ldr->mutex);
}

void ldr_gl5537_task(void *pvParameters) {
    ldr_gl5537_t *ldr = (ldr_gl5537_t *)pvParameters;
    LDR_CHECK_VOID(ldr, "Invalid LDR pointer in task");
    while (1) {
        int ldr_value = ldr_gl5537_read_adc(ldr);
        if (ldr_value >= 0) {
            ldr_gl5537_handle_gesture(ldr, ldr_value);
        } else {
            ESP_LOGW(TAG, "ADC read failed, retrying after 100ms");
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        vTaskDelay(pdMS_TO_TICKS(ldr->scan_interval_ms));
    }
}
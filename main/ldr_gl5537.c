/*
 * ldr_gl5537.c
 *
 *  Created on: 19 thg 7, 2025
 *      Author: Lenovo
 */

#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "ldr_gl5537.h"

#define TAG "LDR_GL5537"
#define ADC_MAX_VALUE 4095 // Giá trị tối đa của ADC 12-bit trên ESP32-S3

esp_err_t ldr_gl5537_init(LDR_GL5537_t *ldr, adc1_channel_t channel) {
    if (ldr == NULL) {
        ESP_LOGE(TAG, "LDR pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    ldr->adc_channel = channel;
    ldr->reference_voltage = LDR_REFERENCE_VOLTAGE;
    ldr->pull_down_resistor = LDR_PULL_DOWN_RESISTOR;

    
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ldr->adc_channel, ADC_ATTEN_DB_11); // Phù hợp với 0-3.3V
    ESP_LOGI(TAG, "LDR initialized on ADC channel %d (GPIO%d)", channel, channel == ADC1_CHANNEL_4 ? 4 : -1);
    return ESP_OK;
}

esp_err_t ldr_gl5537_read_voltage(LDR_GL5537_t *ldr, float *voltage) {
    if (ldr == NULL || voltage == NULL) {
        ESP_LOGE(TAG, "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    
    int adc_value = adc1_get_raw(ldr->adc_channel);
    if (adc_value < 0) {
        ESP_LOGE(TAG, "Failed to read ADC");
        return ESP_FAIL;
    }

    
    *voltage = (float)adc_value * ldr->reference_voltage / ADC_MAX_VALUE;
    ESP_LOGD(TAG, "ADC value: %d, Voltage: %.2fV", adc_value, *voltage);
    return ESP_OK;
}

float ldr_gl5537_voltage_to_lux(float voltage, float reference_voltage, uint32_t pull_down_resistor) {
    if (voltage <= 0 || voltage >= reference_voltage) {
        return 0.0f; 
    }

    
    float ldr_resistance = pull_down_resistor * (reference_voltage / voltage - 1);
    if (ldr_resistance < LDR_MIN_RESISTANCE) {
        ldr_resistance = LDR_MIN_RESISTANCE;
    } else if (ldr_resistance > LDR_MAX_RESISTANCE) {
        ldr_resistance = LDR_MAX_RESISTANCE;
    }

    
    float lux = LDR_LUX_CONSTANT_1 / powf(ldr_resistance, LDR_LUX_CONSTANT_2);
    ESP_LOGD(TAG, "LDR Resistance: %.0fΩ, Lux: %.2f", ldr_resistance, lux);
    return lux;
}

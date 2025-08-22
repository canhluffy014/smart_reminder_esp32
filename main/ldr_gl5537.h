#ifndef LDR_GL5537_H
#define LDR_GL5537_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"

#define LDR_ADC_CHANNEL    ADC_CHANNEL_3  
#define LDR_ADC_UNIT       ADC_UNIT_1
#define LIGHT_THRESHOLD    2200           
#define DEBOUNCE_TIME_MS   20            
#define DOUBLE_SWIPE_TIME  1500          
#define SCAN_INTERVAL_MS   10            
#define ADC_AVG_SAMPLES    3             

typedef void (*ldr_gesture_callback_t)(int swipe_count);

typedef struct {
    adc_oneshot_unit_handle_t adc_handle; 
    adc_channel_t adc_channel;            
    gpio_num_t led_pin;                   
    gpio_num_t buzzer_pin;                
    int light_threshold;                  
    int debounce_time_ms;                 
    int double_swipe_time_ms;             
    int scan_interval_ms;                 
    int adc_avg_samples;                  
    int swipe_count;                      
    TickType_t last_swipe_time;           
    int led_state;                        
    bool is_below_threshold;              
    bool enabled;                         
    SemaphoreHandle_t mutex;             
    ldr_gesture_callback_t gesture_callback; 
} ldr_gl5537_t;

esp_err_t ldr_gl5537_init(ldr_gl5537_t *ldr, gpio_num_t led_pin, gpio_num_t buzzer_pin, adc_channel_t adc_channel, SemaphoreHandle_t mutex);

int ldr_gl5537_read_adc(ldr_gl5537_t *ldr);
void ldr_gl5537_handle_gesture(ldr_gl5537_t *ldr, int ldr_value);
void ldr_gl5537_control_outputs(ldr_gl5537_t *ldr, int led_state, int buzzer_state);
void ldr_gl5537_set_enabled(ldr_gl5537_t *ldr, bool enabled);
void ldr_gl5537_set_callback(ldr_gl5537_t *ldr, ldr_gesture_callback_t callback);
void ldr_gl5537_task(void *pvParameters);
#endif

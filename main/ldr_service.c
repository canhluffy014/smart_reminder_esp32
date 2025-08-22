#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <strings.h>
#include <time.h>
#include "esp_adc/adc_oneshot.h"
#include "driver/gpio.h"
#include "ldr_gl5537.h"

ldr_gl5537_t ldr;

void ldr_scan_task(void *pv) {
    ldr_gl5537_task((void*)&ldr); 
    vTaskDelete(NULL);
}
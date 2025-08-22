#pragma once
#include "soc/gpio_num.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include <limits.h> 
#include <strings.h>
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "display.h"                                
#include "ldr_gl5537.h"
#include "mqtt.h"
#include "send_email.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include "reminders_store.h"
#include "ui_buttons.h"
#include "ui_draw.h"
#include "ldr_service.h"
#include "time_utils.h"

void ui_task(void *pvParam);
void time_sync_notification_cb(struct timeval *tv);
void initialize_sntp(void);
void obtain_time(void);
void print_time_task(void *pvParam);
esp_err_t save_reminders_to_nvs(void);
esp_err_t load_reminders_from_nvs(void);
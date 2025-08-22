#ifndef MAIN_SEND_EMAIL_H_
#define MAIN_SEND_EMAIL_H_

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"

void send_email(void *pvParameters);

#endif 

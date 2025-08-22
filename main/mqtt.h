#ifndef MAIN_MQTT_H_
#define MAIN_MQTT_H_

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sntp.h"

#define MAX_REMINDERS 16
#define DATE_LENGTH 11   
#define CONTENT_LENGTH 64
#define STATUS_LENGTH 16 

void mqtt_app_start(void);
void mqtt_publish(const char *topic, const char *data, int qos, int retain);

#endif 

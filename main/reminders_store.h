#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_log.h"
#include "cJSON.h"
#include "nvs.h"         
#include "nvs_flash.h"
#include "mqtt.h"
#define MAX_REMINDERS 16

typedef struct {
    int  id;                
    char date[11];          
    int  hour;              
    int  minute;            
    char content[64];       
    char status[16];        
} Reminder;

extern int next_id;
extern int num_reminders;
extern int pick_index;
extern Reminder reminders[];
extern SemaphoreHandle_t reminders_mutex;

void recompute_next_id_locked(void);
void reminders_recalc(void);
void add_reminder_full(int id, const char *date, int hour, int min, const char *content, const char *status);
void add_reminder_full_nr(int id, const char *date, int hour, int min, const char *content, const char *status);
void update_reminder(int id, const char *date, int hour, int min, const char *content, const char *status);
void delete_reminder_at(int idx);
void delete_reminder_at_nr(int id);
void update_reminder_status(int id, const char *status);
void send_reminder_history(const char *content);
void sync_reminder(const char *action, int id, const char *date, const char *time, const char *content, const char *status);
esp_err_t save_reminders_to_nvs(void);
esp_err_t load_reminders_from_nvs(void);
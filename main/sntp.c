#define TAG "TimeSync"

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
#include "nvs.h"         
#include "nvs_flash.h"
#include "cJSON.h"
#include "reminders_store.h"
#include "ui_buttons.h"
#include "ui_draw.h"
#include "ldr_service.h"
#include "time_utils.h"

#define SET_STATE(S)  do { ui_state = (S); ui_epoch++; } while (0)

#ifndef LDR_LED_PIN
#define LDR_LED_PIN      GPIO_NUM_42
#endif
#ifndef LDR_BUZZER_PIN
#define LDR_BUZZER_PIN   GPIO_NUM_35
#endif
#define EV_ALARM_START  (1<<0)   
#define EV_GESTURE_DONE (1<<1) 
#define SNOOZE_SECS  (5*60)
    
static TaskHandle_t ldr_task_handle = NULL;
static bool edit_active = false;
static int edit_day = 1, edit_month = 1, edit_year = 2025; 
static int edit_hour = 0, edit_min = 0;
static SemaphoreHandle_t ldr_mutex = NULL;
static EventGroupHandle_t eg_alarm = NULL;
static volatile int ldr_cb_code = -1;
static time_t alarm_started_at = 0;
static int  alarm_index  = -1;
static time_t snooze_until = 0;
static int snooze_index = -1;
static time_t first_swipe_ts  = 0;
static int shown_hour = -1, shown_min = -1;
static int shown_y = -1, shown_m = -1, shown_d = -1;
static volatile bool  alarm_active = false;
static bool alarm_screen_visible = false;

TaskHandle_t mail_task = NULL;

static void ldr_cb(int code) { ldr_cb_code = code; }

void time_sync_notification_cb(struct timeval *tv) {
    if (tv) {
        ESP_LOGI(TAG, "Time synchronized");
    } else {
        ESP_LOGE(TAG, "SNTP callback: Invalid timeval");
    }
}

void initialize_sntp(void) {
    ESP_LOGI(TAG, "Initializing SNTP with servers: time.nist.gov, ntp.ubuntu.com, pool.ntp.org");
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "time.nist.gov");
    esp_sntp_setservername(1, "ntp.ubuntu.com");
    esp_sntp_setservername(2, "pool.ntp.org");
    esp_sntp_set_sync_interval(15000);
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP initialized successfully");
}

void obtain_time(void) {
    ESP_LOGI(TAG, "Starting SNTP sync...");
    ESP_LOGI(TAG, "Free heap before SNTP init: %lu bytes", (unsigned long)esp_get_free_heap_size());
    initialize_sntp();
    setenv("TZ", "ICT-7", 1);
    tzset();
    ESP_LOGI(TAG, "Timezone set to ICT-7");
    mqtt_app_start();
    time_t now;
    struct tm timeinfo;
    char strftime_buf[64];
    int retry = 60;
    const int retry_count = 60;
    while (retry-- > 0) {
        time(&now);
        localtime_r(&now, &timeinfo);
        if (timeinfo.tm_year > (2016 - 1900)) {
            strftime(strftime_buf, sizeof(strftime_buf), "%d.%m.%Y %H:%M:%S", &timeinfo);
            ESP_LOGI(TAG, "Time synchronized: %s", strftime_buf);
            return;
        }
        ESP_LOGI(TAG, "Waiting for SNTP sync, attempt %d/%d", retry_count - retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
    ESP_LOGE(TAG, "SNTP sync failed after %d attempts", retry_count);
}

static void ldr_event_task(void *pv) {
    ESP_LOGI(TAG, "LDR task: waiting for alarm events...");
    for (;;) {
        xEventGroupWaitBits(eg_alarm, EV_ALARM_START, pdTRUE, pdTRUE, portMAX_DELAY);
        ldr_cb_code = -1;
        time(&alarm_started_at);
        first_swipe_ts = 0;
        ldr_gl5537_set_enabled(&ldr, true); 
		gpio_set_level(LDR_BUZZER_PIN, 1);             
        time_t nowt;
        TickType_t last = xTaskGetTickCount();
        bool decided = false;
        while (!decided) {
            time(&nowt);
            if (ldr_cb_code < 0 && (nowt - alarm_started_at) >= 180) {
                xSemaphoreTake(reminders_mutex, portMAX_DELAY);
                update_reminder_status(reminders[alarm_index].id, "repeat");
                strncpy(reminders[alarm_index].status, "repeat", sizeof(reminders[alarm_index].status)-1);
                reminders[alarm_index].status[sizeof(reminders[alarm_index].status)-1] = 0;
                xSemaphoreGive(reminders_mutex);
                snooze_index = alarm_index;
                snooze_until = nowt + SNOOZE_SECS;
                if (ui_state == UI_IDLE) show_alarm_feedback("BAO LAI SAU 5 PHUT", COLOR_YELLOW);
                vTaskDelay(pdMS_TO_TICKS(900));
                gpio_set_level(LDR_BUZZER_PIN, 0);
                alarm_active = false; alarm_screen_visible = false;
                shown_hour = shown_min = -1; shown_y = shown_m = shown_d = -1;
                draw_idle_screen_now();
                decided = true;
                break;
            }
            int code = ldr_cb_code;
            if (code == 2) {
                int was_pending = 0;
                xSemaphoreTake(reminders_mutex, portMAX_DELAY);
                update_reminder_status(reminders[alarm_index].id, "repeat");
                was_pending = (strncasecmp(reminders[alarm_index].status, "pending", 7) == 0);
                strncpy(reminders[alarm_index].status, "repeat", sizeof(reminders[alarm_index].status)-1);
                reminders[alarm_index].status[sizeof(reminders[alarm_index].status)-1] = 0;
                if (was_pending) {
                    struct tm tm_now = *localtime(&nowt);
                    int total = tm_now.tm_hour*60 + tm_now.tm_min + 5;
                    reminders[alarm_index].hour   = (total/60)%24;
                    reminders[alarm_index].minute = (total%60);
                }
                xSemaphoreGive(reminders_mutex);
                snooze_index = alarm_index;
                snooze_until = nowt + SNOOZE_SECS;
                if (ui_state == UI_IDLE) show_alarm_feedback("BAO LAI SAU 5 PHUT", COLOR_YELLOW);
                vTaskDelay(pdMS_TO_TICKS(900));
                gpio_set_level(LDR_BUZZER_PIN, 0);
                alarm_active = false; alarm_screen_visible = false;
                shown_hour = shown_min = -1; shown_y = shown_m = shown_d = -1;
                draw_idle_screen_now();
                decided = true;
            }
            else if (code == 0) {
                xSemaphoreTake(reminders_mutex, portMAX_DELAY);
                update_reminder_status(reminders[alarm_index].id, "completed");
                strncpy(reminders[alarm_index].status, "completed", sizeof(reminders[alarm_index].status)-1);
                reminders[alarm_index].status[sizeof(reminders[alarm_index].status)-1] = 0;
                xSemaphoreGive(reminders_mutex);
                snooze_index = -1;
                if (ui_state == UI_IDLE) show_alarm_feedback("DA HOAN THANH", COLOR_GREEN);
                vTaskDelay(pdMS_TO_TICKS(900));
                gpio_set_level(LDR_BUZZER_PIN, 0);
                alarm_active = false; alarm_screen_visible = false;
                shown_hour = shown_min = -1; shown_y = shown_m = shown_d = -1;
                draw_idle_screen_now();
                decided = true;
            }
            vTaskDelayUntil(&last, pdMS_TO_TICKS(20));
        }
        gpio_set_level(LDR_BUZZER_PIN, 0);
        ldr_gl5537_set_enabled(&ldr, false);
        xEventGroupSetBits(eg_alarm, EV_GESTURE_DONE);
    }
}

void print_time_task(void *pvParam) {
    if (!eg_alarm)  eg_alarm  = xEventGroupCreate();
    if (!ldr_mutex) ldr_mutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(ldr_gl5537_init(&ldr, LDR_LED_PIN, LDR_BUZZER_PIN, LDR_ADC_CHANNEL, ldr_mutex));
    ldr.double_swipe_time_ms = 5000;  
    ldr.scan_interval_ms     = 20;      
    ldr_gl5537_set_callback(&ldr, ldr_cb);
    ldr_gl5537_set_enabled(&ldr, false);  
    static TaskHandle_t ldr_scan_handle = NULL;
    if (ldr_scan_handle == NULL) {
        xTaskCreatePinnedToCore(ldr_scan_task, "ldr_scan", 3072, NULL, 5, &ldr_scan_handle, 0);
    }
    if (ldr_task_handle == NULL) {
        xTaskCreatePinnedToCore(ldr_event_task, "ldr_evt", 4096, NULL, 6, &ldr_task_handle, 0);
    }
    if (!reminders_mutex) {
        reminders_mutex = xSemaphoreCreateMutex();
    }
    static bool loaded = false;
    if (!loaded) {
        if (load_reminders_from_nvs() == ESP_OK) {
            loaded = true;
        }
    }
	reminders_recalc();
    TickType_t pt_last = xTaskGetTickCount();
    ESP_LOGI(TAG, "Starting reminder task");
    int last_checked_minute = -1;
    while (1) {
        time_t now; struct tm timeinfo; char time_buf[64];
        time(&now); localtime_r(&now, &timeinfo);
        int time_synced = (timeinfo.tm_year >= (2016 - 1900));
            if (!time_synced) {
                ESP_LOGI(TAG, "CHUA DONG BO THOI GIAN");
            }
            if (time_synced && timeinfo.tm_min != last_checked_minute) {
                last_checked_minute = timeinfo.tm_min;
                char today[11]; fmt_date(timeinfo.tm_year+1900, timeinfo.tm_mon+1, timeinfo.tm_mday, today);
                bool took=false, released=false;
                if (reminders_mutex) { xSemaphoreTake(reminders_mutex, portMAX_DELAY); took=true; }
                for (int i=0; i<num_reminders; i++) {
                    bool is_repeat = (strncmp(reminders[i].status, "repeat", 6) == 0);
                    bool is_today  = (strncmp(reminders[i].date, today, 10) == 0);
                    if ((is_repeat || is_today) &&
                    timeinfo.tm_hour == reminders[i].hour &&
                    timeinfo.tm_min  == reminders[i].minute) {
                        int    r_hour = reminders[i].hour;
                        int    r_min  = reminders[i].minute;
                        char   r_cont[64]; strcpy(r_cont, reminders[i].content);
                        char   r_date[11]; strcpy(r_date, reminders[i].date);
                        int    idx = i;
                        send_reminder_history(r_cont);
                        if (took && !released) { xSemaphoreGive(reminders_mutex); released=true; }
                        char tbuf[6]; fmt_time(r_hour, r_min, tbuf);
                        if (ui_state == UI_IDLE) {
                            fill_screen(COLOR_BLACK);
                            // gpio_set_level(LDR_BUZZER_PIN, 1);
                            draw_string(10, 10, "NHAC NHO:", COLOR_GREEN);
                            draw_string(10, 40, tbuf, COLOR_WHITE);
                            draw_string(10, 70, r_cont, COLOR_WHITE);
                            draw_string(10, 90, r_date, COLOR_YELLOW);
                            shown_hour = shown_min = -1;
                            shown_y = shown_m = shown_d = -1;
                            alarm_screen_visible = true;
    						// xTaskCreatePinnedToCore(send_email, "mail_alarm_due", 4096, NULL, 4, NULL, 0);
							if (mail_task == NULL) {
    							xTaskCreatePinnedToCore(send_email, "mail_alarm_due", 12288, NULL, 2, &mail_task, 1);
							}
                        }
                        taskYIELD();
                        alarm_active = true;
                        alarm_index  = idx;
                        time(&alarm_started_at);
                        first_swipe_ts = 0;
                        snooze_index = -1; snooze_until = 0;
                        xEventGroupSetBits(eg_alarm, EV_ALARM_START);  
                        xEventGroupWaitBits(eg_alarm, EV_GESTURE_DONE, pdTRUE, pdTRUE, portMAX_DELAY);
                        break;                             
                    }
                }
                if (took && !released) xSemaphoreGive(reminders_mutex); 
            }
		if (!alarm_active && snooze_index >= 0) {
    		time_t nowt; time(&nowt);
    		if (nowt >= snooze_until) {
        	Reminder rr;
        	if (reminders_mutex) xSemaphoreTake(reminders_mutex, portMAX_DELAY);
        	rr = reminders[snooze_index];
        	if (reminders_mutex) xSemaphoreGive(reminders_mutex);
        	char tb[6]; fmt_time(timeinfo.tm_hour, timeinfo.tm_min, tb);
        	if (ui_state == UI_IDLE) {
            	fill_screen(COLOR_BLACK);
            	// gpio_set_level(LDR_BUZZER_PIN, 1);
            	draw_string(10, 10, "NHAC NHO:", COLOR_GREEN);
            	draw_string(10, 40, tb, COLOR_WHITE);
            	draw_string(10, 70, rr.content, COLOR_WHITE);
            	draw_string(10, 90, rr.date, COLOR_YELLOW);
                shown_hour = shown_min = -1;
                shown_y = shown_m = shown_d = -1;
                alarm_screen_visible = true;
        	}
            taskYIELD(); 
        	alarm_active = true;
        	alarm_index  = snooze_index;
            time(&alarm_started_at);
            first_swipe_ts = 0;
            xEventGroupSetBits(eg_alarm, EV_ALARM_START);
            xEventGroupWaitBits(eg_alarm, EV_GESTURE_DONE, pdTRUE, pdTRUE, portMAX_DELAY);
    		}
		}
        if (ui_state == UI_IDLE && !alarm_screen_visible) {
            if (shown_hour == -1 || shown_min == -1 || shown_y==-1) {
                fill_screen(COLOR_BLACK);
                const char *title = "THOI GIAN HIEN TAI";
                int tx = (TFT_WIDTH - (int)strlen(title) * FONT_W)/2;
                draw_string(tx, 20, title, COLOR_GREEN);
                idle_x = (TFT_WIDTH  - 5 * FONT_W)/2;   
                idle_y = (TFT_HEIGHT - FONT_H)/2 - 6;
                char buf[6];
                fmt_time(timeinfo.tm_hour, timeinfo.tm_min, buf);
                draw_string(idle_x, idle_y, buf, COLOR_WHITE);
                char datebuf[11];
                int cy = timeinfo.tm_year + 1900, cm = timeinfo.tm_mon + 1, cd = timeinfo.tm_mday;
                fmt_date(cy, cm, cd, datebuf);
                int dx = (TFT_WIDTH - (int)strlen(datebuf)*FONT_W)/2;
                draw_string(dx, idle_y + FONT_H + 6, datebuf, COLOR_YELLOW);
                shown_hour = timeinfo.tm_hour;
                shown_min  = timeinfo.tm_min;
                shown_y = cy; shown_m = cm; shown_d = cd;
                idle_draw_upcoming(&timeinfo);
            } else {
                if (timeinfo.tm_hour != shown_hour) {
                    char hh[3] = { (char)('0'+(timeinfo.tm_hour/10)), (char)('0'+(timeinfo.tm_hour%10)), 0 };
                    fill_rect(idle_x, idle_y, 2 * FONT_W, FONT_H, COLOR_BLACK);
                    draw_string(idle_x, idle_y, hh, COLOR_WHITE);
                    shown_hour = timeinfo.tm_hour;
                    idle_draw_upcoming(&timeinfo);
                }
                if (timeinfo.tm_min != shown_min) {
                    char mm[3] = { (char)('0'+(timeinfo.tm_min/10)), (char)('0'+(timeinfo.tm_min%10)), 0 };
                    int mx = idle_x + 3 * FONT_W;
                    fill_rect(mx, idle_y, 2 * FONT_W, FONT_H, COLOR_BLACK);
                    draw_string(mx, idle_y, mm, COLOR_WHITE);
                    shown_min = timeinfo.tm_min;
                    idle_draw_upcoming(&timeinfo);
                }
                int cy = timeinfo.tm_year + 1900, cm = timeinfo.tm_mon + 1, cd = timeinfo.tm_mday;
                if (cy!=shown_y || cm!=shown_m || cd!=shown_d) {
                    char datebuf[11];
                    fmt_date(cy, cm, cd, datebuf);
                    int dx = (TFT_WIDTH - (int)strlen(datebuf)*FONT_W)/2;
                    fill_rect(0, idle_y + FONT_H + 6, TFT_WIDTH, FONT_H, COLOR_BLACK);
                    draw_string(dx, idle_y + FONT_H + 6, datebuf, COLOR_YELLOW);
                    shown_y = cy; shown_m = cm; shown_d = cd;
                }
            }
        } else {
            shown_hour = shown_min = -1;
            shown_y = shown_m = shown_d = -1;
        }
        vTaskDelayUntil(&pt_last, pdMS_TO_TICKS(100));
    }
}

void ui_task(void *pvParam) {
    ESP_LOGI(TAG, "UI task started");
    if (!reminders_mutex) reminders_mutex = xSemaphoreCreateMutex();
	reminders_recalc();
    buttons_init();
    ui_state = UI_IDLE;
    TickType_t last = xTaskGetTickCount();
    while (1) {
        BtnEdges e = {0};
        scan_buttons(&e);
        switch (ui_state) {
        case UI_IDLE: {
            if (alarm_active && e.cancel_edge) {
                bool is_rep = false;
                if (reminders_mutex) xSemaphoreTake(reminders_mutex, portMAX_DELAY);
                if (alarm_index >= 0 && alarm_index < num_reminders) {
                    is_rep = (strncasecmp(reminders[alarm_index].status, "repeat", 6) == 0);
                }
                if (reminders_mutex) xSemaphoreGive(reminders_mutex);
                if (is_rep) {                 
                    time_t nowt; time(&nowt);
                    snooze_until = nowt + SNOOZE_SECS;
                    snooze_index = alarm_index;
                } else {
                    snooze_index = -1;
                }
                alarm_active = false;
                alarm_screen_visible = false;         
                shown_hour = shown_min = -1;  
                shown_y = shown_m = shown_d = -1;
                gpio_set_level(LDR_BUZZER_PIN, 0);
                draw_idle_screen_now();
                time_t now; struct tm timeinfo;
                time(&now); localtime_r(&now, &timeinfo);
                shown_hour = timeinfo.tm_hour; shown_min = timeinfo.tm_min;
                shown_y = timeinfo.tm_year+1900; shown_m = timeinfo.tm_mon+1; shown_d = timeinfo.tm_mday;
                idle_draw_upcoming(&timeinfo);
                xEventGroupSetBits(eg_alarm, EV_GESTURE_DONE);   
                ldr_gl5537_set_enabled(&ldr, false);
                break;                        
            }
            if (e.ok_edge) {
                if (alarm_active) {                    
                    alarm_active = false;  
                    alarm_screen_visible = false;           
                    shown_hour = shown_min = -1;       
                    shown_y = shown_m = shown_d = -1;
                    gpio_set_level(LDR_BUZZER_PIN, 0);
                    draw_idle_screen_now();
                    time_t now; struct tm timeinfo;
                    time(&now); localtime_r(&now, &timeinfo);
                    shown_hour = timeinfo.tm_hour; shown_min = timeinfo.tm_min;
                    shown_y = timeinfo.tm_year+1900; shown_m = timeinfo.tm_mon+1; shown_d = timeinfo.tm_mday;
                    idle_draw_upcoming(&timeinfo);
                    xEventGroupSetBits(eg_alarm, EV_GESTURE_DONE);   
                    ldr_gl5537_set_enabled(&ldr, false);
                }
                menu_index = 0;
                SET_STATE(UI_MENU);
                ui_draw_menu();
            }
            break;
        }
        break;
        case UI_MENU:
            if (e.next_edge) { if (menu_index>0) menu_index--; else menu_index=3; ui_draw_menu(); }
            if (e.back_edge) { if (menu_index<3) menu_index++; else menu_index=0; ui_draw_menu(); }
            if (e.ok_edge) {
                if (menu_index == 0) { 
                    if (num_reminders==0) { SET_STATE(UI_IDLE); shown_hour=shown_min=-1; break; }
                    pick_index=0; SET_STATE(UI_VIEW_LIST); ui_draw_list_content("DANH SACH LICH");
                } else if (menu_index == 1) { 
                    if (num_reminders==0) { SET_STATE(UI_IDLE); shown_hour=shown_min=-1; break; }
                    pick_index=0; SET_STATE(UI_EDIT_PICK); ui_draw_list_content("CHON LICH CAN CHINH");
                } else if (menu_index == 2) { 
                    preset_index=0; two_sel=SEL_LEFT; edit_active=false;
                    time_t now; struct tm t; time(&now); localtime_r(&now,&t);
                    edit_year = t.tm_year + 1900;
                    SET_STATE(UI_ADD_CONTENT); ui_draw_preset_list("CHON NOI DUNG");
                } else { 
                    if (num_reminders==0) { SET_STATE(UI_IDLE); shown_hour=shown_min=-1; break; }
                    pick_index=0; SET_STATE(UI_DEL_PICK); ui_draw_list_content("XOA LICH");
                }
            }
            if (e.cancel_edge) { 
                alarm_active = false; 
                alarm_screen_visible = false;                     
                SET_STATE(UI_IDLE); 
                shown_hour=shown_min=-1; 
                shown_y = shown_m = shown_d = -1; 
                xEventGroupSetBits(eg_alarm, EV_GESTURE_DONE);  
                ldr_gl5537_set_enabled(&ldr, false);         
            }
            break;   
        case UI_VIEW_LIST:
            if (e.next_edge) { if (pick_index>0) pick_index--; else pick_index=(num_reminders>0?num_reminders-1:0); ui_draw_list_content("DANH SACH LICH"); }
            if (e.back_edge) { if (pick_index<num_reminders-1) pick_index++; else pick_index=0; ui_draw_list_content("DANH SACH LICH"); }
            if (e.ok_edge)   { SET_STATE(UI_VIEW_DETAIL); ui_draw_view_detail(); }
            if (e.cancel_edge){ SET_STATE(UI_MENU); ui_draw_menu(); }
            break;
        case UI_VIEW_DETAIL:
            if (e.ok_edge || e.cancel_edge) {
                SET_STATE(UI_VIEW_LIST);
                ui_draw_list_content("DANH SACH LICH");
            }
            break;
        case UI_EDIT_PICK:
            if (e.next_edge) { if (pick_index>0) pick_index--; else pick_index=(num_reminders>0?num_reminders-1:0); ui_draw_list_content("CHON LICH CAN CHINH"); }
            if (e.back_edge) { if (pick_index<num_reminders-1) pick_index++; else pick_index=0; ui_draw_list_content("CHON LICH CAN CHINH"); }
            if (e.ok_edge) {
                xSemaphoreTake(reminders_mutex, portMAX_DELAY);
                edit_hour  = reminders[pick_index].hour;
                edit_min   = reminders[pick_index].minute;
                parse_date(reminders[pick_index].date, &edit_year, &edit_month, &edit_day);
                xSemaphoreGive(reminders_mutex);
                submenu_index = 0; edit_active=false; two_sel=SEL_LEFT;
                SET_STATE(UI_EDIT_SUBMENU); ui_draw_edit_submenu();
            }
            if (e.cancel_edge){ SET_STATE(UI_MENU); ui_draw_menu(); }
            break;
        case UI_EDIT_SUBMENU:
            if (e.next_edge) { if (submenu_index>0) submenu_index--; else submenu_index=2; ui_draw_edit_submenu(); }
            if (e.back_edge) { if (submenu_index<2) submenu_index++; else submenu_index=0; ui_draw_edit_submenu(); }
            if (e.ok_edge) {
                if (submenu_index==0) { 
                    preset_index=0;
                    SET_STATE(UI_EDIT_CONTENT); ui_draw_preset_list("CHON NOI DUNG MOI");
                } else if (submenu_index==1) { 
                    two_sel=SEL_LEFT; edit_active=false;
                    SET_STATE(UI_EDIT_DATE); ui_draw_date_editor("CHINH NGAY", edit_day, edit_month, two_sel);
                } else { 
                    field_sel=SEL_HOUR; edit_active=false;
                    SET_STATE(UI_EDIT_TIME); ui_draw_time_editor("CHINH GIO", edit_hour, edit_min, field_sel, true);
                }
            }
            if (e.cancel_edge){ SET_STATE(UI_EDIT_PICK); ui_draw_list_content("CHON LICH CAN CHINH"); }
            break;
        case UI_EDIT_CONTENT:
            if (e.next_edge) { if (preset_index>0) preset_index--; else preset_index=NUM_CONTENT_PRESETS-1; ui_draw_preset_list("CHON NOI DUNG MOI"); }
            if (e.back_edge) { if (preset_index<NUM_CONTENT_PRESETS-1) preset_index++; else preset_index=0; ui_draw_preset_list("CHON NOI DUNG MOI"); }
            if (e.ok_edge) {
                xSemaphoreTake(reminders_mutex, portMAX_DELAY);
		        strncpy(reminders[pick_index].content, CONTENT_PRESETS[preset_index], sizeof(reminders[pick_index].content)-1);
		        reminders[pick_index].content[sizeof(reminders[pick_index].content)-1] = 0;
		        xSemaphoreGive(reminders_mutex);
		        update_reminder(reminders[pick_index].id, reminders[pick_index].date, reminders[pick_index].hour, reminders[pick_index].minute, reminders[pick_index].content, reminders[pick_index].status);
		        save_reminders_to_nvs();
                SET_STATE(UI_EDIT_SUBMENU); ui_draw_edit_submenu();
            }
            if (e.cancel_edge){ SET_STATE(UI_EDIT_SUBMENU); ui_draw_edit_submenu(); }
            break;
        case UI_EDIT_DATE:
            if (e.next_edge) {
                if (!edit_active) { two_sel = (two_sel==SEL_LEFT)?SEL_RIGHT:SEL_LEFT; }
                else { if (two_sel==SEL_LEFT) { edit_day++; } else { edit_month++; } clamp_day_month_y(&edit_day,&edit_month,edit_year); }
                ui_draw_date_editor("CHINH NGAY", edit_day, edit_month, two_sel);
            }
            if (e.back_edge) {
                if (!edit_active) { two_sel = (two_sel==SEL_LEFT)?SEL_RIGHT:SEL_LEFT; }
                else { if (two_sel==SEL_LEFT) { edit_day--; } else { edit_month--; } clamp_day_month_y(&edit_day,&edit_month,edit_year); }
                ui_draw_date_editor("CHINH NGAY", edit_day, edit_month, two_sel);
            }
            if (e.ok_edge) {
                xSemaphoreTake(reminders_mutex, portMAX_DELAY);
		        fmt_date(edit_year, edit_month, edit_day, reminders[pick_index].date);
		        xSemaphoreGive(reminders_mutex);
		        update_reminder(reminders[pick_index].id, reminders[pick_index].date, reminders[pick_index].hour, reminders[pick_index].minute, reminders[pick_index].content, reminders[pick_index].status);
		        edit_active = !edit_active; 
                save_reminders_to_nvs();
		        ui_draw_date_editor("CHINH NGAY", edit_day, edit_month, two_sel);
             }
            if (e.cancel_edge) {
                xSemaphoreTake(reminders_mutex, portMAX_DELAY);
                fmt_date(edit_year, edit_month, edit_day, reminders[pick_index].date);
                xSemaphoreGive(reminders_mutex);
                SET_STATE(UI_EDIT_SUBMENU); ui_draw_edit_submenu();
            }
            break;
        case UI_EDIT_TIME:
            if (e.next_edge) {
                if (!edit_active) { field_sel=(field_sel==SEL_HOUR)?SEL_MINUTE:SEL_HOUR; }
                else { if (field_sel==SEL_HOUR) edit_hour++; else edit_min++; clamp_time(&edit_hour,&edit_min); }
                ui_draw_time_editor("CHINH GIO", edit_hour, edit_min, field_sel, true);
            }
            if (e.back_edge) {
                if (!edit_active) { field_sel=(field_sel==SEL_HOUR)?SEL_MINUTE:SEL_HOUR; }
                else { if (field_sel==SEL_HOUR) edit_hour--; else edit_min--; clamp_time(&edit_hour,&edit_min); }
                ui_draw_time_editor("CHINH GIO", edit_hour, edit_min, field_sel, true);
            }
            if (e.ok_edge) {
                if (edit_active) {
                    xSemaphoreTake(reminders_mutex, portMAX_DELAY);
		            if (field_sel==SEL_HOUR) reminders[pick_index].hour=edit_hour; else reminders[pick_index].minute=edit_min;
		            xSemaphoreGive(reminders_mutex);
		            update_reminder(reminders[pick_index].id, reminders[pick_index].date, reminders[pick_index].hour, reminders[pick_index].minute, reminders[pick_index].content, reminders[pick_index].status);
                    save_reminders_to_nvs();
                }
                edit_active=!edit_active;
                ui_draw_time_editor("CHINH GIO", edit_hour, edit_min, field_sel, true);
            }
            if (e.cancel_edge) {
                xSemaphoreTake(reminders_mutex, portMAX_DELAY);
                reminders[pick_index].hour=edit_hour; reminders[pick_index].minute=edit_min;
                
                xSemaphoreGive(reminders_mutex);
                SET_STATE(UI_EDIT_SUBMENU); ui_draw_edit_submenu();
            }
            break;
        case UI_ADD_CONTENT:
            if (e.next_edge) { if (preset_index>0) preset_index--; else preset_index=NUM_CONTENT_PRESETS-1; ui_draw_preset_list("CHON NOI DUNG"); }
            if (e.back_edge) { if (preset_index<NUM_CONTENT_PRESETS-1) preset_index++; else preset_index=0; ui_draw_preset_list("CHON NOI DUNG"); }
            if (e.ok_edge) {
                edit_day=1; edit_month=1; two_sel=SEL_LEFT; edit_active=false;
                SET_STATE(UI_ADD_DATE); ui_draw_date_editor("CHON NGAY THANG", edit_day, edit_month, two_sel);
            }
            if (e.cancel_edge){ SET_STATE(UI_MENU); ui_draw_menu(); }
            break;
        case UI_ADD_DATE:
            if (e.next_edge) {
                if (two_sel==SEL_LEFT) { edit_day++; } else { edit_month++; }
                clamp_day_month_y(&edit_day,&edit_month,edit_year);
                ui_draw_date_editor("CHON NGAY THANG", edit_day, edit_month, two_sel);
            }
            if (e.back_edge) {
                if (two_sel==SEL_LEFT) { edit_day--; } else { edit_month--; }
                clamp_day_month_y(&edit_day,&edit_month,edit_year);
                ui_draw_date_editor("CHON NGAY THANG", edit_day, edit_month, two_sel);
            }
            if (e.ok_edge) {
                if (two_sel==SEL_LEFT) {
                    two_sel = SEL_RIGHT; 
                    ui_draw_date_editor("CHON NGAY THANG", edit_day, edit_month, two_sel);
                } else {
                    field_sel = SEL_HOUR; edit_hour=0; edit_min=0;
                    SET_STATE(UI_ADD_TIME); ui_draw_time_editor("CHON GIO", edit_hour, edit_min, field_sel, false);
                }
            }
            if (e.cancel_edge){ SET_STATE(UI_MENU); ui_draw_menu(); }
            break;
        case UI_ADD_TIME:
            if (e.next_edge) {
                if (field_sel==SEL_HOUR) { edit_hour++; } else { edit_min++; }
                clamp_time(&edit_hour,&edit_min);
                ui_draw_time_editor("CHON GIO", edit_hour, edit_min, field_sel, false);
            }
            if (e.back_edge) {
                if (field_sel==SEL_HOUR) { edit_hour--; } else { edit_min--; }
                clamp_time(&edit_hour,&edit_min);
                ui_draw_time_editor("CHON GIO", edit_hour, edit_min, field_sel, false);
            }
            if (e.ok_edge) {
                if (field_sel==SEL_HOUR) {
                field_sel=SEL_MINUTE; 
                ui_draw_time_editor("CHON GIO", edit_hour, edit_min, field_sel, false);
                } else {
                    char new_date[11]; fmt_date(edit_year, edit_month, edit_day, new_date);
					add_reminder_full(next_id, new_date, edit_hour, edit_min, CONTENT_PRESETS[preset_index], "pending");
                    save_reminders_to_nvs();
                    SET_STATE(UI_MENU); ui_draw_menu();
                }
            }
            if (e.cancel_edge){ SET_STATE(UI_MENU); ui_draw_menu(); }
            break;
        case UI_DEL_PICK:
            if (e.next_edge) { if (pick_index>0) pick_index--; else pick_index=(num_reminders>0?num_reminders-1:0); ui_draw_list_content("XOA LICH"); }
            if (e.back_edge) { if (pick_index<num_reminders-1) pick_index++; else pick_index=0; ui_draw_list_content("XOA LICH"); }
            if (e.ok_edge) {
                delete_reminder_at(pick_index);
                save_reminders_to_nvs();
                if (num_reminders==0) { SET_STATE(UI_MENU); ui_draw_menu(); }
                else { if (pick_index>=num_reminders) pick_index=num_reminders-1; ui_draw_list_content("XOA LICH"); }
            }
            if (e.cancel_edge){ SET_STATE(UI_MENU); ui_draw_menu(); }
            break;
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(20));
    }
}

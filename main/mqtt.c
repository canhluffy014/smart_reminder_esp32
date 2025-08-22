#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "sntp.h"

static const char *TAG = "MQTT";

extern void add_reminder_full(int id, const char *date, int hour, int min, const char *content, const char *status);
extern void delete_reminder_at(int index);
extern void update_reminder_status(int id, const char *status);
extern void send_reminder_history(const char *content);
extern void sync_reminder(const char *action, int id, const char *date, const char *time, const char *content, const char *status);
extern void update_reminder(int id, const char *date, int hour, int min, const char *content, const char *status);

static esp_mqtt_client_handle_t mqtt_client = NULL;

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

void mqtt_publish(const char *topic, const char *data, int qos, int retain)
{
    if (mqtt_client) {
        int msg_id = esp_mqtt_client_publish(mqtt_client, topic, data, 0, qos, retain);
        ESP_LOGI(TAG, "Đã gửi MQTT: topic=%s, data=%s, msg_id=%d", topic, data, msg_id);
    } else {
        ESP_LOGE(TAG, "MQTT client chưa khởi tạo");
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        msg_id = esp_mqtt_client_subscribe(client, "reminders/add", 0);
        ESP_LOGI(TAG, "Subscribed to reminders/add, msg_id=%d", msg_id);
        msg_id = esp_mqtt_client_subscribe(client, "reminders/update", 0);
        ESP_LOGI(TAG, "Subscribed to reminders/update, msg_id=%d", msg_id);
        msg_id = esp_mqtt_client_subscribe(client, "reminders/delete", 0);
        ESP_LOGI(TAG, "Subscribed to reminders/delete, msg_id=%d", msg_id);
        msg_id = esp_mqtt_client_subscribe(client, "reminders/status", 0);
        ESP_LOGI(TAG, "Subscribed to reminders/status, msg_id=%d", msg_id);
        msg_id = esp_mqtt_client_subscribe(client, "reminders/history", 0);
        ESP_LOGI(TAG, "Subscribed to reminders/history, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        ESP_LOGI(TAG, "Nhận MQTT data, topic=%.*s, data=%.*s", 
                 event->topic_len, event->topic, event->data_len, event->data);
        if (event->data_len >= 256) {
            ESP_LOGE(TAG, "Dữ liệu MQTT quá lớn: %d bytes", event->data_len);
            return;
        }
        char data[256];
        strncpy(data, event->data, event->data_len);
        data[event->data_len] = '\0';
        ESP_LOGI(TAG, "Dữ liệu MQTT copy: %s", data);
        
        cJSON *json = cJSON_Parse(data);
        if (!json || json->type == cJSON_Invalid) {
            ESP_LOGE(TAG, "Lỗi parse JSON: %s", data);
            cJSON_Delete(json);
            return;
        }
        ESP_LOGI(TAG, "Parse JSON thành công");
        
        cJSON *action = cJSON_GetObjectItem(json, "action");
        cJSON *id = cJSON_GetObjectItem(json, "id");
        cJSON *date = cJSON_GetObjectItem(json, "date");
        cJSON *time = cJSON_GetObjectItem(json, "time");
        cJSON *content = cJSON_GetObjectItem(json, "content");
        cJSON *status = cJSON_GetObjectItem(json, "status");
        
        if (action && cJSON_IsString(action)) {
            ESP_LOGI(TAG, "Action=%s", action->valuestring);
            if (strcmp(action->valuestring, "add") == 0) {
                if (!date || !time || !content || !status) {
                    ESP_LOGE(TAG, "Thiếu trường bắt buộc: date=%s, time=%s, content=%s, status=%s",
                             date ? date->valuestring : "null",
                             time ? time->valuestring : "null",
                             content ? content->valuestring : "null",
                             status ? status->valuestring : "null");
                    cJSON_Delete(json);
                    return;
                }
                sync_reminder(action->valuestring, -1, 
                             date->valuestring, time->valuestring, 
                             content->valuestring, status->valuestring);
            } else if (strcmp(action->valuestring, "delete") == 0 && id && cJSON_IsNumber(id)) {
                sync_reminder(action->valuestring, id->valueint, NULL, NULL, NULL, NULL);
            } else if (strcmp(action->valuestring, "update") == 0 && id && cJSON_IsNumber(id)) {
                sync_reminder(action->valuestring, id->valueint, date->valuestring, time->valuestring, content->valuestring, status->valuestring);
            } else {
                ESP_LOGE(TAG, "Action hoặc id không hợp lệ");
            }
        } else {
            ESP_LOGE(TAG, "JSON thiếu action: %s", data);
        }
        cJSON_Delete(json);
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

void mqtt_app_start(void)
{
    ESP_LOGI(TAG, "Khởi tạo MQTT");
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://broker.hivemq.com",
        .broker.address.port = 1883,
    };
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}
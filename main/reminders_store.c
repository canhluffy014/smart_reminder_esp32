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
#define TAG "Reminders task"

int next_id = 1;
int num_reminders = 0;
int pick_index = 0;

SemaphoreHandle_t reminders_mutex = NULL;
Reminder reminders[MAX_REMINDERS] = {};

void recompute_next_id_locked(void) {
    int maxid = 0;
    for (int i = 0; i < num_reminders; i++) {
        if (reminders[i].id > maxid) maxid = reminders[i].id;
    }
    next_id = (maxid > 0) ? (maxid + 1) : 1;
}

void reminders_recalc(void) {
    if (!reminders_mutex) return;
    xSemaphoreTake(reminders_mutex, portMAX_DELAY);
    int count = 0;
    int maxid = 0;
    for (int i = 0; i < MAX_REMINDERS; i++) {
        if (reminders[i].id > 0 ||
            reminders[i].content[0] != '\0' ||
            reminders[i].date[0]    != '\0') {
            count++;
            if (reminders[i].id > maxid) maxid = reminders[i].id;
        }
    }
    num_reminders = count;
    next_id       = (maxid > 0) ? (maxid + 1) : 1;
    xSemaphoreGive(reminders_mutex);
}

void add_reminder_full(int id, const char *date, int hour, int min, const char *content, const char *status) {
    xSemaphoreTake(reminders_mutex, pdMS_TO_TICKS(1000));
    if (num_reminders < MAX_REMINDERS) {
        int idx = num_reminders;
        reminders[idx].id = id;
        strncpy(reminders[idx].date, date, sizeof(reminders[idx].date) - 1);
        reminders[idx].date[sizeof(reminders[idx].date) - 1] = 0;
        reminders[idx].hour = hour;
        reminders[idx].minute = min;
        strncpy(reminders[idx].content, content, sizeof(reminders[idx].content) - 1);
        reminders[idx].content[sizeof(reminders[idx].content) - 1] = 0;
        strncpy(reminders[idx].status, status, sizeof(reminders[idx].status) - 1);
        reminders[idx].status[sizeof(reminders[idx].status) - 1] = 0;
        num_reminders++;
        recompute_next_id_locked();
        ESP_LOGI(TAG, "Thêm báo thức ID %d: %s %02d:%02d %s %s", 
                 id, date, hour, min, content, status);
        cJSON *add_json = cJSON_CreateObject();
        cJSON_AddNumberToObject(add_json, "id", id);
        cJSON_AddStringToObject(add_json, "date", date);
        char time_str[6];
        snprintf(time_str, sizeof(time_str), "%02d:%02d", hour, min);
        cJSON_AddStringToObject(add_json, "time", time_str);
        cJSON_AddStringToObject(add_json, "content", content);
        cJSON_AddStringToObject(add_json, "status", status);
        char *add_str = cJSON_PrintUnformatted(add_json);
        mqtt_publish("reminders/add", add_str, 0, 0);
        cJSON_Delete(add_json);
        free(add_str);
    } else {
        ESP_LOGE(TAG, "Danh sách báo thức đã đầy");
    }
    xSemaphoreGive(reminders_mutex);
}

void add_reminder_full_nr(int id, const char *date, int hour, int min, const char *content, const char *status) {
    xSemaphoreTake(reminders_mutex, pdMS_TO_TICKS(1000));
    if (num_reminders < MAX_REMINDERS) {
        int idx = num_reminders;
        reminders[idx].id = id;
        strncpy(reminders[idx].date, date, sizeof(reminders[idx].date) - 1);
        reminders[idx].date[sizeof(reminders[idx].date) - 1] = 0;
        reminders[idx].hour = hour;
        reminders[idx].minute = min;
        strncpy(reminders[idx].content, content, sizeof(reminders[idx].content) - 1);
        reminders[idx].content[sizeof(reminders[idx].content) - 1] = 0;
        strncpy(reminders[idx].status, status, sizeof(reminders[idx].status) - 1);
        reminders[idx].status[sizeof(reminders[idx].status) - 1] = 0;
        num_reminders++;
        recompute_next_id_locked();
        ESP_LOGI(TAG, "Thêm báo thức ID %d: %s %02d:%02d %s %s", 
                 id, date, hour, min, content, status);
       
    } else {
        ESP_LOGE(TAG, "Danh sách báo thức đã đầy");
    }
    xSemaphoreGive(reminders_mutex);
}

void update_reminder(int id, const char *date, int hour, int min, const char *content, const char *status) {
    ESP_LOGI(TAG, "Bắt đầu update_reminder, id=%d", id);
    if (!reminders_mutex) {
        ESP_LOGE(TAG, "reminders_mutex chưa khởi tạo");
        return;
    }
    if (xSemaphoreTake(reminders_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        int i;
        for (i = 0; i < num_reminders; i++) {
            if (reminders[i].id == id) {
                ESP_LOGI(TAG, "Tìm thấy báo thức ID %d", id);
                if (date && strlen(date) > 0) {
                    strncpy(reminders[i].date, date, sizeof(reminders[i].date) - 1);
                    reminders[i].date[sizeof(reminders[i].date) - 1] = 0;
                }
                if (hour >= 0 && min >= 0) {
                    reminders[i].hour = hour;
                    reminders[i].minute = min;
                }
                if (content && strlen(content) > 0) {
                    strncpy(reminders[i].content, content, sizeof(reminders[i].content) - 1);
                    reminders[i].content[sizeof(reminders[i].content) - 1] = 0;
                }
                if (status && strlen(status) > 0) {
                    strncpy(reminders[i].status, status, sizeof(reminders[i].status) - 1);
                    reminders[i].status[sizeof(reminders[i].status) - 1] = 0;
                }
                ESP_LOGI(TAG, "Cập nhật báo thức ID %d: %s %02d:%02d %s %s", 
                         id, date ? date : reminders[i].date, hour, min, 
                         content ? content : reminders[i].content, status ? status : reminders[i].status);
                cJSON *update_json = cJSON_CreateObject();
                if (!update_json) {
                    ESP_LOGE(TAG, "Không thể tạo JSON object");
                    xSemaphoreGive(reminders_mutex);
                    return;
                }
                cJSON_AddNumberToObject(update_json, "id", id);
                if (date && strlen(date) > 0) cJSON_AddStringToObject(update_json, "date", date);
                
                char time_str[6];
                snprintf(time_str, sizeof(time_str), "%02d:%02d", hour, min);
                cJSON_AddStringToObject(update_json, "time", time_str);
                
                if (content && strlen(content) > 0) cJSON_AddStringToObject(update_json, "content", content);
                if (status && strlen(status) > 0) cJSON_AddStringToObject(update_json, "status", status);
                char *update_str = cJSON_PrintUnformatted(update_json);
                if (update_str) {
                    mqtt_publish("reminders/update", update_str, 0, 0);
                    free(update_str);
                } else {
                    ESP_LOGE(TAG, "Không thể tạo JSON string");
                }
                cJSON_Delete(update_json);
                break;
            }
        }
        if (i == num_reminders) {
            ESP_LOGE(TAG, "Không tìm thấy báo thức ID %d", id);
        }
        xSemaphoreGive(reminders_mutex);
    } else {
        ESP_LOGE(TAG, "Không thể lấy reminders_mutex");
    }
}

void delete_reminder_at(int idx) {
    xSemaphoreTake(reminders_mutex, pdMS_TO_TICKS(1000));
    if (idx >= 0 && idx < num_reminders) {
        int id = reminders[idx].id; 
        for (int i = idx; i < num_reminders - 1; i++) {
            reminders[i] = reminders[i + 1];
        }
        memset(&reminders[num_reminders - 1], 0, sizeof(Reminder));
        num_reminders--;
        if (pick_index >= num_reminders) {
            pick_index = (num_reminders > 0 ? num_reminders - 1 : 0);
        }
        recompute_next_id_locked();
        ESP_LOGI(TAG, "Xóa báo thức ID %d", id);
        cJSON *delete_json = cJSON_CreateObject();
        cJSON_AddNumberToObject(delete_json, "id", id);
        char *delete_str = cJSON_PrintUnformatted(delete_json);
        mqtt_publish("reminders/delete", delete_str, 0, 0);
        cJSON_Delete(delete_json);
        free(delete_str);
    } else {
        ESP_LOGE(TAG, "Index không hợp lệ: %d", idx);
    }
    xSemaphoreGive(reminders_mutex);
}

void delete_reminder_at_nr(int id) {
    xSemaphoreTake(reminders_mutex, pdMS_TO_TICKS(1000));
    int idx = -1;
    for (int i = 0; i < num_reminders; i++) {
        if (reminders[i].id == id) {
            idx = i;
            break;
        }
    }
    if (idx >= 0 && idx < num_reminders) {
        for (int i = idx; i < num_reminders - 1; i++) {
            reminders[i] = reminders[i + 1];
        }
        memset(&reminders[num_reminders - 1], 0, sizeof(Reminder));
        num_reminders--;
        if (pick_index >= num_reminders) {
            pick_index = (num_reminders > 0 ? num_reminders - 1 : 0);
        }
        recompute_next_id_locked();
        ESP_LOGI(TAG, "Xóa báo thức ID %d tại chỉ số %d", id, idx);
        
    } else {
        ESP_LOGE(TAG, "Không tìm thấy báo thức với ID: %d", id);
    }
    xSemaphoreGive(reminders_mutex);
}

void update_reminder_status(int id, const char *status) {
    if (!status || (strcmp(status, "pending") != 0 && strcmp(status, "completed") != 0 && strcmp(status, "repeat") != 0)) {
        ESP_LOGE(TAG, "Trạng thái không hợp lệ: %s", status ? status : "NULL");
        return;
    }
    for (int i = 0; i < num_reminders; i++) {
        if (reminders[i].id == id) {
            // strncpy(reminders[i].status, status, STATUS_LENGTH);
            strncpy(reminders[i].status, status, sizeof(reminders[i].status)-1); 
            reminders[i].status[sizeof(reminders[i].status)-1] = 0;
            ESP_LOGI(TAG, "Cập nhật trạng thái báo thức ID %d: %s", id, status);
                
            cJSON *status_json = cJSON_CreateObject();
            cJSON_AddNumberToObject(status_json, "id", id);
            cJSON_AddStringToObject(status_json, "status", status);
            char *status_str = cJSON_PrintUnformatted(status_json);
            mqtt_publish("reminders/status", status_str, 0, 0);
            cJSON_Delete(status_json);
            free(status_str);
            break;
        }
    }   
}

void send_reminder_history(const char *content) {
    cJSON *history_json = cJSON_CreateObject();
    cJSON_AddStringToObject(history_json, "message", content);
    char *history_str = cJSON_PrintUnformatted(history_json);
    mqtt_publish("reminders/history", history_str, 0, 0);
    cJSON_Delete(history_json);
    free(history_str);
    
}

void sync_reminder(const char *action, int id, const char *date, const char *time, const char *content, const char *status) {
        if (strcmp(action, "add") == 0) {
        if (!date || !time || !content || !status) {
            ESP_LOGE(TAG, "Thiếu trường bắt buộc cho action add");
            return;
        }
        int hour, min;
        if (sscanf(time, "%d:%d", &hour, &min) != 2 || hour < 0 || hour > 23 || min < 0 || min > 59) {
            ESP_LOGE(TAG, "Time không hợp lệ: %s", time);
            return;
        }
        int new_id = (id == -1) ? next_id : id;
        ESP_LOGI(TAG, "Gọi add_reminder_full: id=%d", new_id);
        add_reminder_full_nr(new_id, date, hour, min, content, status);
        save_reminders_to_nvs();
    } else if (strcmp(action, "update") == 0) {
        xSemaphoreTake(reminders_mutex, portMAX_DELAY);
        bool found = false;
        for (int i = 0; i < num_reminders; i++) {
            if (reminders[i].id == id) {
                found = true;
                if (date != NULL && strlen(date) > 0) {
                    if (strlen(date) != 10 || !strstr(date, "-") || date[4] != '-' || date[7] != '-') {
                        ESP_LOGE(TAG, "Invalid date format for update ID %d: %s", id, date);
                    } else {
                        strncpy(reminders[i].date, date, sizeof(reminders[i].date) - 1);
                        reminders[i].date[sizeof(reminders[i].date) - 1] = '\0';
                    }
                }
                if (time != NULL && strlen(time) > 0) {
                    int hour, minute;
                    if (sscanf(time, "%d:%d", &hour, &minute) != 2) {
                        ESP_LOGE(TAG, "Invalid time format for update ID %d: %s", id, time);
                    } else if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
                        ESP_LOGE(TAG, "Invalid time values for update ID %d: hour=%d, minute=%d", id, hour, minute);
                    } else {
                        reminders[i].hour = hour;
                        reminders[i].minute = minute;
                    }
                }
                if (content != NULL && strlen(content) > 0) {
                    if (strlen(content) > 63) {
                        ESP_LOGE(TAG, "Content too long for update ID %d: %s", id, content);
                    } else {
                        strncpy(reminders[i].content, content, sizeof(reminders[i].content) - 1);
                        reminders[i].content[sizeof(reminders[i].content) - 1] = '\0';
                    }
                }
                if (status != NULL && strlen(status) > 0) {
                    strncpy(reminders[i].status, status, sizeof(reminders[i].status) - 1);
                    reminders[i].status[sizeof(reminders[i].status) - 1] = '\0';
                }
                ESP_LOGI(TAG, "Cập nhật báo thức ID %d: %s %02d:%02d %s %s", 
                         id, reminders[i].date, reminders[i].hour, reminders[i].minute, 
                         reminders[i].content, reminders[i].status);
                save_reminders_to_nvs();
                break;
            }
        }
        xSemaphoreGive(reminders_mutex);
        if (!found) {
            ESP_LOGW(TAG, "Reminder ID %d không tồn tại, bỏ qua cập nhật", id);
        }
    } else if (strcmp(action, "delete") == 0) {
      	delete_reminder_at_nr(id);
        save_reminders_to_nvs();
	}
}

static esp_err_t nvs_init_once(void) {
    static bool inited = false;
    if (inited) return ESP_OK;
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err == ESP_OK) inited = true;
    return err;
}

esp_err_t save_reminders_to_nvs(void) {
    ESP_ERROR_CHECK(nvs_init_once());
    nvs_handle_t h;
    esp_err_t err = nvs_open("reminders", NVS_READWRITE, &h);
    if (err != ESP_OK) { ESP_LOGE(TAG, "NVS open fail: %s", esp_err_to_name(err)); return err; }
    err = nvs_set_i32(h, "num_reminders", num_reminders);
    if (err != ESP_OK) { nvs_close(h); return err; }
    extern int next_id;
    err = nvs_set_i32(h, "next_id", next_id);
    if (err != ESP_OK) { nvs_close(h); return err; }
    for (int i = 0; i < num_reminders; i++) {
        char key[32];
        snprintf(key, sizeof(key), "reminder_%d", i);
        err = nvs_set_blob(h, key, &reminders[i], sizeof(Reminder));
        if (err != ESP_OK) { ESP_LOGE(TAG, "Save blob %d fail: %s", i, esp_err_to_name(err)); nvs_close(h); return err; }
    }
    err = nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "Saved %d reminders to NVS", num_reminders);
    return err;
}

esp_err_t load_reminders_from_nvs(void) {
    ESP_ERROR_CHECK(nvs_init_once());
    nvs_handle_t h;
    esp_err_t err = nvs_open("reminders", NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "No 'reminders' namespace; start empty");
        num_reminders = 0;
        extern int next_id; next_id = 1;
        return ESP_OK;
    }
    if (err != ESP_OK) { ESP_LOGE(TAG, "NVS open fail: %s", esp_err_to_name(err)); return err; }
    err = nvs_get_i32(h, "num_reminders", &num_reminders);
    if (err != ESP_OK) { nvs_close(h); return err; }
    extern int next_id;
    err = nvs_get_i32(h, "next_id", &next_id);
    if (err != ESP_OK) { nvs_close(h); return err; }
    if (num_reminders > MAX_REMINDERS) num_reminders = MAX_REMINDERS;
    for (int i = 0; i < num_reminders; i++) {
        char key[32]; snprintf(key, sizeof(key), "reminder_%d", i);
        size_t sz = sizeof(Reminder);
        err = nvs_get_blob(h, key, &reminders[i], &sz);
        if (err != ESP_OK) { ESP_LOGE(TAG, "Load blob %d fail: %s", i, esp_err_to_name(err)); nvs_close(h); return err; }
    }
    nvs_close(h);
    ESP_LOGI(TAG, "Loaded %d reminders from NVS", num_reminders);
    return ESP_OK;
}
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_tls.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"

#define MAILERSEND_API_KEY "mlsn.5e8391d1439c88cc29681a33c50fc9a0e0ffdc9faab7556f3fecda9b2f60e650"

static const char *TAG = "MAILERSEND_EMAIL";
extern TaskHandle_t mail_task; 

static esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            ESP_LOGI(TAG, "Dữ liệu HTTP nhận được: %.*s", evt->data_len, (char*)evt->data);
            break;
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "Lỗi sự kiện HTTP");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "Đã kết nối tới server HTTP");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "Ngắt kết nối khỏi server HTTP");
            break;
        default:
            break;
    }
    return ESP_OK;
}

void send_email(void *pvParameters) {
    const char *post_data =
        "{"
        "\"from\": {\"email\": \"esp32@test-zxk54v8ro1xljy6v.mlsender.net\", \"name\": \"ESP32 Alarm\"},"
        "\"to\": [{\"email\": \"canhluffy014@gmail.com\", \"name\": \"Receiver\"}],"
        "\"subject\": \"Alarm Set Notification\","
        "\"text\": \"Đã đến thời gian nhắc nhở.\","
        "\"html\": \"<h1>Thông báo đã đến lịch</h1><p>Lịch của bạn đã đến.</p>\""
        "}";

    esp_http_client_config_t config = {
        .url = "https://api.mailersend.com/v1/email",
        .event_handler = _http_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    char auth_header[128];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", MAILERSEND_API_KEY);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    ESP_LOGI(TAG, "Gửi email với dữ liệu: %s", post_data);
    esp_err_t err = esp_http_client_perform(client);
	if (err == ESP_OK) {
    	int status_code = esp_http_client_get_status_code(client);
    	if (status_code == 202) {
        	ESP_LOGI(TAG, "G?i email th�nh c�ng, M� tr?ng th�i: %d", status_code);
    	} else {
        	ESP_LOGE(TAG, "G?i email th?t b?i, M� tr?ng th�i: %d", status_code);
        	size_t cap = 1024;
        	char *response_buffer = (char*)malloc(cap);
        	if (response_buffer) {
            	int n = esp_http_client_read(client, response_buffer, cap - 1);
            	if (n > 0) {
                	response_buffer[n] = '\0';
                	ESP_LOGI(TAG, "Ph?n h?i t? server: %s", response_buffer);
            	}
            	free(response_buffer);
        	}
    	}
	} else {
    	ESP_LOGE(TAG, "Y�u c?u HTTP th?t b?i: %s", esp_err_to_name(err));
	}
    esp_http_client_cleanup(client);
    mail_task = NULL;
	vTaskDelete(NULL);
}



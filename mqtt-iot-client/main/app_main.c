#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "mqtt_client.h"
#include "driver/adc.h"

#include "protocol_wifi_common.h"

static const char *TAG = "MQTT";
static uint16_t adc_data;

static TaskHandle_t mqtt_task_handle = NULL;
static TaskHandle_t mq9_sensor_task_handle = NULL;
static xQueueHandle mq9_value_queue_handle = NULL;

static void mqtt_task(void * args) {
	esp_mqtt_client_handle_t client = (esp_mqtt_client_handle_t*) args;
	uint16_t data = 0;

	while (1) {
		if (xQueueReceive(mq9_value_queue_handle, &data, 500)) {
			ESP_LOGI(TAG, "received item from queue: %d", data);

			char msg [sizeof(uint16_t) + 1];
			utoa(data, msg, 10);

			uint8_t msg_id = 0;
			msg_id = esp_mqtt_client_publish(client, CONFIG_MQ9_MQTT_TOPIC, msg, 0, 1, 0);
			ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
		}
	}
}

static void mq9_sensor_task(void * args) {
	while (1) {
		adc_read(&adc_data);
		ESP_LOGI(TAG, "adc read: %d\r\n", adc_data);

		long sent = xQueueSend(mq9_value_queue_handle, &adc_data, 5000);

		ESP_LOGI(TAG, "send value to the queue: %ld\r\n", sent);

		vTaskDelay(CONFIG_MQ9_REFRESH_INTERVAL_MS / portTICK_RATE_MS);
	}
}

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event) {
	esp_mqtt_client_handle_t client = event->client;
	switch (event->event_id) {
		case MQTT_EVENT_CONNECTED:
			ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
			xTaskCreate(mqtt_task, "mqtt_task", 1024, (void *) client, 5, &mqtt_task_handle);
			break;
		case MQTT_EVENT_DISCONNECTED:
			ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
			if (mqtt_task_handle != NULL) vTaskDelete(mqtt_task_handle);
			break;
		case MQTT_EVENT_ERROR:
			ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
			break;
		default:
			break;
	}
	return ESP_OK;
}

static void mqtt_app_start(void) {
	esp_mqtt_client_config_t mqtt_cfg = {
		.uri = CONFIG_BROKER_URL,
		.event_handle = mqtt_event_handler,
	};

	mq9_value_queue_handle = xQueueCreate(1024, sizeof(uint16_t));
	xTaskCreate(mq9_sensor_task, "mq9_sensor_task", 1024, NULL, 5, &mq9_sensor_task_handle);

	esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
	esp_mqtt_client_start(client);
}

void app_main() {
	ESP_ERROR_CHECK(nvs_flash_init());
	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	ESP_ERROR_CHECK(wifi_connect());

	ESP_LOGI(TAG, "[APP] Startup..");
	ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
	ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

	esp_log_level_set("*", ESP_LOG_INFO);
	esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
	esp_log_level_set("TRANSPORT_TCP", ESP_LOG_VERBOSE);
	esp_log_level_set("TRANSPORT_SSL", ESP_LOG_VERBOSE);
	esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
	esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);

	adc_config_t adc_config;
	adc_config.mode = ADC_READ_TOUT_MODE;
	adc_config.clk_div = 8;

	ESP_ERROR_CHECK(adc_init(&adc_config));

	mqtt_app_start();
}

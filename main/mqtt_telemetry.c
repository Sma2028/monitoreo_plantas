#include "mqtt_telemetry.h"
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "app_state.h"
#include "settings.h"

static const char *TAG = "mqtt";
static esp_mqtt_client_handle_t client;
static char plant_id[32];
static char telemetry_topic[64];
static char online_topic[64];
static TaskHandle_t telemetry_task;

static void publish_telemetry(void)
{
    app_status_t *st = malloc(sizeof(app_status_t));
    if (!st) return;
    app_snapshot(st);
    settings_t cfg;
    settings_get(&cfg);
    const uint32_t up = app_uptime_s();

    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "name", cfg.plant_name);
    cJSON_AddStringToObject(j, "state", alerts_state_name(st->eng.state));
    cJSON_AddNumberToObject(j, "ts", (double)app_epoch_now());
    cJSON_AddNumberToObject(j, "up", up);
    cJSON *vars = cJSON_AddObjectToObject(j, "vars");
    for (int i = 0; i < VAR_COUNT; i++) {
        cJSON *v = cJSON_AddObjectToObject(vars, alerts_var_key((var_id_t)i));
        if (st->r[i].valid && !st->r[i].fault) cJSON_AddNumberToObject(v, "v", (int)(st->r[i].value * 10 + (st->r[i].value >= 0 ? .5f : -.5f)) / 10.0);
        else cJSON_AddNullToObject(v, "v");
        cJSON_AddBoolToObject(v, "fault", st->r[i].fault);
        cJSON_AddBoolToObject(v, "alarm", st->eng.alarm[i]);
        cJSON_AddNumberToObject(v, "dir", st->eng.dir[i]);
    }
    cJSON *soil = cJSON_AddObjectToObject(j, "soil");
    cJSON_AddNumberToObject(soil, "mv", st->soil_mv);
    cJSON_AddBoolToObject(soil, "saturated", st->soil_saturated);
    cJSON *pir = cJSON_AddObjectToObject(j, "pir");
    cJSON_AddBoolToObject(pir, "warming", st->pir_warming);
    cJSON_AddNumberToObject(pir, "count", st->motion_count);
    if (st->last_motion_uptime) cJSON_AddNumberToObject(pir, "ago", up - st->last_motion_uptime);
    else cJSON_AddNullToObject(pir, "ago");
    cJSON *w = cJSON_AddObjectToObject(j, "wifi");


    cJSON_AddNumberToObject(w, "rssi", st->rssi);
    cJSON_AddNumberToObject(j, "period", cfg.sample_period_s);
    free(st);
    char *payload = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    if (payload) {
        esp_mqtt_client_enqueue(client, telemetry_topic, payload, 0, 0, false, false);
        free(payload);
    }
}

static void mqtt_event(void *arg, esp_event_base_t base, int32_t event_id, void *data)
{
    if (event_id == MQTT_EVENT_CONNECTED) {
        esp_mqtt_client_enqueue(client, online_topic, "online", 0, 1, true, true);
        ESP_LOGI(TAG, "Broker conectado. Planta: %s", plant_id);
        xTaskNotifyGive(telemetry_task);
    } else if (event_id == MQTT_EVENT_DISCONNECTED) {
        ESP_LOGW(TAG, "Broker desconectado. Reintentando.");
    }
}

static void report_readings(void *arg)
{
    for (;;) {
        settings_t cfg;
        settings_get(&cfg);
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(cfg.sample_period_s * 1000));
        publish_telemetry();
    }
}

void mqtt_telemetry_start(void)
{
    uint8_t mac[6];
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) return;
    snprintf(plant_id, sizeof(plant_id), "planta-%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    snprintf(telemetry_topic, sizeof(telemetry_topic), "plantas/%s/telemetria", plant_id);
    snprintf(online_topic, sizeof(online_topic), "plantas/%s/online", plant_id);
    const esp_mqtt_client_config_t config = {
        .broker.address.uri = "mqtt://broker.hivemq.com:1883",
        .credentials.client_id = plant_id,
        .session.last_will = { .topic = online_topic, .msg = "offline", .qos = 1, .retain = true },
    };
    client = esp_mqtt_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "No se pudo crear el cliente MQTT");
        return;
    }
    if (xTaskCreate(report_readings, "mqtt_telemetry", 4096, NULL, 3, &telemetry_task) == pdPASS) {
        esp_err_t err = esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event, NULL);
        if (err == ESP_OK) err = esp_mqtt_client_start(client);
        if (err == ESP_OK) return;
        vTaskDelete(telemetry_task);
    }
    esp_mqtt_client_destroy(client);
    ESP_LOGE(TAG, "No se pudo iniciar MQTT");
}

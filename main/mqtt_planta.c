/*
 * mqtt_planta.c — Comunicación MQTT, con la misma idea del ejemplo de Arduino:
 *
 *   reconnect()  -> el cliente de ESP-IDF se reconecta solo; al conectar se suscribe
 *                   a TOPIC_CONTROL y TOPIC_CAMBIAR.
 *   callback()   -> on_mensaje(): "ack" reconoce la alerta, un JSON en TOPIC_CAMBIAR
 *                   cambia los rangos de la planta.
 *   publish()    -> mqtt_planta_publicar(): cada medición manda temperatura, humedad,
 *                   luz, suelo y estado, cada uno como texto en su propio tópico.
 *
 * Los valores se publican con "retain" para que la página los vea apenas se abre.
 */
#include "mqtt_planta.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_mac.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "config_red.h"
#include "app_state.h"
#include "settings.h"
#include "cfg_json.h"

static const char *TAG = "mqtt";

static esp_mqtt_client_handle_t s_client;
static volatile bool s_conectado;

bool mqtt_planta_conectado(void) { return s_conectado; }

/* Publica sin bloquear la medición (el mensaje se envía desde la tarea del cliente) */
static void publicar(const char *topic, const char *texto, bool retener)
{
    if (!s_client || !s_conectado) return;
    esp_mqtt_client_enqueue(s_client, topic, texto, 0, 0, retener, true);
}

static void publicar_rangos(void)
{
    settings_t s;
    settings_get(&s);
    cJSON *j = cfg_json_build(&s);
    char *txt = cJSON_PrintUnformatted(j);
    if (txt) publicar(TOPIC_RANGOS, txt, true);
    cJSON_free(txt);
    cJSON_Delete(j);
}

void mqtt_planta_evento(const char *texto)
{
    publicar(TOPIC_EVENTO, texto, false);
}

void mqtt_planta_publicar(void)
{
    if (!s_conectado) return;
    app_status_t *ps = malloc(sizeof(app_status_t));   /* es grande: mejor no en la pila */
    if (!ps) return;
    app_snapshot(ps);
    const app_status_t st = *ps;
    free(ps);

    static const char *topics[VAR_COUNT] = { TOPIC_TEMPERATURA, TOPIC_HUMEDAD, TOPIC_LUZ, TOPIC_SUELO };
    static const int decimales[VAR_COUNT] = { 1, 1, 0, 0 };
    char txt[16];
    for (int i = 0; i < VAR_COUNT; i++) {
        if (st.r[i].fault)        strcpy(txt, "falla");
        else if (!st.r[i].valid)  continue;
        else snprintf(txt, sizeof(txt), "%.*f", decimales[i], st.r[i].value);
        publicar(topics[i], txt, true);
    }
    publicar(TOPIC_ESTADO, alerts_state_name(st.eng.state), true);
    ESP_LOGI(TAG, "Publicado: T=%.1f HR=%.1f Luz=%.0f Suelo=%.0f estado=%s",
             st.r[VAR_TEMP].value, st.r[VAR_HUM].value, st.r[VAR_LUX].value,
             st.r[VAR_SOIL].value, alerts_state_name(st.eng.state));
}

/* ---------- Lo que llega de la página (como callback() del ejemplo) ---------- */
static void cambiar_rangos(const char *json, int len)
{
    cJSON *j = cJSON_ParseWithLength(json, len);
    if (!j) { mqtt_planta_evento("Error: los rangos no llegaron en formato JSON"); return; }
    settings_t s;
    settings_get(&s);
    bool algo = cfg_json_apply_ranges(j, &s);
    cJSON_Delete(j);
    char err[96];
    if (!algo) {
        mqtt_planta_evento("Error: el mensaje no traía rangos");
    } else if (settings_apply(&s, err, sizeof(err)) != ESP_OK) {   /* valida igual que la web */
        char msg[128];
        snprintf(msg, sizeof(msg), "Error: %s", err);
        mqtt_planta_evento(msg);
        ESP_LOGW(TAG, "Rangos rechazados: %s", err);
    } else {
        app_add_event(EV_INFO, "Rangos actualizados desde la página");   /* también va a TOPIC_EVENTO */
    }
    publicar_rangos();               /* la página ve siempre los rangos que de verdad se usan */
}

static void on_mensaje(const char *topic, int tlen, const char *data, int dlen)
{
    char t[96], m[16];
    snprintf(t, sizeof(t), "%.*s", tlen, topic);
    ESP_LOGI(TAG, "Comando recibido en [%s]: %.*s", t, dlen > 200 ? 200 : dlen, data);

    if (strcmp(t, TOPIC_CAMBIAR) == 0) {
        cambiar_rangos(data, dlen);
    } else if (strcmp(t, TOPIC_CONTROL) == 0) {
        snprintf(m, sizeof(m), "%.*s", dlen, data);
        if (strcmp(m, "ack") == 0 || strcmp(m, "ACK") == 0) {
            if (!app_request_ack("página web")) mqtt_planta_evento("No había alerta pendiente");
            mqtt_planta_publicar();
            ESP_LOGI(TAG, "-> Accion: alerta reconocida");
        } else if (strcmp(m, "actualizar") == 0) {
            publicar_rangos();
            mqtt_planta_publicar();
        }
    }
}

static void on_evento(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    esp_mqtt_event_handle_t e = data;
    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        s_conectado = true;
        ESP_LOGI(TAG, "¡Conectado al broker %s!", MQTT_SERVER);
        esp_mqtt_client_publish(s_client, TOPIC_CONEXION, "online", 0, 1, 1);
        esp_mqtt_client_subscribe(s_client, TOPIC_CONTROL, 1);
        esp_mqtt_client_subscribe(s_client, TOPIC_CAMBIAR, 1);
        publicar_rangos();
        mqtt_planta_publicar();
        app_add_event(EV_WIFI, "Conectado al broker MQTT");
        break;
    case MQTT_EVENT_DISCONNECTED:
        if (s_conectado) ESP_LOGW(TAG, "Se perdió el broker. Reintentando...");
        s_conectado = false;
        break;
    case MQTT_EVENT_DATA:
        if (e->current_data_offset == 0 && e->data_len == e->total_data_len)   /* mensajes completos */
            on_mensaje(e->topic, e->topic_len, e->data, e->data_len);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGW(TAG, "Fallo de conexión MQTT. Reintentando en 5 segundos...");
        break;
    default:
        break;
    }
}

/* Espera a tener Wi-Fi con IP antes de arrancar el cliente (si no, la red aún no existe) */
static void mqtt_task(void *arg)
{
    for (;;) {
        app_lock();
        bool red = app_status()->sta_connected;
        app_unlock();
        if (red) break;
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    static char client_id[32];
    snprintf(client_id, sizeof(client_id), "ESP32_planta_%02x%02x%02x", mac[3], mac[4], mac[5]);

    esp_mqtt_client_config_t cfg = {
        .broker.address.hostname = MQTT_SERVER,
        .broker.address.port = MQTT_PORT,
        .broker.address.transport = MQTT_TRANSPORT_OVER_TCP,
        .credentials.client_id = client_id,
        .session.keepalive = 30,
        .session.last_will = {                /* si el ESP32 se apaga, el broker avisa "offline" */
            .topic = TOPIC_CONEXION, .msg = "offline", .qos = 1, .retain = 1,
        },
        .network.reconnect_timeout_ms = 5000,
    };
    ESP_LOGI(TAG, "Intentando conexión al broker %s:%d como %s...", MQTT_SERVER, MQTT_PORT, client_id);
    s_client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, on_evento, NULL);
    esp_mqtt_client_start(s_client);
    vTaskDelete(NULL);
}

void mqtt_planta_start(void)
{
    xTaskCreate(mqtt_task, "mqtt_planta", 4096, NULL, 4, NULL);
}

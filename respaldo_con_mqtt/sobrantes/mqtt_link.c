/*
 * mqtt_link.c — Cliente MQTT (esp-mqtt). Ver los tópicos en mqtt_link.h.
 *
 *  - Se conecta cuando hay Wi-Fi; si se cae, esp-mqtt reintenta solo cada 10 s.
 *  - Los comandos que cambian algo exigen "clave" = contraseña de la página web.
 *    OJO: en un broker público cualquiera puede leer los mensajes. Para uso real
 *    usa un broker privado con usuario/contraseña y TLS (mqtts://).
 *  - El monitoreo y las alertas locales NO dependen de MQTT.
 */
#include "mqtt_link.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_crt_bundle.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "cfg_json.h"

static const char *TAG = "mqtt";

typedef struct {
    uint8_t  kind;
    time_t   epoch;
    uint32_t uptime;
    char     text[EVENT_TEXT_LEN];
} mq_event_t;

static esp_mqtt_client_handle_t s_cli;
static SemaphoreHandle_t s_mtx;
static QueueHandle_t s_evq;
static volatile bool s_connected, s_reconf = true, s_force;   /* s_reconf: (re)crear el cliente cuando haya red */
static mqtt_info_t s_info;
static char s_base[40];
static char s_will_topic[56];
static uint8_t s_bad_pass;
static uint32_t s_block_until;

/* ------------------------------------------------------------------ */
static void make_id(const settings_t *cfg, char *out, size_t n)
{
    if (cfg->mqtt_id[0]) { strlcpy(out, cfg->mqtt_id, n); return; }
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, n, "planta-%02x%02x%02x", mac[3], mac[4], mac[5]);
}

static void topic(char *out, size_t n, const char *leaf)
{
    snprintf(out, n, "%s/%s", s_base, leaf);
}

/* Publica (encola) un JSON y lo libera. Seguro desde cualquier tarea. */
static void pub_json(const char *leaf, cJSON *j, bool retain)
{
    char *txt = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    if (!txt) return;
    char t[64];
    topic(t, sizeof(t), leaf);
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (s_cli && s_connected) {
        esp_mqtt_client_enqueue(s_cli, t, txt, 0, retain ? 1 : 0, retain, true);
        s_info.published++;
    }
    xSemaphoreGive(s_mtx);
    free(txt);
}

/* ------------------------------------------------------------------ */
/*                       Mensajes que se publican                      */
/* ------------------------------------------------------------------ */
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
    cJSON_AddStringToObject(w, "ssid", st->sta_ssid);
    cJSON_AddStringToObject(w, "ip", st->sta_ip);
    cJSON_AddNumberToObject(w, "rssi", st->rssi);
    cJSON_AddNumberToObject(j, "period", cfg.mqtt_period_s);
    free(st);
    pub_json("telemetria", j, true);
}

static void publish_state(void)
{
    app_status_t *st = malloc(sizeof(app_status_t));
    if (!st) return;
    app_snapshot(st);
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "state", alerts_state_name(st->eng.state));
    cJSON_AddNumberToObject(j, "ts", (double)app_epoch_now());
    cJSON *a = cJSON_AddArrayToObject(j, "alarms");
    for (int i = 0; i < VAR_COUNT; i++) {
        if (!st->eng.alarm[i]) continue;
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "var", alerts_var_key((var_id_t)i));
        cJSON_AddStringToObject(o, "dir", st->eng.dir[i] < 0 ? "bajo" : "alto");
        cJSON_AddNumberToObject(o, "v", st->r[i].value);
        cJSON_AddItemToArray(a, o);
    }
    cJSON *f = cJSON_AddArrayToObject(j, "faults");
    for (int i = 0; i < VAR_COUNT; i++)
        if (st->r[i].fault) cJSON_AddItemToArray(f, cJSON_CreateString(alerts_var_key((var_id_t)i)));
    free(st);
    pub_json("estado", j, true);
}

static void publish_config(void)
{
    settings_t cfg;
    settings_get(&cfg);
    pub_json("config", cfg_json_build(&cfg), true);
}

/* ------------------------------------------------------------------ */
/*                          Comandos recibidos                         */
/* ------------------------------------------------------------------ */
static void respond(const char *cmd, const cJSON *req, bool ok, const char *msg)
{
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "cmd", cmd);
    cJSON_AddBoolToObject(r, "ok", ok);
    cJSON_AddStringToObject(r, "msg", msg);
    const cJSON *id = req ? cJSON_GetObjectItemCaseSensitive(req, "id") : NULL;
    if (id) cJSON_AddItemToObject(r, "id", cJSON_Duplicate(id, true));
    pub_json("cmd/resp", r, false);
}

static bool check_pass(const char *cmd, const cJSON *j)
{
    const uint32_t now = app_uptime_s();
    if (now < s_block_until) { respond(cmd, j, false, "Demasiados intentos: espera 1 minuto"); return false; }
    const cJSON *k = j ? cJSON_GetObjectItemCaseSensitive(j, "clave") : NULL;
    settings_t cfg;
    settings_get(&cfg);
    if (cJSON_IsString(k) && strcmp(k->valuestring, cfg.web_pass) == 0) { s_bad_pass = 0; return true; }
    if (++s_bad_pass >= 5) { s_bad_pass = 0; s_block_until = now + 60; }
    respond(cmd, j, false, "Clave incorrecta (es la misma contraseña de la página del dispositivo)");
    return false;
}

static void handle_cmd(const char *cmd, const char *data, int len)
{
    cJSON *j = len > 0 ? cJSON_ParseWithLength(data, len) : NULL;

    if (strcmp(cmd, "actualizar") == 0) {
        s_force = true;
    } else if (strcmp(cmd, "ack") == 0) {
        if (check_pass(cmd, j)) {
            bool changed = app_request_ack("MQTT");
            respond(cmd, j, true, changed ? "Alerta reconocida" : "No había alerta pendiente");
        }
    } else if (strcmp(cmd, "rangos") == 0 || strcmp(cmd, "calibrar") == 0) {
        bool rangos = cmd[0] == 'r';
        if (!j) respond(cmd, NULL, false, "JSON inválido");
        else if (check_pass(cmd, j)) {
            settings_t s;
            settings_get(&s);
            bool any;
            const cJSON *reset = cJSON_GetObjectItemCaseSensitive(j, "reset");
            if (!rangos && cJSON_IsString(reset)) {
                esp_err_t e = settings_reset_calibration(reset->valuestring);
                respond(cmd, j, e == ESP_OK, e == ESP_OK ? "Calibración restablecida" : "No se pudo restablecer");
                if (e == ESP_OK) app_add_event(EV_INFO, "Calibración restablecida por MQTT");
                cJSON_Delete(j);
                return;
            }
            any = rangos ? cfg_json_apply_ranges(j, &s) : cfg_json_apply_cal(j, &s);
            char err[128];
            if (!any) respond(cmd, j, false, "No venía ningún valor para cambiar");
            else if (settings_apply(&s, err, sizeof(err)) != ESP_OK) respond(cmd, j, false, err);
            else {
                respond(cmd, j, true, rangos ? "Rangos guardados" : "Calibración guardada");
                app_add_event(EV_INFO, rangos ? "Rangos actualizados por MQTT" : "Calibración actualizada por MQTT");
            }
        }
    } else if (strcmp(cmd, "resp") != 0) {
        respond(cmd, j, false, "Comando desconocido");
    }
    cJSON_Delete(j);
}

static void on_mqtt(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    esp_mqtt_event_handle_t ev = data;
    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED: {
        char t[64];
        s_connected = true;
        s_force = true;
        s_info.last_error[0] = 0;
        esp_mqtt_client_publish(ev->client, s_will_topic, "online", 0, 1, 1);
        topic(t, sizeof(t), "cmd/#");
        esp_mqtt_client_subscribe(ev->client, t, 1);
        ESP_LOGI(TAG, "Conectado al broker. Tópicos en %s/...", s_base);
        app_add_event(EV_WIFI, "MQTT conectado");
        break;
    }
    case MQTT_EVENT_DISCONNECTED:
        if (s_connected) app_add_event(EV_WIFI, "MQTT desconectado");
        s_connected = false;
        break;
    case MQTT_EVENT_DATA: {
        if (ev->current_data_offset != 0 || ev->data_len != ev->total_data_len) break;  /* mensaje partido: se ignora */
        char t[64];
        int n = ev->topic_len < (int)sizeof(t) - 1 ? ev->topic_len : (int)sizeof(t) - 1;
        memcpy(t, ev->topic, n);
        t[n] = 0;
        size_t bl = strlen(s_base);
        if (strncmp(t, s_base, bl) == 0 && strncmp(t + bl, "/cmd/", 5) == 0) {
            ESP_LOGI(TAG, "Comando: %s", t + bl + 5);
            handle_cmd(t + bl + 5, ev->data, ev->data_len);
        }
        break;
    }
    case MQTT_EVENT_ERROR:
        if (ev->error_handle) {
            if (ev->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED)
                snprintf(s_info.last_error, sizeof(s_info.last_error), "El broker rechazó la conexión (usuario o clave)");
            else if (ev->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
                snprintf(s_info.last_error, sizeof(s_info.last_error), "No se pudo llegar al broker (red o dirección)");
        }
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
static void stop_client(void)
{
    /* Se suelta el candado ANTES de detener: el cliente puede estar publicando una respuesta */
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    esp_mqtt_client_handle_t c = s_cli;
    s_cli = NULL;
    s_connected = false;
    xSemaphoreGive(s_mtx);
    if (c) {
        esp_mqtt_client_stop(c);
        esp_mqtt_client_destroy(c);
    }
}

static void start_client(const settings_t *cfg)
{
    char id[SETTINGS_ID_LEN];
    make_id(cfg, id, sizeof(id));
    snprintf(s_base, sizeof(s_base), "plantas/%s", id);
    snprintf(s_will_topic, sizeof(s_will_topic), "%s/online", s_base);
    strlcpy(s_info.id, id, sizeof(s_info.id));
    strlcpy(s_info.base, s_base, sizeof(s_info.base));
    strlcpy(s_info.uri, cfg->mqtt_uri, sizeof(s_info.uri));
    strlcpy(s_info.user, cfg->mqtt_user, sizeof(s_info.user));
    s_info.enabled = cfg->mqtt_enabled;
    if (!cfg->mqtt_enabled) { ESP_LOGI(TAG, "MQTT desactivado"); return; }

    /* El cliente guarda punteros: se usan copias estáticas */
    static settings_t c;
    static char client_id[SETTINGS_ID_LEN + 4];
    c = *cfg;
    strlcpy(client_id, id, sizeof(client_id));

    esp_mqtt_client_config_t mc = {
        .broker.address.uri = c.mqtt_uri,
        .credentials.client_id = client_id,
        .session.keepalive = 30,
        .session.last_will.topic = s_will_topic,
        .session.last_will.msg = "offline",
        .session.last_will.qos = 1,
        .session.last_will.retain = 1,
        .network.reconnect_timeout_ms = 10000,
        .buffer.size = 2048,
    };
    if (strncmp(c.mqtt_uri, "mqtts://", 8) == 0 || strncmp(c.mqtt_uri, "wss://", 6) == 0)
        mc.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;   /* certificados conocidos */
    if (c.mqtt_user[0]) mc.credentials.username = c.mqtt_user;
    if (c.mqtt_pass[0]) mc.credentials.authentication.password = c.mqtt_pass;

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_cli = esp_mqtt_client_init(&mc);
    if (s_cli) {
        esp_mqtt_client_register_event(s_cli, ESP_EVENT_ANY_ID, on_mqtt, NULL);
        esp_mqtt_client_start(s_cli);
        ESP_LOGI(TAG, "Conectando a %s como %s", c.mqtt_uri, id);
    } else {
        snprintf(s_info.last_error, sizeof(s_info.last_error), "Dirección del broker inválida");
    }
    xSemaphoreGive(s_mtx);
}

static void mqtt_task(void *arg)
{
    uint32_t cfg_rev = 0, last_tel = 0;
    sys_state_t last_state = SYS_INIT;
    uint32_t last_alarm_mask = 0xFFFF, last_fault_mask = 0xFFFF;

    for (;;) {
        /* El cliente solo arranca cuando hay Wi-Fi con IP: antes de eso la pila
         * TCP/IP no está lista (y en modo configuración no hay internet). */
        app_lock();
        const bool have_net = app_status()->sta_connected && app_status()->sta_ip[0];
        app_unlock();

        if (s_reconf && !have_net) {
            settings_t cfg;
            settings_get(&cfg);
            s_info.enabled = cfg.mqtt_enabled;
            strlcpy(s_info.uri, cfg.mqtt_uri, sizeof(s_info.uri));
            snprintf(s_info.last_error, sizeof(s_info.last_error), "Esperando conexión a tu Wi-Fi");
        }
        if (s_reconf && have_net) {
            s_reconf = false;
            stop_client();
            settings_t cfg;
            settings_get(&cfg);
            start_client(&cfg);
        }

        if (s_connected) {
            settings_t cfg;
            settings_get(&cfg);
            const uint32_t now = app_uptime_s();
            bool force = s_force;
            s_force = false;

            /* Configuración (rangos, nombre, calibración) cuando cambia */
            uint32_t rev = settings_revision();
            if (force || rev != cfg_rev) { cfg_rev = rev; publish_config(); }

            /* Estado cuando cambia */
            app_lock();
            sys_state_t state = app_status()->eng.state;
            uint32_t am = 0, fm = 0;
            for (int i = 0; i < VAR_COUNT; i++) {
                if (app_status()->eng.alarm[i]) am |= 1u << i;
                if (app_status()->r[i].fault) fm |= 1u << i;
            }
            app_unlock();
            bool changed = state != last_state || am != last_alarm_mask || fm != last_fault_mask;
            if (force || changed) {
                last_state = state; last_alarm_mask = am; last_fault_mask = fm;
                publish_state();
            }

            /* Telemetría periódica (y en seguida si cambió el estado) */
            if (force || changed || now - last_tel >= cfg.mqtt_period_s) {
                last_tel = now;
                publish_telemetry();
            }

            /* Eventos pendientes */
            mq_event_t e;
            while (xQueueReceive(s_evq, &e, 0) == pdTRUE) {
                cJSON *j = cJSON_CreateObject();
                cJSON_AddNumberToObject(j, "k", e.kind);
                cJSON_AddStringToObject(j, "msg", e.text);
                cJSON_AddNumberToObject(j, "ts", (double)e.epoch);
                cJSON_AddNumberToObject(j, "up", e.uptime);
                if (e.kind == EV_ALERT) pub_json("alerta", cJSON_Duplicate(j, true), false);
                pub_json("evento", j, false);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* ------------------------------------------------------------------ */
esp_err_t mqtt_link_start(void)
{
    s_mtx = xSemaphoreCreateMutex();
    s_evq = xQueueCreate(10, sizeof(mq_event_t));
    if (!s_mtx || !s_evq) return ESP_ERR_NO_MEM;
    xTaskCreate(mqtt_task, "mqtt_link", 6144, NULL, 4, NULL);
    return ESP_OK;
}

void mqtt_link_reconfigure(void) { s_reconf = true; }

void mqtt_link_get_info(mqtt_info_t *out)
{
    *out = s_info;
    out->connected = s_connected;
}

void mqtt_link_on_event(event_kind_t kind, const char *text)
{
    if (!s_evq) return;
    mq_event_t e = { .kind = (uint8_t)kind, .epoch = app_epoch_now(), .uptime = app_uptime_s() };
    strlcpy(e.text, text, sizeof(e.text));
    if (xQueueSend(s_evq, &e, 0) != pdTRUE) {      /* cola llena: se descarta el más viejo */
        mq_event_t old;
        xQueueReceive(s_evq, &old, 0);
        xQueueSend(s_evq, &e, 0);
    }
}

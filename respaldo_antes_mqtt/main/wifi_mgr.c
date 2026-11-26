/*
 * wifi_mgr.c — Gestión de Wi-Fi.
 *
 *  1. Si hay red guardada, se conecta como estación (STA) y reintenta solo (NF-12).
 *  2. Si no hay red guardada, o no logra conectarse en 60 s, abre además un
 *     punto de acceso "Planta-XXXX" (contraseña planta1234) con portal cautivo.
 *     El monitoreo y las alertas locales siguen funcionando todo el tiempo.
 *  3. Cuando vuelve a conectarse, apaga el punto de acceso 2 minutos después
 *     (tiempo para que el usuario vea en el portal la nueva IP).
 *  Las credenciales las guarda el propio driver de Wi-Fi en NVS y nunca se
 *  devuelven por la web (NF-11).
 */
#include "wifi_mgr.h"
#include <string.h>
#include <stdlib.h>
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_state.h"
#include "dns_server.h"

static const char *TAG = "wifi";

static esp_netif_t *s_sta;
static volatile bool s_connected, s_ap_on, s_scanning, s_sntp_started;
static volatile int64_t s_disc_since_ms, s_conn_since_ms, s_last_try_ms;
static char s_ap_ssid[33];

/* Credenciales pendientes de aplicar (se aplican desde la tarea, no desde la web) */
static volatile bool s_pending_creds, s_pending_forget;
static wifi_config_t s_pending_cfg;

static inline int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static bool has_credentials(void)
{
    wifi_config_t c;
    if (esp_wifi_get_config(WIFI_IF_STA, &c) != ESP_OK) return false;
    return c.sta.ssid[0] != 0;
}

static void start_ap(void)
{
    wifi_config_t ap = { 0 };
    strlcpy((char *)ap.ap.ssid, s_ap_ssid, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(s_ap_ssid);
    strlcpy((char *)ap.ap.password, WIFI_AP_PASSWORD, sizeof(ap.ap.password));
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap.ap.max_connection = 4;
    ap.ap.channel = 1;

    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_AP, &ap);
    dns_server_start();
    s_ap_on = true;
    app_add_event(EV_WIFI, "Modo configuración: red %s activa", s_ap_ssid);
    ESP_LOGI(TAG, "Punto de acceso '%s' (clave %s) -> http://%s", s_ap_ssid, WIFI_AP_PASSWORD, WIFI_AP_IP);
}

static void stop_ap(void)
{
    dns_server_stop();
    esp_wifi_set_mode(WIFI_MODE_STA);
    s_ap_on = false;
    ESP_LOGI(TAG, "Punto de acceso apagado");
}

static void start_sntp_once(void)
{
    if (s_sntp_started) return;
    s_sntp_started = true;
    setenv("TZ", "<-05>5", 1);          /* Colombia (UTC-5). Cámbialo si lo necesitas. */
    tzset();
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&cfg);
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (has_credentials()) { esp_wifi_connect(); s_last_try_ms = now_ms(); }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_connected) {
            s_disc_since_ms = now_ms();
            app_add_event(EV_WIFI, "Se perdió la conexión Wi-Fi");
        }
        s_connected = false;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        char ip[48];
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&ev->ip_info.ip));
        s_connected = true;
        s_conn_since_ms = now_ms();
        wifi_config_t c;
        esp_wifi_get_config(WIFI_IF_STA, &c);
        app_lock();
        strlcpy(app_status()->sta_ip, ip, sizeof(app_status()->sta_ip));
        app_unlock();
        app_add_event(EV_WIFI, "Conectado a %s (IP %s)", (char *)c.sta.ssid, ip);
        start_sntp_once();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "Un dispositivo se conectó al punto de acceso");
    }
}

static void publish_status(void)
{
    wifi_config_t c = { 0 };
    esp_wifi_get_config(WIFI_IF_STA, &c);
    wifi_ap_record_t ap;
    int8_t rssi = 0;
    if (s_connected && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) rssi = ap.rssi;

    app_lock();
    app_status_t *st = app_status();
    st->sta_connected = s_connected;
    st->has_credentials = c.sta.ssid[0] != 0;
    strlcpy(st->sta_ssid, (char *)c.sta.ssid, sizeof(st->sta_ssid));
    st->rssi = rssi;
    st->net_mode = s_ap_on ? NET_APSTA : NET_STA;
    strlcpy(st->ap_ssid, s_ap_ssid, sizeof(st->ap_ssid));
    if (!s_connected) st->sta_ip[0] = 0;
    app_unlock();
}

static void wifi_task(void *arg)
{
    for (;;) {
        const int64_t t = now_ms();

        if (s_pending_forget) {
            s_pending_forget = false;
            wifi_config_t empty = { 0 };
            esp_wifi_disconnect();
            esp_wifi_set_config(WIFI_IF_STA, &empty);
            s_connected = false;
            s_disc_since_ms = t;
        }
        if (s_pending_creds) {
            s_pending_creds = false;
            esp_wifi_disconnect();
            esp_wifi_set_config(WIFI_IF_STA, &s_pending_cfg);   /* se guarda en NVS */
            app_add_event(EV_WIFI, "Probando red %s...", (char *)s_pending_cfg.sta.ssid);
            esp_wifi_connect();
            s_last_try_ms = t;
            s_disc_since_ms = t;
        }

        const bool creds = has_credentials();

        /* Reconexión automática (SR-25) */
        if (!s_connected && creds && !s_scanning) {
            int64_t interval = s_ap_on ? 15000 : 5000;     /* con AP activo, menos intentos */
            if (t - s_last_try_ms >= interval) {
                esp_wifi_connect();
                s_last_try_ms = t;
            }
        }
        /* Abrir portal si no hay red o falla de forma sostenida */
        if (!s_ap_on && !s_connected && (!creds || t - s_disc_since_ms >= 60000)) start_ap();
        /* Cerrar portal un rato después de conectarse */
        if (s_ap_on && s_connected && t - s_conn_since_ms >= 120000) stop_ap();

        publish_status();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t wifi_mgr_start(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_sta = esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    esp_netif_set_hostname(s_sta, MDNS_HOSTNAME);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL));

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "Planta-%02X%02X", mac[4], mac[5]);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* mDNS: http://planta.local (funciona en la mayoría de PC, iPhone y Android recientes) */
    if (mdns_init() == ESP_OK) {
        mdns_hostname_set(MDNS_HOSTNAME);
        mdns_instance_name_set("Monitoreo de plantas");
        mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    }

    s_disc_since_ms = now_ms();
    xTaskCreate(wifi_task, "wifi_mgr", 4096, NULL, 4, NULL);
    return ESP_OK;
}

esp_err_t wifi_mgr_set_credentials(const char *ssid, const char *pass)
{
    if (!ssid || !ssid[0] || strlen(ssid) > 32 || (pass && strlen(pass) > 63)) return ESP_ERR_INVALID_ARG;
    if (pass && pass[0] && strlen(pass) < 8) return ESP_ERR_INVALID_ARG;    /* WPA2: ≥ 8 */
    memset(&s_pending_cfg, 0, sizeof(s_pending_cfg));
    strlcpy((char *)s_pending_cfg.sta.ssid, ssid, sizeof(s_pending_cfg.sta.ssid));
    if (pass) strlcpy((char *)s_pending_cfg.sta.password, pass, sizeof(s_pending_cfg.sta.password));
    s_pending_creds = true;
    return ESP_OK;
}

void wifi_mgr_forget(void)
{
    s_pending_forget = true;
}

int wifi_mgr_scan(wifi_scan_item_t *out, int max)
{
    s_scanning = true;
    esp_err_t e = esp_wifi_scan_start(NULL, true);
    if (e != ESP_OK) {                        /* puede fallar si justo está conectando */
        vTaskDelay(pdMS_TO_TICKS(1500));
        e = esp_wifi_scan_start(NULL, true);
    }
    int count = 0;
    if (e == ESP_OK) {
        uint16_t n = 20;
        wifi_ap_record_t *recs = calloc(n, sizeof(wifi_ap_record_t));
        if (recs && esp_wifi_scan_get_ap_records(&n, recs) == ESP_OK) {
            for (int i = 0; i < n && count < max; i++) {
                if (!recs[i].ssid[0]) continue;
                bool dup = false;                  /* quitar redes repetidas */
                for (int j = 0; j < count; j++)
                    if (strcmp(out[j].ssid, (char *)recs[i].ssid) == 0) { dup = true; break; }
                if (dup) continue;
                strlcpy(out[count].ssid, (char *)recs[i].ssid, sizeof(out[count].ssid));
                out[count].rssi = recs[i].rssi;
                out[count].secure = recs[i].authmode != WIFI_AUTH_OPEN;
                count++;
            }
        } else {
            esp_wifi_clear_ap_list();
        }
        free(recs);
    }
    s_scanning = false;
    return count;
}

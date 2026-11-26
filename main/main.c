/*
 * main.c — Sistema inteligente de monitoreo y cuidado de plantas (DR-PS-02)
 * Placa: ESP32-C3-WROOM-02 (esquemático Revisión A)
 *
 * Tareas:
 *   sensores  (este archivo)  lee sensores cada N s, filtra, compara y actualiza alertas
 *   ui_local  (local_ui.c)    LED RGB, zumbador, pulsadores y PIR cada 20 ms
 *   wifi_mgr  (wifi_mgr.c)    conexión, reconexión y portal de configuración
 *   httpd     (web_server.c)  página web y API
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "nvs_flash.h"

#include "board.h"
#include "app_state.h"
#include "settings.h"
#include "alerts.h"
#include "i2c_bus.h"
#include "sensors.h"
#include "oled.h"
#include "local_ui.h"
#include "mqtt_planta.h"
#include "wifi_mgr.h"
#include "web_server.h"

static const char *TAG = "main";

/* Lecturas propias de esta tarea (se copian al estado compartido en cada ciclo) */
static reading_t s_rd[VAR_COUNT];

/* Promedio móvil de las lecturas SIN corregir (para la calibración) */
#define RAW_N 6
static float s_raw[3][RAW_N];
static uint8_t s_raw_n[3], s_raw_i[3];

static void raw_push(int k, bool ok, float v)
{
    if (!ok) return;
    s_raw[k][s_raw_i[k]] = v;
    s_raw_i[k] = (s_raw_i[k] + 1) % RAW_N;
    if (s_raw_n[k] < RAW_N) s_raw_n[k]++;
}

static float raw_avg(int k)
{
    float sum = 0;
    for (int i = 0; i < s_raw_n[k]; i++) sum += s_raw[k][i];
    return s_raw_n[k] ? sum / s_raw_n[k] : 0;
}

/* Actualiza una lectura con detección de falla: 3 errores seguidos = falla (SR-07) */
static void update_reading(var_id_t id, bool ok, float v)
{
    reading_t *r = &s_rd[id];
    if (ok && v >= SENSOR_PHYS_MIN[id] && v <= SENSOR_PHYS_MAX[id]) {
        r->value = v;
        r->valid = true;
        r->fail_count = 0;
        if (r->fault) {
            r->fault = false;
            app_add_event(EV_OK, "%s: sensor recuperado", alerts_var_label(id));
        }
    } else {
        if (r->fail_count < 255) r->fail_count++;
        if (r->fail_count >= 3 && !r->fault) {
            r->fault = true;
            app_add_event(EV_FAULT, "Falla de sensor: %s", alerts_var_label(id));
        }
    }
}

static const char *dir_text(int8_t d) { return d < 0 ? "BAJO" : "ALTO"; }

static void render_oled(const settings_t *cfg)
{
    if (!oled_present()) return;
    app_status_t *st = malloc(sizeof(app_status_t));
    if (!st) return;
    app_snapshot(st);
    char l[64];

    oled_clear();
    const char *est = st->eng.state == SYS_ALERT ? "ALERTA" :
                      st->eng.state == SYS_ACKED ? "RECONOCIDA" :
                      st->eng.state == SYS_NORMAL ? "NORMAL" : "INICIANDO";
    snprintf(l, sizeof(l), " Planta: %s", est);
    oled_text(0, 0, l);
    oled_invert_row(0);

    #define VAL(i, fmt, buf) do { \
        if (st->r[i].fault) snprintf(buf, sizeof(buf), "falla"); \
        else if (!st->r[i].valid) snprintf(buf, sizeof(buf), "--"); \
        else snprintf(buf, sizeof(buf), fmt, st->r[i].value); } while (0)
    char a[10], b[10];
    VAL(VAR_TEMP, "%.1f~C", a);
    VAL(VAR_HUM, "%.0f%%", b);
    snprintf(l, sizeof(l), "T %-8s HR %s", a, b);
    oled_text(0, 1, l);
    VAL(VAR_LUX, "%.0f lx", a);
    snprintf(l, sizeof(l), "Luz   %s", a);
    oled_text(0, 2, l);
    VAL(VAR_SOIL, "%.0f%%", a);
    snprintf(l, sizeof(l), "Suelo %s (%dmV)", a, st->soil_mv);
    oled_text(0, 3, l);
    #undef VAL

    if (st->pir_warming) snprintf(l, sizeof(l), "PIR calentando...");
    else snprintf(l, sizeof(l), "Movimientos: %lu", (unsigned long)st->motion_count);
    oled_text(0, 4, l);

    if (st->sta_connected) snprintf(l, sizeof(l), "IP %s", st->sta_ip);
    else if (st->net_mode != NET_STA) snprintf(l, sizeof(l), "AP %s", st->ap_ssid);
    else snprintf(l, sizeof(l), "WiFi: sin conexion");
    oled_text(0, 5, l);

    /* Primera variable en alarma o en falla */
    for (int i = 0; i < VAR_COUNT; i++) {
        static const char *corto[VAR_COUNT] = { "TEMP", "HUMEDAD", "LUZ", "SUELO" };
        if (st->eng.alarm[i]) {
            snprintf(l, sizeof(l), "! %s %s", corto[i], dir_text(st->eng.dir[i]));
            oled_text(0, 6, l);
            break;
        }
        if (st->r[i].fault) {
            snprintf(l, sizeof(l), "! Falla %s", corto[i]);
            oled_text(0, 6, l);
            break;
        }
    }
    if (st->eng.state == SYS_ALERT) oled_text(0, 7, "ACK = silenciar");
    free(st);
    oled_flush();
}

static float s_boot_t;
static bool s_boot_t_valid;

static void sensor_task(void *arg)
{
    esp_task_wdt_add(NULL);                 /* NF-12: watchdog vigila esta tarea */
    bool aht_ok = aht10_init() == ESP_OK;
    bool bh_ok = bh1750_init() == ESP_OK;

    for (;;) {
        settings_t cfg;
        settings_get(&cfg);
        const TickType_t t0 = xTaskGetTickCount();

        /* --- Temperatura y humedad relativa (AHT10) --- */
        float t = 0, h = 0;
        if (!aht_ok) aht_ok = aht10_init() == ESP_OK;     /* reintento si se desconectó */
        bool ok = aht_ok && aht10_read(&t, &h) == ESP_OK;
        if (!ok) aht_ok = false;
        raw_push(0, ok, t);
        raw_push(1, ok, h);
        if (ok && !s_boot_t_valid) {        /* referencia para la calibración en frío */
            s_boot_t = t;
            s_boot_t_valid = true;
            ESP_LOGI(TAG, "Temperatura al encender (sin corregir): %.2f °C", t);
        }
        /* Correcciones de calibración (guía de calibración, sección 6) */
        float t_c = t + cfg.t_offset;
        float h_c = cfg.hr_gain * h + cfg.hr_offset;
        if (h_c < 0) h_c = 0;
        if (h_c > 100) h_c = 100;
        update_reading(VAR_TEMP, ok, t_c);
        update_reading(VAR_HUM, ok, h_c);

        /* --- Iluminancia (BH1750) --- */
        float lux = 0;
        if (!bh_ok) bh_ok = bh1750_init() == ESP_OK;
        ok = bh_ok && bh1750_read(&lux) == ESP_OK;
        if (!ok) bh_ok = false;
        raw_push(2, ok, lux);
        float lux_c = lux * cfg.lux_k;
        if (lux_c > SENSOR_PHYS_MAX[VAR_LUX]) lux_c = SENSOR_PHYS_MAX[VAR_LUX];
        update_reading(VAR_LUX, ok, lux_c);

        /* --- Humedad del suelo (ADC1) --- */
        int mv = 0;
        ok = soil_read_mv(&mv) == ESP_OK;
        /* Con el sensor desconectado, C10 se descarga y la tensión cae casi a 0 */
        bool soil_ok = ok && mv > 300;
        update_reading(VAR_SOIL, soil_ok, soil_percent(mv, cfg.soil_dry_mv, cfg.soil_wet_mv));

        /* --- Comparación y alertas (F-08, F-09) --- */
        app_lock();
        app_status_t *st = app_status();
        memcpy(st->r, s_rd, sizeof(s_rd));
        st->soil_mv = mv;
        st->soil_saturated = mv >= SOIL_ADC_MAX_MV - 60;
        st->cycles++;
        for (int k = 0; k < 3; k++) {
            st->raw_valid[k] = s_raw_n[k] > 0;
            st->raw_avg[k] = raw_avg(k);
        }
        st->boot_t = s_boot_t;
        st->boot_t_valid = s_boot_t_valid;
        sys_state_t before = st->eng.state;
        uint32_t back = 0;
        uint32_t fresh = alerts_update(&st->eng, s_rd, cfg.range, cfg.persistence, &back);
        sys_state_t after = st->eng.state;
        int8_t dirs[VAR_COUNT];
        memcpy(dirs, st->eng.dir, sizeof(dirs));
        app_unlock();

        for (int i = 0; i < VAR_COUNT; i++) {
            if (fresh & (1u << i))
                app_add_event(EV_ALERT, "%s fuera de rango (%s): %.1f",
                              alerts_var_label(i), dirs[i] < 0 ? "bajo" : "alto", s_rd[i].value);
            if (back & (1u << i))
                app_add_event(EV_OK, "%s volvió al rango", alerts_var_label(i));
        }
        if (before == SYS_INIT) app_add_event(EV_INFO, "Monitoreo iniciado");
        else if (after == SYS_NORMAL && before != SYS_NORMAL)
            app_add_event(EV_OK, "Todas las variables en rango");

        ESP_LOGI(TAG, "T=%.1f°C HR=%.1f%% Luz=%.0f lx Suelo=%.0f%% (%d mV) estado=%s",
                 s_rd[VAR_TEMP].value, s_rd[VAR_HUM].value, s_rd[VAR_LUX].value,
                 s_rd[VAR_SOIL].value, mv, alerts_state_name(after));

        render_oled(&cfg);
        mqtt_planta_publicar();                 /* manda los valores a la página (MQTT) */
        esp_task_wdt_reset();

        /* Esperar hasta completar el período de muestreo (SR-06) */
        const TickType_t period = pdMS_TO_TICKS(cfg.sample_period_s * 1000);
        TickType_t spent = xTaskGetTickCount() - t0;
        if (spent < period) vTaskDelay(period - spent);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== Monitoreo de plantas (%s) ===", CONFIG_IDF_TARGET);

    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        e = nvs_flash_init();
    }
    ESP_ERROR_CHECK(e);

    settings_init();
    app_state_init();
    local_ui_start();                       /* LED blanco = inicialización */

    ESP_ERROR_CHECK(i2c_bus_init());
    ESP_LOGI(TAG, "Escaneando bus I2C...");
    i2c_bus_scan_log();
    if (oled_init() == ESP_OK) {
        app_lock(); app_status()->oled_present = true; app_unlock();
        oled_text(0, 3, "  Iniciando...");
        oled_flush();
    }
    ESP_ERROR_CHECK(soil_init());

    wifi_mgr_start();                       /* se conecta a la red de config_red.h */
    mqtt_planta_start();                    /* arranca MQTT apenas haya Wi-Fi       */
    web_server_start();

    xTaskCreate(sensor_task, "sensores", 6144, NULL, 5, NULL);
}

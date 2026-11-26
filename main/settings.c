/*
 * settings.c — Carga, validación y guardado de parámetros en NVS.
 */
#include "settings.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>

const float SENSOR_PHYS_MIN[VAR_COUNT] = { -40.0f,   0.0f,     0.0f,   0.0f };
const float SENSOR_PHYS_MAX[VAR_COUNT] = {  85.0f, 100.0f, 65535.0f, 100.0f };

/* ===== Calibración FIJA de este equipo (medida en la prueba de concepto, nov-2026) =====
 * Ya no se calibra desde la página. Si cambias un sensor, cambia estos números y vuelve a grabar. */
#define CAL_SOIL_SECO_MV     2266     /* sensor al aire                       */
#define CAL_SOIL_HUMEDO_MV    885     /* sensor en agua                       */
#define CAL_T_OFFSET        (-0.6f)   /* °C   (método de arranque en frío)     */
#define CAL_HR_GANANCIA      (1.0f)   /* HR = ganancia·HR + offset (sal)       */
#define CAL_HR_OFFSET      (-15.1f)   /* %HR                                   */
#define CAL_LUX_K           (0.969f)  /* lux = k·lux (comparado con luxómetro) */

static void apply_fixed_cal(settings_t *s)
{
    s->soil_dry_mv = CAL_SOIL_SECO_MV;
    s->soil_wet_mv = CAL_SOIL_HUMEDO_MV;
    s->t_offset    = CAL_T_OFFSET;
    s->hr_gain     = CAL_HR_GANANCIA;
    s->hr_offset   = CAL_HR_OFFSET;
    s->hr_p1_ref   = 0.0f;
    s->hr_p1_raw   = 0.0f;
    s->lux_k       = CAL_LUX_K;
}

void settings_defaults(settings_t *s)
{
    memset(s, 0, sizeof(*s));
    s->version = SETTINGS_VERSION;
    /* Rangos de ejemplo del Anexo A (no son recomendaciones agronómicas) */
    s->range[VAR_TEMP] = (range_t){ 18.0f,    28.0f,   0.5f };
    s->range[VAR_HUM]  = (range_t){ 40.0f,    70.0f,   2.0f };
    s->range[VAR_LUX]  = (range_t){ 1000.0f, 10000.0f, 100.0f };
    s->range[VAR_SOIL] = (range_t){ 30.0f,    70.0f,   3.0f };
    s->persistence     = 3;
    s->sample_period_s = 5;
    strcpy(s->web_pass,   "planta123");
    strcpy(s->plant_name, "Mi planta");
    apply_fixed_cal(s);
}

bool settings_validate(const settings_t *s, char *err, size_t n)
{
    for (int i = 0; i < VAR_COUNT; i++) {
        const range_t *r = &s->range[i];
        const char *name = alerts_var_label((var_id_t)i);
        if (r->min != r->min || r->max != r->max || r->hyst != r->hyst) {  /* NaN */
            snprintf(err, n, "%s: valor no numérico", name);
            return false;
        }
        if (r->min < SENSOR_PHYS_MIN[i] || r->max > SENSOR_PHYS_MAX[i]) {
            snprintf(err, n, "%s: fuera del rango físico del sensor (%.0f a %.0f)",
                     name, SENSOR_PHYS_MIN[i], SENSOR_PHYS_MAX[i]);
            return false;
        }
        if (!(r->min < r->max)) {
            snprintf(err, n, "%s: el mínimo debe ser menor que el máximo", name);
            return false;
        }
        if (r->hyst < 0 || (r->max - r->min) <= 2.0f * r->hyst) {
            snprintf(err, n, "%s: la histéresis debe ser ≥ 0 y menor que la mitad del rango", name);
            return false;
        }
    }
    if (s->persistence < 1 || s->persistence > 20) {
        snprintf(err, n, "Persistencia: debe estar entre 1 y 20 lecturas");
        return false;
    }
    if (s->sample_period_s < 2 || s->sample_period_s > 60) {
        snprintf(err, n, "Período de muestreo: entre 2 y 60 s");
        return false;
    }
    if (s->soil_dry_mv - s->soil_wet_mv < 200) {
        snprintf(err, n, "Calibración de suelo: 'seco' debe ser al menos 200 mV mayor que 'húmedo'");
        return false;
    }
    if (!(s->t_offset >= -10.0f && s->t_offset <= 10.0f)) {
        snprintf(err, n, "Corrección de temperatura fuera de rango (±10 °C)");
        return false;
    }
    if (!(s->hr_gain >= 0.5f && s->hr_gain <= 1.5f) || !(s->hr_offset >= -30.0f && s->hr_offset <= 30.0f)) {
        snprintf(err, n, "Corrección de humedad fuera de rango (ganancia 0,5–1,5 y offset ±30 %%HR)");
        return false;
    }
    if (!(s->lux_k >= 0.2f && s->lux_k <= 5.0f)) {
        snprintf(err, n, "Factor de luz fuera de rango (0,2–5)");
        return false;
    }
    if (strlen(s->web_pass) < 6) {
        snprintf(err, n, "La contraseña debe tener al menos 6 caracteres");
        return false;
    }
    return true;
}

#ifndef SETTINGS_HOST_TEST
/* ------------------------------------------------------------------ */
/*                    Parte que usa ESP-IDF (NVS + mutex)              */
/* ------------------------------------------------------------------ */
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "settings";
static const char *NVS_NS = "planta";
static const char *NVS_KEY = "cfg";
static settings_t s_cur;
static SemaphoreHandle_t s_mtx;

static esp_err_t save_to_nvs(const settings_t *s)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    e = nvs_set_blob(h, NVS_KEY, s, sizeof(*s));
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "Parámetros guardados en NVS (%s)", esp_err_to_name(e));
    return e;
}

esp_err_t settings_init(void)
{
    s_mtx = xSemaphoreCreateMutex();
    settings_defaults(&s_cur);

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        settings_t tmp;
        settings_defaults(&tmp);                 /* lo que no venga guardado queda por defecto */
        size_t len = 0;
        esp_err_t e = nvs_get_blob(h, NVS_KEY, NULL, &len);   /* tamaño guardado */
        bool migrated = false;
        if (e == ESP_OK && len <= sizeof(tmp)) e = nvs_get_blob(h, NVS_KEY, &tmp, &len);
        else if (e == ESP_OK && len > sizeof(tmp) && len <= 512) {
            /* Lo guardó la versión con MQTT (v3): mismos campos + datos MQTT al final.
             * Se conservan rangos, nombre y contraseña; lo de MQTT se descarta. */
            uint8_t *big = malloc(len);
            if (big && nvs_get_blob(h, NVS_KEY, big, &len) == ESP_OK && ((settings_t *)big)->version == 3) {
                memcpy(&tmp, big, sizeof(tmp));
                tmp.version = SETTINGS_VERSION;
                migrated = true;
            } else e = ESP_FAIL;
            free(big);
        } else e = ESP_FAIL;
        nvs_close(h);
        if (e == ESP_OK && tmp.version == 1 && len == SETTINGS_V1_SIZE) {
            /* v1 -> v2: se conservan rangos y calibración de suelo; calibración nueva por defecto */
            settings_t d; settings_defaults(&d);
            memcpy((uint8_t *)&tmp + SETTINGS_V1_SIZE, (uint8_t *)&d + SETTINGS_V1_SIZE,
                   sizeof(tmp) - SETTINGS_V1_SIZE);
            tmp.version = SETTINGS_VERSION;
            migrated = true;
        }
        char err[96];
        apply_fixed_cal(&tmp);                   /* la calibración siempre es la fija */
        if (e == ESP_OK && tmp.version == SETTINGS_VERSION &&
            (migrated || len == sizeof(tmp)) && settings_validate(&tmp, err, sizeof(err))) {
            s_cur = tmp;
            ESP_LOGI(TAG, "Parámetros cargados de NVS%s", migrated ? " (migrados)" : "");
            if (migrated) save_to_nvs(&s_cur);
            return ESP_OK;
        }
    }
    ESP_LOGW(TAG, "Sin parámetros válidos en NVS: usando valores por defecto");
    return save_to_nvs(&s_cur);
}

void settings_get(settings_t *out)
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    *out = s_cur;
    xSemaphoreGive(s_mtx);
}

esp_err_t settings_apply(const settings_t *s, char *err, size_t n)
{
    if (!settings_validate(s, err, n)) return ESP_ERR_INVALID_ARG;
    esp_err_t e = ESP_OK;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (memcmp(&s_cur, s, sizeof(*s)) != 0) {     /* SR-26: escribir solo si cambió */
        s_cur = *s;
        s_cur.version = SETTINGS_VERSION;
        e = save_to_nvs(&s_cur);
    }
    xSemaphoreGive(s_mtx);
    if (e != ESP_OK) snprintf(err, n, "Error al guardar en memoria: %s", esp_err_to_name(e));
    return e;
}

esp_err_t settings_reset_calibration(const char *which)
{
    settings_t d, cur;
    settings_defaults(&d);
    settings_get(&cur);
    bool all = strcmp(which, "all") == 0;
    if (all || strcmp(which, "t") == 0)   cur.t_offset = d.t_offset;
    if (all || strcmp(which, "hr") == 0)  { cur.hr_gain = d.hr_gain; cur.hr_offset = d.hr_offset;
                                            cur.hr_p1_ref = 0; cur.hr_p1_raw = 0; }
    if (all || strcmp(which, "lux") == 0) cur.lux_k = d.lux_k;
    if (all || strcmp(which, "soil") == 0) { cur.soil_dry_mv = d.soil_dry_mv; cur.soil_wet_mv = d.soil_wet_mv; }
    char err[96];
    return settings_apply(&cur, err, sizeof(err));
}

esp_err_t settings_reset_ranges(void)
{
    settings_t d, cur;
    settings_defaults(&d);
    settings_get(&cur);
    /* Se reinician los rangos y tiempos; se conservan calibración, contraseña y nombre */
    memcpy(cur.range, d.range, sizeof(cur.range));
    cur.persistence = d.persistence;
    cur.sample_period_s = d.sample_period_s;
    char err[96];
    return settings_apply(&cur, err, sizeof(err));
}
#endif

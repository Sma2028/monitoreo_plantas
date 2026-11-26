/*
 * settings.h — Parámetros configurables guardados en NVS (F-06, F-07, F-17).
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stddef.h>
#include "alerts.h"

#define SETTINGS_VERSION   2
#define SETTINGS_PASS_LEN  33
#define SETTINGS_NAME_LEN  33

typedef struct {
    uint32_t version;
    range_t  range[VAR_COUNT];
    uint8_t  persistence;           /* lecturas consecutivas (SR-12)        */
    uint16_t sample_period_s;       /* período de muestreo (SR-06)          */
    int32_t  soil_dry_mv;           /* calibración: suelo seco               */
    int32_t  soil_wet_mv;           /* calibración: suelo húmedo             */
    char     web_pass[SETTINGS_PASS_LEN];
    char     plant_name[SETTINGS_NAME_LEN];
    /* ---- Calibración (versión 2). Se añaden al FINAL para poder migrar la v1 ---- */
    float    t_offset;              /* T_corr  = T + t_offset                 */
    float    hr_gain;               /* HR_corr = hr_gain * HR + hr_offset     */
    float    hr_offset;
    float    hr_p1_ref;             /* punto 1 de humedad (para el 2.º punto) */
    float    hr_p1_raw;
    float    lux_k;                 /* lux_corr = lux_k * lux                 */
} settings_t;

/* Versión 1 de la estructura (sin calibración), para migrar lo guardado */
#define SETTINGS_V1_SIZE  offsetof(settings_t, t_offset)

/* Rango físico de cada sensor (Anexo A) */
extern const float SENSOR_PHYS_MIN[VAR_COUNT];
extern const float SENSOR_PHYS_MAX[VAR_COUNT];

void settings_defaults(settings_t *s);

/* Valida coherencia. Devuelve true si es válido; si no, escribe el motivo en err. */
bool settings_validate(const settings_t *s, char *err, size_t err_len);

#ifndef SETTINGS_HOST_TEST
#include "esp_err.h"
esp_err_t settings_init(void);                 /* carga desde NVS (o valores por defecto) */
void      settings_get(settings_t *out);       /* copia segura (con mutex)                */
/* Valida, aplica y guarda SOLO si cambió algo (SR-26). */
esp_err_t settings_apply(const settings_t *s, char *err, size_t err_len);
esp_err_t settings_reset_ranges(void);         /* pulsación larga de BOOT (F-12)          */
esp_err_t settings_reset_calibration(const char *which); /* "t", "hr", "lux", "soil" o "all" */
#endif

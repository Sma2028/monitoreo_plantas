/*
 * cfg_json.h — Convierte la configuración a JSON y aplica cambios recibidos en JSON.
 * Lo usan la página web local (/api/config) y MQTT (plantas/<id>/config y cmd/rangos),
 * así los dos caminos validan exactamente igual.
 */
#pragma once
#include <stdbool.h>
#include "cJSON.h"
#include "settings.h"

/* {"name","persistence","period","soil_dry","soil_wet","ranges":{temp:{min,max,hyst},...},"cal":{...}} */
cJSON *cfg_json_build(const settings_t *s);

/* Aplica a *s los campos presentes en j (cambios parciales). Campos: name, persistence,
 * period, ranges.{temp,hum,lux,soil}.{min,max,hyst}. Devuelve true si había algo. */
bool cfg_json_apply_ranges(const cJSON *j, settings_t *s);

/* Igual para calibración: t_offset, hr_gain, hr_offset, hr_p1_ref, hr_p1_raw, lux_k. */
bool cfg_json_apply_cal(const cJSON *j, settings_t *s);

#include "cfg_json.h"
#include <string.h>

static bool num(const cJSON *o, const char *k, double *out)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(o, k);
    if (!cJSON_IsNumber(it)) return false;
    *out = it->valuedouble;
    return true;
}

cJSON *cfg_json_build(const settings_t *s)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", s->plant_name);
    cJSON_AddNumberToObject(root, "persistence", s->persistence);
    cJSON_AddNumberToObject(root, "period", s->sample_period_s);
    cJSON_AddNumberToObject(root, "soil_dry", s->soil_dry_mv);
    cJSON_AddNumberToObject(root, "soil_wet", s->soil_wet_mv);
    cJSON *rg = cJSON_AddObjectToObject(root, "ranges");
    for (int i = 0; i < VAR_COUNT; i++) {
        cJSON *o = cJSON_AddObjectToObject(rg, alerts_var_key((var_id_t)i));
        cJSON_AddNumberToObject(o, "min", s->range[i].min);
        cJSON_AddNumberToObject(o, "max", s->range[i].max);
        cJSON_AddNumberToObject(o, "hyst", s->range[i].hyst);
    }
    cJSON *cal = cJSON_AddObjectToObject(root, "cal");
    cJSON_AddNumberToObject(cal, "t_offset", s->t_offset);
    cJSON_AddNumberToObject(cal, "hr_gain", s->hr_gain);
    cJSON_AddNumberToObject(cal, "hr_offset", s->hr_offset);
    cJSON_AddNumberToObject(cal, "lux_k", s->lux_k);
    return root;
}

bool cfg_json_apply_ranges(const cJSON *j, settings_t *s)
{
    bool any = false;
    double d;
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(j, "name");
    if (cJSON_IsString(name) && name->valuestring[0]) {
        strlcpy(s->plant_name, name->valuestring, sizeof(s->plant_name));
        any = true;
    }
    if (num(j, "persistence", &d)) { s->persistence = (uint8_t)(d < 0 ? 0 : d > 255 ? 255 : d); any = true; }
    if (num(j, "period", &d))      { s->sample_period_s = (uint16_t)(d < 0 ? 0 : d > 65535 ? 65535 : d); any = true; }
    const cJSON *rg = cJSON_GetObjectItemCaseSensitive(j, "ranges");
    for (int i = 0; i < VAR_COUNT && rg; i++) {
        const cJSON *o = cJSON_GetObjectItemCaseSensitive(rg, alerts_var_key((var_id_t)i));
        if (!o) continue;
        if (num(o, "min", &d))  { s->range[i].min = (float)d;  any = true; }
        if (num(o, "max", &d))  { s->range[i].max = (float)d;  any = true; }
        if (num(o, "hyst", &d)) { s->range[i].hyst = (float)d; any = true; }
    }
    return any;
}

bool cfg_json_apply_cal(const cJSON *j, settings_t *s)
{
    bool any = false;
    double d;
    if (num(j, "t_offset", &d))  { s->t_offset = (float)d;  any = true; }
    if (num(j, "hr_gain", &d))   { s->hr_gain = (float)d;   any = true; }
    if (num(j, "hr_offset", &d)) { s->hr_offset = (float)d; any = true; }
    if (num(j, "hr_p1_ref", &d)) { s->hr_p1_ref = (float)d; any = true; }
    if (num(j, "hr_p1_raw", &d)) { s->hr_p1_raw = (float)d; any = true; }
    if (num(j, "lux_k", &d))     { s->lux_k = (float)d;     any = true; }
    return any;
}

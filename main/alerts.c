/*
 * alerts.c — Lógica de alertas con persistencia e histéresis.
 *
 *  Entrar en alarma : la variable está FUERA de [min, max] durante
 *                     'persistence' lecturas consecutivas (SR-12).
 *  Salir de alarma  : la variable vuelve DENTRO de [min+hyst, max-hyst] (SR-13).
 *  Reconocer        : solo silencia (ALERT -> ACKED); no borra la condición (SR-18).
 *  Nueva variable   : si otra variable entra en alarma estando en ACKED,
 *                     se vuelve a ALERT (regla de la sección 7).
 *  Falla de sensor  : la variable con falla no genera alertas (SR-07 / F-21).
 */
#include "alerts.h"
#include <string.h>

void alerts_init(alert_engine_t *e)
{
    memset(e, 0, sizeof(*e));
    e->state = SYS_INIT;
}

uint32_t alerts_update(alert_engine_t *e, const reading_t r[VAR_COUNT],
                       const range_t rg[VAR_COUNT], uint8_t persistence,
                       uint32_t *returned_mask)
{
    uint32_t new_mask = 0, back_mask = 0;
    if (persistence == 0) persistence = 1;

    for (int i = 0; i < VAR_COUNT; i++) {
        if (!r[i].valid || r[i].fault) {
            /* Sin dato fiable: no se generan falsas alertas de esta variable */
            if (e->alarm[i]) back_mask |= (1u << i);
            e->out_count[i] = 0;
            e->alarm[i] = false;
            e->dir[i] = 0;
            continue;
        }
        const float v = r[i].value;
        const bool low  = v < rg[i].min;
        const bool high = v > rg[i].max;

        if (!e->alarm[i]) {
            if (low || high) {
                if (e->out_count[i] < 255) e->out_count[i]++;
                if (e->out_count[i] >= persistence) {
                    e->alarm[i] = true;
                    e->dir[i] = low ? -1 : 1;
                    new_mask |= (1u << i);
                }
            } else {
                e->out_count[i] = 0;
            }
        } else {
            const bool back_in = (v >= rg[i].min + rg[i].hyst) &&
                                 (v <= rg[i].max - rg[i].hyst);
            if (back_in) {
                e->alarm[i] = false;
                e->dir[i] = 0;
                e->out_count[i] = 0;
                back_mask |= (1u << i);
            } else if (low || high) {
                e->dir[i] = low ? -1 : 1;
            }
        }
    }

    bool any = false;
    for (int i = 0; i < VAR_COUNT; i++) any |= e->alarm[i];

    if (!any) {
        e->state = SYS_NORMAL;                 /* T1, T4, T5 */
    } else if (new_mask) {
        e->state = SYS_ALERT;                  /* T2, o nueva alerta desde ACKED */
    } else if (e->state != SYS_ACKED) {
        e->state = SYS_ALERT;
    }

    if (returned_mask) *returned_mask = back_mask;
    return new_mask;
}

bool alerts_ack(alert_engine_t *e)
{
    if (e->state == SYS_ALERT) {               /* T3 */
        e->state = SYS_ACKED;
        return true;
    }
    return false;
}

const char *alerts_state_name(sys_state_t s)
{
    switch (s) {
    case SYS_NORMAL: return "normal";
    case SYS_ALERT:  return "alerta";
    case SYS_ACKED:  return "reconocida";
    default:         return "inicio";
    }
}

const char *alerts_var_key(var_id_t v)
{
    static const char *k[VAR_COUNT] = { "temp", "hum", "lux", "soil" };
    return (v < VAR_COUNT) ? k[v] : "?";
}

const char *alerts_var_label(var_id_t v)
{
    static const char *k[VAR_COUNT] = {
        "Temperatura", "Humedad relativa", "Iluminancia", "Humedad del suelo"
    };
    return (v < VAR_COUNT) ? k[v] : "?";
}

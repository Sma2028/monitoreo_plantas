/*
 * alerts.h — Máquina de estados de alertas (sección 7 del DR-PS-02).
 * Código C puro (sin ESP-IDF) para poder probarlo en el PC.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { VAR_TEMP = 0, VAR_HUM, VAR_LUX, VAR_SOIL, VAR_COUNT } var_id_t;

typedef struct {
    float min;
    float max;
    float hyst;     /* margen de histéresis para volver a "normal" */
} range_t;

typedef struct {
    float   value;
    bool    valid;      /* hay al menos una lectura buena                     */
    bool    fault;      /* falla declarada (3 lecturas malas seguidas, SR-07)  */
    uint8_t fail_count;
} reading_t;

typedef enum {
    SYS_INIT = 0,       /* inicialización                                     */
    SYS_NORMAL,         /* LED verde                                          */
    SYS_ALERT,          /* LED rojo parpadeante + zumbador                    */
    SYS_ACKED,          /* alerta reconocida: LED amarillo, zumbador silenciado */
} sys_state_t;

typedef struct {
    uint8_t     out_count[VAR_COUNT]; /* lecturas consecutivas fuera de rango */
    bool        alarm[VAR_COUNT];     /* variable en alarma                   */
    int8_t      dir[VAR_COUNT];       /* -1 bajo el mínimo, +1 sobre el máximo */
    sys_state_t state;
} alert_engine_t;

void alerts_init(alert_engine_t *e);

/* Procesa un ciclo de muestreo.
 * Devuelve una máscara de bits con las variables que ACABAN de entrar en alarma
 * (bit i = variable i). *returned_mask recibe las que acaban de volver a normal. */
uint32_t alerts_update(alert_engine_t *e,
                       const reading_t r[VAR_COUNT],
                       const range_t rg[VAR_COUNT],
                       uint8_t persistence,
                       uint32_t *returned_mask);

/* Reconocer: ALERT -> ACKED. Devuelve true si cambió algo. */
bool alerts_ack(alert_engine_t *e);

const char *alerts_state_name(sys_state_t s);   /* "inicio", "normal", ... */
const char *alerts_var_key(var_id_t v);         /* "temp", "hum", ...      */
const char *alerts_var_label(var_id_t v);       /* "Temperatura", ...      */

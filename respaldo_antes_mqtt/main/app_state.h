/*
 * app_state.h — Estado compartido entre tareas (sensores, interfaz local, web).
 * Todo acceso va entre app_lock() / app_unlock().
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "alerts.h"

#define EVENT_LOG_LEN   12
#define EVENT_TEXT_LEN  64

typedef enum { EV_INFO = 0, EV_ALERT, EV_OK, EV_FAULT, EV_MOTION, EV_WIFI } event_kind_t;

typedef struct {
    time_t   epoch;          /* hora real (0 si aún no hay NTP) */
    uint32_t uptime_s;       /* segundos desde el arranque      */
    uint8_t  kind;
    char     text[EVENT_TEXT_LEN];
} event_t;

typedef enum { NET_STA = 0, NET_AP, NET_APSTA } net_mode_t;

typedef struct {
    /* Mediciones */
    reading_t      r[VAR_COUNT];
    int            soil_mv;
    bool           soil_saturated;   /* tensión cerca del tope del ADC */
    bool           sensor_present[VAR_COUNT];

    /* Alertas */
    alert_engine_t eng;

    /* Calibración: lecturas SIN corregir, promedio de las últimas 6 (30 s) */
    float          raw_avg[3];          /* 0 = T (°C), 1 = HR (%), 2 = lux       */
    bool           raw_valid[3];
    float          boot_t;              /* primera temperatura tras encender     */
    bool           boot_t_valid;

    /* PIR */
    bool           pir_now;             /* nivel actual de la salida del PIR     */
    bool           pir_warming;
    uint32_t       motion_count;
    uint32_t       last_motion_uptime;  /* 0 = nunca */
    time_t         last_motion_epoch;

    /* Red */
    net_mode_t     net_mode;
    bool           sta_connected;
    bool           has_credentials;
    char           sta_ssid[33];
    char           sta_ip[16];
    int8_t         rssi;
    char           ap_ssid[33];

    /* Otros */
    bool           oled_present;
    uint32_t       cycles;

    /* Registro de eventos (F-22) */
    event_t        events[EVENT_LOG_LEN];
    uint8_t        ev_head;      /* posición del próximo evento */
    uint8_t        ev_count;
} app_status_t;

void          app_state_init(void);
void          app_lock(void);
void          app_unlock(void);
app_status_t *app_status(void);      /* usar SOLO con el candado tomado */
void          app_snapshot(app_status_t *out);

/* Añade un evento (toma el candado internamente: NO llamar con el candado tomado) */
void          app_add_event(event_kind_t kind, const char *fmt, ...);

/* Reconocer alerta (pulsador o web). Devuelve true si había alerta sin reconocer. */
bool          app_request_ack(const char *origin);

uint32_t      app_uptime_s(void);
time_t        app_epoch_now(void);   /* 0 si la hora aún no está sincronizada */

/*
 * mqtt_link.h — Conexión MQTT con la página remota (tablero).
 *
 * Tópicos (base = plantas/<id>):
 *   base/online        "online" / "offline" (retenido; "offline" es el último deseo, LWT)
 *   base/telemetria    lecturas cada mqtt_period_s (retenido)              JSON
 *   base/estado        estado del sistema cuando cambia (retenido)          JSON
 *   base/config        rangos, nombre y calibración cuando cambian (ret.)   JSON
 *   base/alerta        cada vez que una variable entra en alarma            JSON
 *   base/evento        registro de eventos (alertas, Wi-Fi, calibración…)   JSON
 *   base/cmd/ack       reconocer alerta              {"clave":"..."}
 *   base/cmd/rangos    cambiar rangos                {"clave":"...","ranges":{...},"name":...}
 *   base/cmd/calibrar  cambiar calibración           {"clave":"...","t_offset":-0.6,...}
 *   base/cmd/actualizar  pide volver a publicar todo (sin clave)
 *   base/cmd/resp      respuesta a cada comando      {"cmd":"rangos","ok":true,"msg":"...","id":...}
 */
#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "app_state.h"
#include "settings.h"

typedef struct {
    bool enabled;
    bool connected;
    char id[SETTINGS_ID_LEN];
    char base[40];
    char uri[SETTINGS_URI_LEN];
    char user[SETTINGS_NAME_LEN];
    char last_error[64];
    uint32_t published;
} mqtt_info_t;

esp_err_t mqtt_link_start(void);
void      mqtt_link_reconfigure(void);        /* llamar tras cambiar los datos del broker */
void      mqtt_link_get_info(mqtt_info_t *out);
/* La llama app_add_event(): encola el evento para publicarlo (no bloquea). */
void      mqtt_link_on_event(event_kind_t kind, const char *text);

/*
 * web_server.h — Servidor HTTP con la página web y la API REST (F-15, F-16, F-11).
 *
 *  GET  /                 página web (main/web/index.html, embebida en el firmware)
 *  GET  /api/status       mediciones, estado, alertas, PIR, Wi-Fi y eventos
 *  GET  /api/config       rangos y parámetros actuales
 *  POST /api/config       guardar rangos           (requiere contraseña)
 *  POST /api/ack          reconocer alerta          (requiere contraseña)
 *  POST /api/calibrate    {"point":"dry"|"wet"}     (requiere contraseña)
 *  POST /api/reset_ranges rangos por defecto        (requiere contraseña)
 *  POST /api/password     {"new":"..."}             (requiere contraseña)
 *  GET  /api/scan         redes Wi-Fi cercanas
 *  POST /api/wifi         {"ssid":"..","pass":".."} (requiere contraseña)
 *
 *  La contraseña se envía en la cabecera HTTP "X-Auth".
 */
#pragma once
#include "esp_err.h"

esp_err_t web_server_start(void);

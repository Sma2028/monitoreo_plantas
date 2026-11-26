/*
 * wifi_mgr.h — Conexión a la red del usuario + portal de configuración (F-13, F-14).
 */
#pragma once
#include <stdint.h>
#include "esp_err.h"

#define WIFI_AP_PASSWORD   "planta1234"     /* contraseña del punto de acceso de configuración */
#define WIFI_AP_IP         "192.168.4.1"
#define MDNS_HOSTNAME      "planta"         /* http://planta.local */

typedef struct {
    char    ssid[33];
    int8_t  rssi;
    uint8_t secure;      /* 1 si la red pide contraseña */
} wifi_scan_item_t;

esp_err_t wifi_mgr_start(void);
esp_err_t wifi_mgr_set_credentials(const char *ssid, const char *pass);
void      wifi_mgr_forget(void);
int       wifi_mgr_scan(wifi_scan_item_t *out, int max);

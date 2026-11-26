/*
 * local_ui.h — Interfaz local: LED RGB, zumbador, pulsadores y PIR.
 * Corre en su propia tarea cada 20 ms, independiente del Wi-Fi.
 */
#pragma once
#include "esp_err.h"

esp_err_t local_ui_start(void);

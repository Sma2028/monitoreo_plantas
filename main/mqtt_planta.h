/*
 * mqtt_planta.h — Envía las mediciones a broker.hivemq.com y recibe comandos de la página.
 * Los tópicos están en config_red.h.
 */
#pragma once
#include <stdbool.h>

void mqtt_planta_start(void);          /* se llama una vez en app_main */
void mqtt_planta_publicar(void);       /* se llama al final de cada medición */
void mqtt_planta_evento(const char *texto);   /* publica un evento (alertas, avisos) */
bool mqtt_planta_conectado(void);

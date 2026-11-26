/*
 * config_red.h — TODO lo que tiene que ver con la red está aquí.
 * Si cambias de red Wi-Fi o de broker, solo cambias este archivo y vuelves a grabar.
 */
#pragma once

/* --- CREDENCIALES DE RED WIFI ---
 * En el iPhone: Ajustes > Compartir Internet > activa "Maximizar compatibilidad"
 * (el ESP32 solo se conecta a redes de 2,4 GHz). */
#define WIFI_SSID        "iPhone sofia"
#define WIFI_PASSWORD    "sofii1234"

/* --- CONFIGURACIÓN MQTT --- */
#define MQTT_SERVER      "broker.hivemq.com"
#define MQTT_PORT        1883

/* --- TÓPICOS MQTT (deben ser IDÉNTICOS en el HTML) --- */
#define TOPIC_BASE         "instrumentacion/sofia/planta"

/* El ESP32 PUBLICA (la página los lee) */
#define TOPIC_TEMPERATURA  TOPIC_BASE "/temperatura"   /* "22.5"  °C   (o "falla")      */
#define TOPIC_HUMEDAD      TOPIC_BASE "/humedad"       /* "49.1"  %HR                    */
#define TOPIC_LUZ          TOPIC_BASE "/luz"           /* "850"   lx                     */
#define TOPIC_SUELO        TOPIC_BASE "/suelo"         /* "42"    %                      */
#define TOPIC_ESTADO       TOPIC_BASE "/estado"        /* "normal" | "alerta" | "reconocida" | "inicio" */
#define TOPIC_EVENTO       TOPIC_BASE "/evento"        /* texto: "Temperatura fuera de rango (alto): 31.2" */
#define TOPIC_RANGOS       TOPIC_BASE "/rangos"        /* JSON con los rangos que está usando la planta */
#define TOPIC_CONEXION     TOPIC_BASE "/conexion"      /* "online" | "offline"           */

/* El ESP32 ESCUCHA (la página los envía) */
#define TOPIC_CONTROL      TOPIC_BASE "/control"       /* "ack" = reconocer alerta, "actualizar" = reenviar todo */
#define TOPIC_CAMBIAR      TOPIC_BASE "/rangos/cambiar" /* JSON con los rangos nuevos   */

/*
 * board.h — Asignación de pines. Cambia las conexiones SOLO aquí.
 *
 * El mismo firmware compila para dos placas:
 *   - idf.py set-target esp32c3 -> PCB propia (ESP32-C3-WROOM-02, esquemático Rev. A)
 *   - idf.py set-target esp32   -> Prueba de concepto en protoboard con ESP32 DevKit
 */
#pragma once
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "hal/adc_types.h"

#if CONFIG_IDF_TARGET_ESP32
/* =====================================================================
 *  PRUEBA DE CONCEPTO — ESP32 DevKit (ESP32-WROOM-32) en protoboard
 * ===================================================================== */
#define PIN_I2C_SDA         GPIO_NUM_32
#define PIN_I2C_SCL         GPIO_NUM_33
#define I2C_INTERNAL_PULLUP true        /* los módulos traen sus pull-ups; esto solo ayuda */

#define SOIL_ADC_UNIT       ADC_UNIT_1
#define SOIL_ADC_CHANNEL    ADC_CHANNEL_6   /* GPIO34 = ADC1 canal 6 (solo entrada) */
#define SOIL_ADC_MAX_MV     2450            /* tope útil del ESP32 con 12 dB         */

#define PIN_PIR             GPIO_NUM_35     /* solo entrada, sin pull interna        */
#define PIR_INTERNAL_PULLDOWN 0             /* GPIO34-39 no tienen pull-up/down      */
#define PIN_BTN_ACK         GPIO_NUM_13     /* pulsador a GND, pull-up interna       */
#define PIN_BTN_BOOT        GPIO_NUM_0      /* botón BOOT de la DevKit               */

#define PIN_LED_R           GPIO_NUM_25
#define PIN_LED_G           GPIO_NUM_26
#define PIN_LED_B           GPIO_NUM_27
/* GPIO12 es pin de arranque y debe estar en BAJO al encender: la resistencia de
 * 10 kΩ de la base a GND del transistor lo garantiza. */
#define PIN_BUZZER          GPIO_NUM_12     /* base del transistor NPN vía 1 kΩ      */
/* Libre: GPIO14 */

/* LED RGB conectado directo a los GPIO (sin NFET):
 *   cátodo común -> 0  (GPIO en ALTO enciende)
 *   ánodo común  -> 1  (GPIO en BAJO enciende; el ánodo va a 3V3)               */
#ifndef LED_ACTIVE_LOW
#define LED_ACTIVE_LOW      0
#endif

#else
/* =====================================================================
 *  PCB FINAL — ESP32-C3-WROOM-02 (esquemático Revisión A)
 * ===================================================================== */
#define PIN_I2C_SDA         GPIO_NUM_6
#define PIN_I2C_SCL         GPIO_NUM_7
#define I2C_INTERNAL_PULLUP false       /* la placa tiene R10/R11 de 4,7 kΩ */

#define SOIL_ADC_UNIT       ADC_UNIT_1
#define SOIL_ADC_CHANNEL    ADC_CHANNEL_0   /* IO0 */
#define SOIL_ADC_MAX_MV     2500

#define PIN_PIR             GPIO_NUM_3
#define PIN_BTN_ACK         GPIO_NUM_1
#define PIN_BTN_BOOT        GPIO_NUM_9

#define PIN_LED_R           GPIO_NUM_5      /* NFET: activo en ALTO */
#define PIN_LED_G           GPIO_NUM_10
#define PIN_LED_B           GPIO_NUM_8
#define PIN_BUZZER          GPIO_NUM_4
#define PIR_INTERNAL_PULLDOWN 1

#define LED_ACTIVE_LOW      0
#endif

/* ---- Común a ambas placas ---- */
#define I2C_FREQ_HZ         100000
#define AHT10_ADDR          0x38
#define BH1750_ADDR         0x23
#define BH1750_ADDR_ALT     0x5C
#define OLED_ADDR           0x3C

#define PIR_WARMUP_S        60
#define BTN_DEBOUNCE_MS     50
#define BTN_LONG_MS         3000
#define BTN_VERY_LONG_MS    10000

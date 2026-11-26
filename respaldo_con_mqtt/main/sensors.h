/*
 * sensors.h — Capa de abstracción de sensores (NF-08: un módulo por sensor).
 */
#pragma once
#include <stdbool.h>
#include "esp_err.h"

/* AHT10 (temperatura y humedad relativa) — aht10.c */
esp_err_t aht10_init(void);
esp_err_t aht10_read(float *temp_c, float *rh_pct);

/* BH1750 (iluminancia) — bh1750.c */
esp_err_t bh1750_init(void);
esp_err_t bh1750_read(float *lux);

/* Sensor capacitivo de humedad de suelo (ADC1) — soil.c */
esp_err_t soil_init(void);
esp_err_t soil_read_mv(int *mv);          /* promedio filtrado en milivoltios   */
float     soil_percent(int mv, int dry_mv, int wet_mv);  /* 0–100 % relativo */

/*
 * oled.h — Pantalla OLED I2C 128x64 (SSD1306, o SH1106 de 1,3") en 0x3C.
 * Texto de 21 columnas x 8 filas con fuente 5x7.
 */
#pragma once
#include <stdbool.h>
#include "esp_err.h"

esp_err_t oled_init(void);           /* ESP_ERR_NOT_FOUND si no hay pantalla */
bool      oled_present(void);
void      oled_clear(void);
void      oled_text(int col, int row, const char *s);   /* '~' se dibuja como '°' */
void      oled_invert_row(int row);
esp_err_t oled_flush(void);

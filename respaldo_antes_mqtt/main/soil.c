/*
 * soil.c — Sensor capacitivo de humedad de suelo en IO0 (ADC1, canal 0).
 *
 * - Se usa ADC1: el ADC2 del C3 no es fiable (y choca con el Wi-Fi).
 * - Atenuación de 12 dB: rango útil ≈ 0–2500 mV en el ESP32-C3.
 * - Se toman 16 muestras, se descartan las 4 más bajas y las 4 más altas
 *   y se promedian las 8 centrales (filtro robusto contra picos del Wi-Fi).
 * - El porcentaje es RELATIVO a la calibración de dos puntos (seco / húmedo).
 *   En un sensor capacitivo, más agua = MENOS tensión.
 */
#include "sensors.h"
#include "board.h"
#include <stdlib.h>
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "suelo";
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static bool s_cali_ok;

esp_err_t soil_init(void)
{
    adc_oneshot_unit_init_cfg_t unit = { .unit_id = SOIL_ADC_UNIT };
    esp_err_t e = adc_oneshot_new_unit(&unit, &s_adc);
    if (e != ESP_OK) return e;

    adc_oneshot_chan_cfg_t ch = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    e = adc_oneshot_config_channel(s_adc, SOIL_ADC_CHANNEL, &ch);
    if (e != ESP_OK) return e;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cc = {
        .unit_id = SOIL_ADC_UNIT, .chan = SOIL_ADC_CHANNEL,
        .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    s_cali_ok = (adc_cali_create_scheme_curve_fitting(&cc, &s_cali) == ESP_OK);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t lc = {
        .unit_id = SOIL_ADC_UNIT, .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    s_cali_ok = (adc_cali_create_scheme_line_fitting(&lc, &s_cali) == ESP_OK);
#endif
    ESP_LOGI(TAG, "ADC listo (calibración de fábrica: %s)", s_cali_ok ? "sí" : "no");
    return ESP_OK;
}

static int cmp_int(const void *a, const void *b)
{
    return (*(const int *)a) - (*(const int *)b);
}

esp_err_t soil_read_mv(int *mv)
{
    enum { N = 16, DROP = 4 };
    int raw[N];
    for (int i = 0; i < N; i++) {
        esp_err_t e = adc_oneshot_read(s_adc, SOIL_ADC_CHANNEL, &raw[i]);
        if (e != ESP_OK) return e;
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    qsort(raw, N, sizeof(int), cmp_int);
    int sum = 0;
    for (int i = DROP; i < N - DROP; i++) sum += raw[i];
    int avg = sum / (N - 2 * DROP);

    if (s_cali_ok) {
        int v = 0;
        if (adc_cali_raw_to_voltage(s_cali, avg, &v) == ESP_OK) { *mv = v; return ESP_OK; }
    }
    *mv = avg * SOIL_ADC_MAX_MV / 4095;       /* aproximación sin calibración */
    return ESP_OK;
}

float soil_percent(int mv, int dry_mv, int wet_mv)
{
    if (dry_mv <= wet_mv) return 0;
    float p = (float)(dry_mv - mv) * 100.0f / (float)(dry_mv - wet_mv);
    if (p < 0) p = 0;
    if (p > 100) p = 100;
    return p;
}

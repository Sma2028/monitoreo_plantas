/*
 * aht10.c — Sensor de temperatura y humedad AHT10 (I2C 0x38).
 * También funciona con módulos que en realidad traen AHT20 (comando 0xBE).
 */
#include "sensors.h"
#include "i2c_bus.h"
#include "board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "aht10";
static i2c_master_dev_handle_t s_dev;

esp_err_t aht10_init(void)
{
    if (!s_dev) {
        esp_err_t e = i2c_bus_add_device(AHT10_ADDR, &s_dev);
        if (e != ESP_OK) return e;
    }
    const uint8_t soft_reset = 0xBA;
    i2c_master_transmit(s_dev, &soft_reset, 1, 100);
    vTaskDelay(pdMS_TO_TICKS(25));

    const uint8_t init_aht10[3] = { 0xE1, 0x08, 0x00 };
    esp_err_t e = i2c_master_transmit(s_dev, init_aht10, 3, 100);
    if (e != ESP_OK) {
        const uint8_t init_aht20[3] = { 0xBE, 0x08, 0x00 };
        e = i2c_master_transmit(s_dev, init_aht20, 3, 100);
    }
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_LOGI(TAG, "Inicialización: %s", esp_err_to_name(e));
    return e;
}

esp_err_t aht10_read(float *temp_c, float *rh_pct)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    const uint8_t trigger[3] = { 0xAC, 0x33, 0x00 };
    esp_err_t e = i2c_master_transmit(s_dev, trigger, 3, 100);
    if (e != ESP_OK) return e;
    vTaskDelay(pdMS_TO_TICKS(80));              /* tiempo de conversión ≈ 75 ms */

    uint8_t d[6] = { 0 };
    for (int i = 0; i < 5; i++) {
        e = i2c_master_receive(s_dev, d, sizeof(d), 100);
        if (e != ESP_OK) return e;
        if (!(d[0] & 0x80)) break;              /* bit 7 = ocupado */
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (d[0] & 0x80) return ESP_ERR_TIMEOUT;

    uint32_t raw_h = ((uint32_t)d[1] << 12) | ((uint32_t)d[2] << 4) | (d[3] >> 4);
    uint32_t raw_t = (((uint32_t)d[3] & 0x0F) << 16) | ((uint32_t)d[4] << 8) | d[5];
    if (raw_h == 0 && raw_t == 0) return ESP_ERR_INVALID_RESPONSE;

    *rh_pct = (float)raw_h * 100.0f / 1048576.0f;
    *temp_c = (float)raw_t * 200.0f / 1048576.0f - 50.0f;
    return ESP_OK;
}

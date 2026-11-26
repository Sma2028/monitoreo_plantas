/*
 * bh1750.c — Sensor de luz BH1750 / GY-302 (I2C 0x23, o 0x5C con ADDR alto).
 * Modo continuo de alta resolución (1 lx, ~120 ms por medida).
 */
#include "sensors.h"
#include "i2c_bus.h"
#include "board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "bh1750";
static i2c_master_dev_handle_t s_dev;

esp_err_t bh1750_init(void)
{
    if (!s_dev) {
        uint8_t addr = BH1750_ADDR;
        if (!i2c_bus_probe(addr) && i2c_bus_probe(BH1750_ADDR_ALT)) addr = BH1750_ADDR_ALT;
        esp_err_t e = i2c_bus_add_device(addr, &s_dev);
        if (e != ESP_OK) return e;
        ESP_LOGI(TAG, "Usando dirección 0x%02X", addr);
    }
    const uint8_t power_on = 0x01, reset = 0x07, cont_hres = 0x10;
    esp_err_t e = i2c_master_transmit(s_dev, &power_on, 1, 100);
    if (e == ESP_OK) e = i2c_master_transmit(s_dev, &reset, 1, 100);
    if (e == ESP_OK) e = i2c_master_transmit(s_dev, &cont_hres, 1, 100);
    vTaskDelay(pdMS_TO_TICKS(180));            /* primera conversión */
    ESP_LOGI(TAG, "Inicialización: %s", esp_err_to_name(e));
    return e;
}

esp_err_t bh1750_read(float *lux)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    uint8_t d[2];
    esp_err_t e = i2c_master_receive(s_dev, d, 2, 100);
    if (e != ESP_OK) return e;
    *lux = (float)(((uint16_t)d[0] << 8) | d[1]) / 1.2f;   /* factor de la hoja de datos */
    return ESP_OK;
}

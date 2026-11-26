/*
 * i2c_bus.c — Bus I2C compartido (driver nuevo i2c_master de ESP-IDF 5.x).
 *
 * Las pull-ups internas del ESP32-C3 (~45 kΩ) se DESACTIVAN: el bus usa las
 * resistencias externas R7/R8 de 4,7 kΩ del esquemático, que son las que
 * garantizan flancos de subida rápidos a 100 kHz.
 */
#include "i2c_bus.h"
#include "board.h"
#include "esp_log.h"

static const char *TAG = "i2c";
static i2c_master_bus_handle_t s_bus;

esp_err_t i2c_bus_init(void)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = I2C_INTERNAL_PULLUP,
    };
    esp_err_t e = i2c_new_master_bus(&cfg, &s_bus);
    ESP_LOGI(TAG, "Bus I2C SDA=IO%d SCL=IO%d: %s", PIN_I2C_SDA, PIN_I2C_SCL, esp_err_to_name(e));
    return e;
}

esp_err_t i2c_bus_add_device(uint8_t addr, i2c_master_dev_handle_t *out)
{
    i2c_device_config_t d = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    return i2c_master_bus_add_device(s_bus, &d, out);
}

bool i2c_bus_probe(uint8_t addr)
{
    return i2c_master_probe(s_bus, addr, 50) == ESP_OK;
}

void i2c_bus_scan_log(void)
{
    int found = 0;
    for (uint8_t a = 0x08; a < 0x78; a++) {
        if (i2c_bus_probe(a)) {
            ESP_LOGI(TAG, "  Dispositivo en 0x%02X", a);
            found++;
        }
    }
    if (!found) ESP_LOGW(TAG, "  Ningún dispositivo I2C respondió: revisa VCC, GND, SDA/SCL y pull-ups");
}

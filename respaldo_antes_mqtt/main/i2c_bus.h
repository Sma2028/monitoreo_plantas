#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

esp_err_t i2c_bus_init(void);
esp_err_t i2c_bus_add_device(uint8_t addr, i2c_master_dev_handle_t *out);
bool      i2c_bus_probe(uint8_t addr);
void      i2c_bus_scan_log(void);     /* imprime en consola qué direcciones responden */

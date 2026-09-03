#pragma once
// Driver mínimo del RTC PCF85063 (I2C 0x51) del reloj: leer y fijar la hora.
#include <time.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

// Registra el chip en el bus (el bus lo crea el BSP: bsp_i2c_get_handle()).
esp_err_t rtc_pcf85063_init(i2c_master_bus_handle_t bus);

// Lee la hora. Devuelve ESP_ERR_INVALID_STATE si el oscilador se detuvo (hora no fiable,
// p. ej. tras quedarse sin batería); en ese caso *t queda igual leído pero no hay que confiar en él.
esp_err_t rtc_pcf85063_get(struct tm *t);

// Fija la hora (hora local, 24 h) y limpia la bandera de oscilador detenido.
esp_err_t rtc_pcf85063_set(const struct tm *t);

#ifdef __cplusplus
}
#endif

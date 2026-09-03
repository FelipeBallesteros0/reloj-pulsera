#pragma once
// Botón lateral BOOT (GPIO9). El otro botón, PWR, no es un GPIO: lo entrega el PMIC (ver pmic.h).
#include <stdbool.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t boton_boot_init(void);
bool boton_boot_presionado(void);
#ifdef __cplusplus
}
#endif

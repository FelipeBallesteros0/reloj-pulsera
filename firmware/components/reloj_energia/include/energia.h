#pragma once
// Gestor de energía: aplica la regla de energia_politica (WiFi por demanda, candados de PM en la etapa 2) y lleva
// el modo actual para el log y "Acerca de".
#include <stdint.h>
#include "esp_err.h"
#include "energia_politica.h"
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t energia_init(void);                              // con CONFIG_PM_ENABLE: esp_pm_configure + candados
void energia_actualizar(const energia_entradas_t *e);      // aplica solo lo que cambió; registra los cambios de modo
energia_modo_t energia_modo(void);
uint32_t energia_segundos_en_modo(void);
#ifdef __cplusplus
}
#endif

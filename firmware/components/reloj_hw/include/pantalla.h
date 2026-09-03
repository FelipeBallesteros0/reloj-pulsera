#pragma once
// Pantalla AMOLED (CO5300 por QSPI) + táctil FT3168 + port de LVGL. Es nuestra copia del arranque del BSP,
// para tener a mano los handles del panel (encender/apagar, brillo) que el BSP no expone.
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t pantalla_init(void);          // SPI + panel + táctil + tarea de LVGL; deja la pantalla encendida
lv_display_t *pantalla_disp(void);
bool pantalla_lock(uint32_t ms);        // mutex de LVGL (0 = esperar lo que haga falta)
void pantalla_unlock(void);
void pantalla_brillo(int pct);          // 0-100; se recuerda para el próximo encendido
int  pantalla_brillo_actual(void);
void pantalla_encender(void);
void pantalla_apagar(void);             // brillo 0 + comando "display off"; LVGL sigue vivo para detectar el toque
bool pantalla_encendida(void);
uint32_t pantalla_ms_sin_tocar(void);   // milisegundos desde el último toque

#ifdef __cplusplus
}
#endif

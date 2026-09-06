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
void pantalla_encender(void);           // reanuda LVGL, saca al panel del sleep, espera el primer cuadro dibujado y muestra; solo desde la tarea principal
void pantalla_apagar(void);             // brillo 0, DISPOFF, SLPIN y LVGL parado; el toque sigue detectándose por la INT
bool pantalla_encendida(void);
uint32_t pantalla_ms_sin_tocar(void);   // milisegundos desde el último toque
bool pantalla_punto_tocado(int *x, int *y);   // true (y coordenadas) si hay un dedo apoyado ahora
bool pantalla_toque_pendiente(void);    // true una vez por cada toque (también con la pantalla apagada)

#ifdef __cplusplus
}
#endif

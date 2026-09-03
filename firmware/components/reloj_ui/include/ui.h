#pragma once
// Interfaz LVGL del reloj: carátula y ajustes. Todas las funciones se llaman con el mutex de LVGL tomado
// (pantalla_lock), desde la tarea de la app. Los callbacks corren en la tarea de LVGL.
#include <stdbool.h>
#include <time.h>
#include "ble.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ui_cb_t)(void);
typedef void (*ui_cb_int_t)(int);

void ui_init(const char *nombre_ble, int brillo_pct);
void ui_hora(const struct tm *t);
void ui_bateria(int pct, bool cargando, bool usb);
void ui_ble(ble_estado_t estado, bool tiene_bond, const char *direccion);
void ui_ram(unsigned kb);
void ui_notif_pendientes(int n);
void ui_al_olvidar(ui_cb_t cb);          // botón "Olvidar iPhone"
void ui_al_cambiar_brillo(ui_cb_int_t cb);
void ui_ir_reloj(void);
void ui_ir_ajustes(void);
bool ui_en_reloj(void);

#ifdef __cplusplus
}
#endif

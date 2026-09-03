#pragma once
// Interfaz LVGL del reloj: carátula, notificaciones (lista, detalle, aviso emergente, llamada) y ajustes.
// Las funciones ui_* se llaman con el mutex de LVGL tomado (pantalla_lock) desde la tarea de la app; leen el
// almacén de notificaciones directamente. Los callbacks corren en la tarea de LVGL.
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "ble.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ui_cb_t)(void);
typedef void (*ui_cb_int_t)(int);
typedef void (*ui_cb_uid_t)(uint32_t uid);
typedef void (*ui_cb_llamada_t)(uint32_t uid, bool contestar);

void ui_init(const char *nombre_ble, int brillo_pct);
void ui_hora(const struct tm *t);
void ui_bateria(int pct, bool cargando, bool usb);
void ui_ble(ble_estado_t estado, bool tiene_bond, const char *direccion);
void ui_ram(unsigned kb);

// Notificaciones
void ui_notifs_refrescar(void);            // relee el almacén: contador, lista y detalle abierto
void ui_aviso(uint32_t uid);               // tarjeta emergente 6 s (toca para abrir)
void ui_llamada(uint32_t uid, bool mostrar);
bool ui_en_llamada(uint32_t *uid);

// Navegación
void ui_ir_reloj(void);
void ui_ir_ajustes(void);
void ui_ir_lista(void);
void ui_ir_detalle(uint32_t uid);
bool ui_en_reloj(void);

// Callbacks hacia la app
void ui_al_olvidar(ui_cb_t cb);              // botón "Olvidar iPhone"
void ui_al_cambiar_brillo(ui_cb_int_t cb);
void ui_al_descartar(ui_cb_uid_t cb);        // botón "Descartar" de una notificación
void ui_al_llamada(ui_cb_llamada_t cb);      // contestar / rechazar

#ifdef __cplusplus
}
#endif

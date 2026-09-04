#pragma once
// Interfaz LVGL del reloj: carátula, notificaciones (lista, detalle, aviso emergente, llamada) y ajustes.
// Las funciones ui_* se llaman con el mutex de LVGL tomado (pantalla_lock) desde la tarea de la app; leen el
// almacén de notificaciones directamente. Los callbacks corren en la tarea de LVGL.
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "ble.h"
#include "andia.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ui_cb_t)(void);
typedef void (*ui_cb_int_t)(int);
typedef void (*ui_cb_bool_t)(bool);
typedef void (*ui_cb_uid_t)(uint32_t uid);
typedef void (*ui_cb_llamada_t)(uint32_t uid, bool contestar);

void ui_init(const char *nombre_ble, int brillo_pct);
// Valores iniciales de los ajustes (se muestran al abrir cada sección).
void ui_ajustes(int volumen_pct, int tiempo_pantalla_s, int sensibilidad_pct);
void ui_hora(const struct tm *t);
void ui_bateria(int pct, bool cargando, bool usb);
void ui_ble(ble_estado_t estado, bool tiene_bond, const char *direccion);
void ui_ram(unsigned kb);
void ui_red(bool conectada, const char *ip);
void ui_wifi(const char *ssid, const char *ip, bool en_casa, bool andia_conectada, int rssi);   // sección WiFi

// Notificaciones
void ui_notifs_refrescar(void);            // relee el almacén: contador, lista y detalle abierto
void ui_aviso(uint32_t uid);               // tarjeta emergente 6 s (toca para abrir)
void ui_llamada(uint32_t uid, bool mostrar);
bool ui_en_llamada(uint32_t *uid);

// ANDIA
void ui_andia_estado(andia_estado_t e);
void ui_andia_pregunta(const char *t);
void ui_andia_respuesta(const char *t);
void ui_andia_error(const char *t);
void ui_ir_andia(void);
void ui_al_microfono(ui_cb_t cb);          // botón de micrófono (carátula y pantalla ANDIA)
void ui_al_cancelar_andia(ui_cb_t cb);
void ui_al_vigilia(ui_cb_bool_t cb);        // interruptor "Oye Andia siempre" en Ajustes
void ui_vigilia(bool activa);               // refleja el ajuste (interruptor y pie de la carátula)
bool ui_en_andia(void);

// Sensores (app): la app lee el IMU mientras esta pantalla está abierta
bool ui_en_sensores(void);
void ui_sensores(float ax, float ay, float az, float gx, float gy, float gz);
// Navegación
void ui_ir_reloj(void);
void ui_ir_menu(void);
void ui_ir_ajustes(void);
void ui_ir_lista(void);
void ui_ir_detalle(uint32_t uid);
bool ui_en_reloj(void);

// Callbacks hacia la app
void ui_al_olvidar(ui_cb_t cb);              // botón "Olvidar iPhone"
void ui_al_cambiar_brillo(ui_cb_int_t cb);
void ui_al_descartar(ui_cb_uid_t cb);        // botón "Descartar" de una notificación
void ui_al_llamada(ui_cb_llamada_t cb);      // contestar / rechazar
void ui_al_cambiar_volumen(ui_cb_int_t cb);          // 0-100
void ui_al_probar_sonido(ui_cb_t cb);                // botón "Probar" en Sonido
void ui_al_cambiar_tiempo_pantalla(ui_cb_int_t cb);  // segundos
void ui_al_cambiar_sensibilidad(ui_cb_int_t cb);     // 0-100 (ganancia del micrófono)
void ui_al_apagar(ui_cb_t cb);                       // "Apagar el reloj" confirmado

#ifdef __cplusplus
}
#endif

#pragma once
// Cliente ANCS: descubre el servicio en el iPhone, se suscribe y pide los atributos de cada notificación.
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
void ancs_iniciar(uint16_t conn_handle);          // llamar con el enlace ya cifrado (tras CTS)
void ancs_desconectado(void);                     // limpia el estado al perder la conexión
bool ancs_listo(void);                            // suscrito a Notification Source y Data Source
// Se llama desde el evento NOTIFY_RX de GAP; devuelve true si el handle era de ANCS.
bool ancs_notificacion_rx(uint16_t attr_handle, const uint8_t *datos, uint16_t largo);
// Acciones sobre una notificación (Descartar = negativa, Contestar = positiva). false si no hay conexión ANCS.
bool ancs_accion(uint32_t uid, bool positiva);
#ifdef __cplusplus
}
#endif

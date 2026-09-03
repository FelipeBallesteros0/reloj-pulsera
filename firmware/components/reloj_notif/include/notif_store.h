#pragma once
// Almacén en RAM de las notificaciones del iPhone (anillo: la más nueva primero). Lo escribe la tarea BLE y lo lee
// la tarea de la app; en el firmware va protegido con un mutex, en los tests de host no hace falta.
#include <stdbool.h>
#include <stdint.h>
#include "ancs_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NOTIF_MAX 24

typedef struct {
    uint32_t uid;
    uint8_t categoria;
    uint8_t flags;
    bool leida;
    bool con_detalle;          // ya llegaron los atributos
    uint32_t recibida_ms;      // reloj monótono del sistema al recibirla
    char app_id[ANCS_LARGO_APP_ID];
    char titulo[ANCS_LARGO_TITULO + 1];
    char subtitulo[ANCS_LARGO_SUBTITULO + 1];
    char mensaje[ANCS_LARGO_MENSAJE + 1];
    char fecha[ANCS_LARGO_FECHA];
} notif_t;

void notif_store_init(void);
void notif_store_vaciar(void);
// Inserta (o reubica al frente si ya existía) una notificación con esos datos básicos. Si el anillo está lleno,
// descarta la más antigua. Cualquier uid es válido (iOS usa 0 para la primera de cada conexión).
bool notif_store_agregar(uint32_t uid, uint8_t categoria, uint8_t flags, uint32_t ahora_ms);
// Completa los atributos de una notificación existente. false si no está.
bool notif_store_poner_detalle(uint32_t uid, const ancs_attrs_t *a);
bool notif_store_quitar(uint32_t uid);
bool notif_store_marcar_leida(uint32_t uid);
void notif_store_marcar_todas_leidas(void);
int  notif_store_cantidad(void);
int  notif_store_no_leidas(void);
// Copia la notificación i (0 = más reciente) en *out. false si i está fuera de rango.
bool notif_store_obtener(int i, notif_t *out);
bool notif_store_buscar(uint32_t uid, notif_t *out);

// Ganchos de exclusión mutua (el firmware los fija con un mutex de FreeRTOS; en host quedan en NULL).
void notif_store_set_lock(void (*lock)(void), void (*unlock)(void));

#ifdef __cplusplus
}
#endif

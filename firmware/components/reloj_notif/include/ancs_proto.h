#pragma once
// Protocolo ANCS (Apple Notification Center Service) en C puro, sin dependencias de NimBLE: testeable en host.
// Referencia: ANCS Specification (Apple). Todo little-endian.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// EventID / EventFlags / CategoryID / CommandID / AttributeID / ActionID según la especificación.
enum { ANCS_EV_AGREGADA = 0, ANCS_EV_MODIFICADA = 1, ANCS_EV_QUITADA = 2 };
enum { ANCS_FLAG_SILENCIOSA = 1 << 0, ANCS_FLAG_IMPORTANTE = 1 << 1, ANCS_FLAG_PREEXISTENTE = 1 << 2,
       ANCS_FLAG_ACCION_POSITIVA = 1 << 3, ANCS_FLAG_ACCION_NEGATIVA = 1 << 4 };
enum { ANCS_CAT_OTRA = 0, ANCS_CAT_LLAMADA = 1, ANCS_CAT_LLAMADA_PERDIDA = 2, ANCS_CAT_BUZON = 3, ANCS_CAT_SOCIAL = 4,
       ANCS_CAT_AGENDA = 5, ANCS_CAT_CORREO = 6, ANCS_CAT_NOTICIAS = 7, ANCS_CAT_SALUD = 8, ANCS_CAT_FINANZAS = 9,
       ANCS_CAT_UBICACION = 10, ANCS_CAT_ENTRETENIMIENTO = 11 };
enum { ANCS_CMD_GET_NOTIF_ATTRS = 0, ANCS_CMD_GET_APP_ATTRS = 1, ANCS_CMD_ACCION = 2 };
enum { ANCS_ATTR_APP_ID = 0, ANCS_ATTR_TITULO = 1, ANCS_ATTR_SUBTITULO = 2, ANCS_ATTR_MENSAJE = 3,
       ANCS_ATTR_TAMANO_MENSAJE = 4, ANCS_ATTR_FECHA = 5, ANCS_ATTR_ACCION_POSITIVA = 6, ANCS_ATTR_ACCION_NEGATIVA = 7 };
enum { ANCS_ACCION_POSITIVA = 0, ANCS_ACCION_NEGATIVA = 1 };

// Tamaños de los campos que pedimos (los mismos que van en el comando GetNotificationAttributes).
#define ANCS_LARGO_TITULO    64
#define ANCS_LARGO_SUBTITULO 48
#define ANCS_LARGO_MENSAJE   192
#define ANCS_LARGO_APP_ID    48
#define ANCS_LARGO_FECHA     16     // yyyyMMdd'T'HHmmSS = 15 + NUL
#define ANCS_LARGO_NOMBRE_APP 32

// Notification Source (8 bytes).
typedef struct {
    uint8_t evento;      // ANCS_EV_*
    uint8_t flags;       // ANCS_FLAG_*
    uint8_t categoria;   // ANCS_CAT_*
    uint8_t cantidad;    // notificaciones activas en la categoría
    uint32_t uid;
} ancs_ns_t;
bool ancs_parse_ns(const uint8_t *d, size_t n, ancs_ns_t *out);

// Atributos de una notificación (respuesta a GetNotificationAttributes), ya como cadenas C.
typedef struct {
    uint32_t uid;
    char app_id[ANCS_LARGO_APP_ID];
    char titulo[ANCS_LARGO_TITULO + 1];
    char subtitulo[ANCS_LARGO_SUBTITULO + 1];
    char mensaje[ANCS_LARGO_MENSAJE + 1];
    char fecha[ANCS_LARGO_FECHA];
} ancs_attrs_t;

// Acumulador de fragmentos del Data Source: las respuestas llegan partidas en notificaciones GATT de hasta MTU-3 bytes.
#define ANCS_DS_CAP 640
typedef struct {
    uint8_t buf[ANCS_DS_CAP];
    size_t len;
} ancs_ds_t;
void ancs_ds_reset(ancs_ds_t *a);
// Añade un fragmento. Devuelve 1 si la respuesta está completa (parseable), 0 si falta, -1 si se desbordó o es basura.
int ancs_ds_alimentar(ancs_ds_t *a, const uint8_t *frag, size_t n);

// Parsea una respuesta completa de GetNotificationAttributes. Campos ausentes quedan vacíos; los largos se truncan
// respetando UTF-8.
bool ancs_parse_attrs(const uint8_t *d, size_t n, ancs_attrs_t *out);
// Parsea una respuesta completa de GetAppAttributes (app_id + DisplayName).
bool ancs_parse_app_attrs(const uint8_t *d, size_t n, char *app_id, size_t cap_id, char *nombre, size_t cap_nombre);

// Comandos para el Control Point. Devuelven el largo escrito (0 si no cabe).
size_t ancs_cmd_get_attrs(uint8_t *out, size_t cap, uint32_t uid);
size_t ancs_cmd_get_app_attrs(uint8_t *out, size_t cap, const char *app_id);
size_t ancs_cmd_accion(uint8_t *out, size_t cap, uint32_t uid, uint8_t accion);

// Copia como máximo cap-1 bytes de src (largo n) sin partir una secuencia UTF-8; termina en NUL.
void ancs_copiar_utf8(char *dst, size_t cap, const uint8_t *src, size_t n);

#ifdef __cplusplus
}
#endif

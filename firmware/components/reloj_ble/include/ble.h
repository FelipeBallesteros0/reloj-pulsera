#pragma once
// Bluetooth LE (NimBLE): el reloj es periférico, anuncia con la "solicitud" del servicio ANCS de Apple para que el
// iPhone lo liste en Ajustes → Bluetooth, se empareja con bonding (Just Works) y, ya cifrado, lee la hora (CTS).
#include <stdbool.h>
#include <time.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BLE_ANUNCIANDO = 0,   // esperando al iPhone
    BLE_CONECTADO,        // conectado, aún sin cifrar
    BLE_VINCULADO,        // enlace cifrado con bond: listo para CTS/ANCS
} ble_estado_t;

typedef enum {
    BLE_EV_ESTADO = 0,    // cambió el estado (payload: estado)
    BLE_EV_HORA,          // el iPhone entregó la hora (payload: hora)
} ble_evento_tipo_t;

typedef struct {
    ble_evento_tipo_t tipo;
    union {
        ble_estado_t estado;
        struct tm hora;
    };
} ble_evento_t;

// Arranca el host NimBLE. Los eventos se entregan por la cola (elementos ble_evento_t) y nunca desde LVGL.
esp_err_t ble_init(const char *nombre, QueueHandle_t cola);
ble_estado_t ble_estado(void);
bool ble_tiene_bond(void);          // ¿hay algún iPhone emparejado guardado en NVS?
void ble_olvidar(void);             // borra los bonds y corta la conexión (en el iPhone hay que "Omitir dispositivo")
const char *ble_direccion(void);    // "AA:BB:CC:DD:EE:FF"

// Uso interno de los clientes GATT (cts.c, ancs.c): publicar un evento hacia la app.
void ble_publicar(const ble_evento_t *ev);

#ifdef __cplusplus
}
#endif

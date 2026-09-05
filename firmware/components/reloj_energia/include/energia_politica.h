#pragma once
// Regla de energía del reloj: qué debe estar encendido según el estado. C puro, sin estado: se testea en host.
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    bool usb;                  // USB conectado (PMIC)
    bool vigilia;              // ajuste "Oye Andia siempre"
    bool pantalla;             // pantalla encendida
    bool pantalla_por_usuario; // el último encendido fue por toque o PWR (no por un aviso)
    bool conversacion;         // ANDIA en un estado distinto de INACTIVA
} energia_entradas_t;
typedef struct {
    bool wifi;        // WiFi encendido
    bool cpu_rapida;  // candado de frecuencia máxima
    bool dormir;      // light sleep permitido (reposo)
    bool lvgl;        // LVGL en marcha
} energia_salidas_t;
typedef enum { ENERGIA_USB, ENERGIA_CONVERSACION, ENERGIA_VIGILIA, ENERGIA_ACTIVO, ENERGIA_REPOSO } energia_modo_t;
energia_modo_t energia_decidir(const energia_entradas_t *e, energia_salidas_t *s);
const char *energia_modo_nombre(energia_modo_t m);   // "USB", "conversación", "vigilia", "activo", "reposo"
#ifdef __cplusplus
}
#endif

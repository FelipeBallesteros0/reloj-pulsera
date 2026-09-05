#pragma once
// Registro en RAM de la batería (una muestra por minuto) para medir la descarga en %/h sin consola ni USB.
// C puro: se testea en host.
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define BATERIA_REGISTRO_N 240          // 4 h a una muestra por minuto (~1 KB)
typedef struct {
    uint16_t mv[BATERIA_REGISTRO_N];
    uint8_t pct[BATERIA_REGISTRO_N];
    uint16_t n;                         // muestras guardadas (≤ N)
    uint16_t sig;                       // índice de la próxima escritura (anillo)
} bateria_registro_t;
void bateria_registro_init(bateria_registro_t *r);
void bateria_registro_agregar(bateria_registro_t *r, uint16_t mv, uint8_t pct);   // llamar una vez por minuto
// Descarga en décimas de %/h entre la muestra de hace `minutos` (o la más vieja, si hay al menos media ventana)
// y la última. false si no hay muestras suficientes.
bool bateria_registro_descarga(const bateria_registro_t *r, int minutos, int *decimas_pct_h);
#ifdef __cplusplus
}
#endif

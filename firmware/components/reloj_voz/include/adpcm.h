#pragma once
// IMA ADPCM (4 bits por muestra, 4:1) en bloques autocontenidos, idéntico bit a bit a andia/adpcm.py del servidor.
// Bloque = cabecera de 4 bytes (predictor int16 LE, índice de paso, 0) + un nibble por muestra (el bajo primero).
// La cabecera lleva el estado ANTES de la primera muestra: cada bloque se decodifica solo, sin depender del anterior.
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define ADPCM_CABECERA 4
typedef struct {
    int16_t pred;
    uint8_t idx;
} adpcm_estado_t;

// Codifica n muestras (n par; si es impar el último nibble queda en 0) en salida (4 + (n + 1) / 2 bytes).
// Devuelve los bytes escritos. El estado continúa de un bloque al siguiente (mejor calidad).
size_t adpcm_codificar_bloque(adpcm_estado_t *e, const int16_t *pcm, size_t n, uint8_t *salida);
// Toma el estado de la cabecera de un bloque recibido; después se puede decodificar por trozos.
void adpcm_leer_cabecera(adpcm_estado_t *e, const uint8_t *cabecera);
// Decodifica n bytes de datos (2 muestras por byte) y devuelve las muestras escritas (2n).
size_t adpcm_decodificar(adpcm_estado_t *e, const uint8_t *datos, size_t n, int16_t *pcm);
#ifdef __cplusplus
}
#endif

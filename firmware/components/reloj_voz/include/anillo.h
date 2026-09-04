#pragma once
// Búfer anillo de bytes para un productor y un consumidor (sin candados: índices volátiles). C puro, testeable en host.
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint8_t *mem;
    size_t cap;
    volatile size_t escritura;   // índice del productor
    volatile size_t lectura;     // índice del consumidor
} anillo_t;
void anillo_init(anillo_t *a, uint8_t *mem, size_t cap);
size_t anillo_ocupado(const anillo_t *a);
size_t anillo_escribir(anillo_t *a, const uint8_t *datos, size_t n);   // devuelve lo que cupo
size_t anillo_leer(anillo_t *a, uint8_t *destino, size_t n);           // devuelve lo que había
void anillo_vaciar(anillo_t *a);
#ifdef __cplusplus
}
#endif

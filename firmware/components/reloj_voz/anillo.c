#include "anillo.h"

void anillo_init(anillo_t *a, uint8_t *mem, size_t cap)
{
    a->mem = mem;
    a->cap = cap;
    a->escritura = a->lectura = 0;
}

size_t anillo_ocupado(const anillo_t *a) { return (a->escritura + a->cap - a->lectura) % a->cap; }

static size_t libre(const anillo_t *a) { return a->cap - 1 - anillo_ocupado(a); }   // una celda de guarda

size_t anillo_escribir(anillo_t *a, const uint8_t *datos, size_t n)
{
    size_t cabe = libre(a);
    if (n > cabe) {
        n = cabe;
    }
    size_t w = a->escritura;
    for (size_t i = 0; i < n; i++) {
        a->mem[w] = datos[i];
        w = (w + 1) % a->cap;
    }
    a->escritura = w;
    return n;
}

size_t anillo_leer(anillo_t *a, uint8_t *destino, size_t n)
{
    size_t hay = anillo_ocupado(a);
    if (n > hay) {
        n = hay;
    }
    size_t r = a->lectura;
    for (size_t i = 0; i < n; i++) {
        destino[i] = a->mem[r];
        r = (r + 1) % a->cap;
    }
    a->lectura = r;
    return n;
}

void anillo_vaciar(anillo_t *a) { a->lectura = a->escritura; }

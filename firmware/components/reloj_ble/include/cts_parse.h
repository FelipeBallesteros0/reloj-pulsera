#pragma once
// Decodificador de la característica Current Time (0x2A2B) del Current Time Service (0x1805). C puro, testeable en host.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#ifdef __cplusplus
extern "C" {
#endif
// d: año LE16, mes 1-12, día 1-31, hora, minuto, segundo, [día de semana 1=lunes..7=domingo], [1/256 s], [ajuste].
// Devuelve false si faltan bytes o algún campo está fuera de rango.
bool cts_parse(const uint8_t *d, size_t n, struct tm *t);
#ifdef __cplusplus
}
#endif

#include <assert.h>
#include <stdio.h>
#include "bateria_registro.h"

int main(void)
{
    bateria_registro_t r;
    bateria_registro_init(&r);
    int d = 999;
    assert(!bateria_registro_descarga(&r, 30, &d));               // vacío
    bateria_registro_agregar(&r, 4000, 80);
    assert(!bateria_registro_descarga(&r, 30, &d));               // una sola muestra: sin lapso
    // 30 muestras más: de 80 % a 78 % en 30 min → 4,0 %/h
    for (int i = 1; i <= 30; i++) {
        bateria_registro_agregar(&r, 4000, (uint8_t)(80 - (i * 2) / 30));
    }
    assert(bateria_registro_descarga(&r, 30, &d) && d == 40);
    assert(!bateria_registro_descarga(&r, 120, &d));              // 30 min no llegan a media ventana de 2 h
    // 300 muestras constantes: el anillo da la vuelta y la descarga es 0
    for (int i = 0; i < 300; i++) {
        bateria_registro_agregar(&r, 3900, 50);
    }
    assert(r.n == BATERIA_REGISTRO_N);
    assert(bateria_registro_descarga(&r, 120, &d) && d == 0);
    // Cargando: la descarga sale negativa
    for (int i = 0; i < 61; i++) {
        bateria_registro_agregar(&r, 4100, (uint8_t)(50 + i / 6));
    }
    assert(bateria_registro_descarga(&r, 60, &d) && d < 0);
    // Muestras sueltas: la última y la de hace 60 min; fuera de rango no hay
    uint16_t mv; uint8_t pct;
    assert(bateria_registro_muestra(&r, 0, &mv, &pct) && mv == 4100 && pct == 60);
    assert(bateria_registro_muestra(&r, 60, &mv, &pct) && mv == 4100 && pct == 50);
    assert(!bateria_registro_muestra(&r, BATERIA_REGISTRO_N, &mv, &pct));
    puts("test_bateria: OK");
    return 0;
}

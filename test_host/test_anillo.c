#include <stdio.h>
#include <string.h>
#include "anillo.h"

static int fallos = 0;
#define CHECK(c) do { if (!(c)) { printf("FALLO %s:%d: %s\n", __FILE__, __LINE__, #c); fallos++; } } while (0)

int main(void)
{
    uint8_t mem[8], out[8];
    anillo_t a;
    anillo_init(&a, mem, sizeof(mem));
    CHECK(anillo_ocupado(&a) == 0);
    CHECK(anillo_escribir(&a, (uint8_t *)"abcde", 5) == 5);
    CHECK(anillo_ocupado(&a) == 5);
    CHECK(anillo_escribir(&a, (uint8_t *)"fghij", 5) == 2);       // capacidad útil 7: solo caben 2 más
    CHECK(anillo_leer(&a, out, 4) == 4 && memcmp(out, "abcd", 4) == 0);
    CHECK(anillo_escribir(&a, (uint8_t *)"XY", 2) == 2);          // da la vuelta
    CHECK(anillo_leer(&a, out, 8) == 5 && memcmp(out, "efgXY", 5) == 0);
    CHECK(anillo_leer(&a, out, 1) == 0);
    anillo_escribir(&a, (uint8_t *)"zz", 2);
    anillo_vaciar(&a);
    CHECK(anillo_ocupado(&a) == 0);
    printf(fallos ? "test_anillo: %d fallos\n" : "test_anillo: OK\n", fallos);
    return fallos ? 1 : 0;
}

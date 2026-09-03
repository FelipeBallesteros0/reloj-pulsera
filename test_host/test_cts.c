// Tests del decodificador de Current Time (CTS). Ejecutar con: make test
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "cts_parse.h"

static int fallos = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FALLO %s:%d: %s\n", __FILE__, __LINE__, #cond); fallos++; } } while (0)

int main(void)
{
    struct tm t;
    // 2026-09-03 16:45:10, miércoles (CTS: 1=lunes → 3), con fracción y ajuste
    const uint8_t ok[10] = { 0xEA, 0x07, 9, 3, 16, 45, 10, 3, 0, 0 };
    CHECK(cts_parse(ok, sizeof(ok), &t));
    CHECK(t.tm_year == 126 && t.tm_mon == 8 && t.tm_mday == 3);
    CHECK(t.tm_hour == 16 && t.tm_min == 45 && t.tm_sec == 10);
    CHECK(t.tm_wday == 3);
    CHECK(t.tm_isdst == -1);

    // Domingo (7) → tm_wday 0; 7 bytes justos (sin día de semana) → 0
    uint8_t dom[8] = { 0xEA, 0x07, 9, 6, 0, 0, 0, 7 };
    CHECK(cts_parse(dom, 8, &t) && t.tm_wday == 0);
    CHECK(cts_parse(dom, 7, &t) && t.tm_wday == 0);

    // Demasiado corto
    CHECK(!cts_parse(ok, 6, &t));
    CHECK(!cts_parse(NULL, 10, &t));

    // Campos fuera de rango
    uint8_t mal[10];
    memcpy(mal, ok, 10); mal[2] = 13; CHECK(!cts_parse(mal, 10, &t));
    memcpy(mal, ok, 10); mal[3] = 0;  CHECK(!cts_parse(mal, 10, &t));
    memcpy(mal, ok, 10); mal[4] = 24; CHECK(!cts_parse(mal, 10, &t));
    memcpy(mal, ok, 10); mal[5] = 60; CHECK(!cts_parse(mal, 10, &t));
    memcpy(mal, ok, 10); mal[0] = 0x00; mal[1] = 0x00; CHECK(!cts_parse(mal, 10, &t));   // año 0 (iPhone sin hora)

    printf(fallos ? "test_cts: %d fallos\n" : "test_cts: OK\n", fallos);
    return fallos ? 1 : 0;
}

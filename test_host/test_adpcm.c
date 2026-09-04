// Tests del códec IMA ADPCM (mismos vectores que pruebas/test_adpcm.py del servidor).
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "adpcm.h"

static void seno(int16_t *s, int n, double amp)
{
    for (int i = 0; i < n; i++) s[i] = (int16_t)(amp * sin(2 * M_PI * 440 * i / 16000.0));
}

static void vector_a_mano(void)
{
    adpcm_estado_t e = { 0 };
    const int16_t pcm[4] = { 0, 1000, -1000, 0 };
    uint8_t b[8];
    assert(adpcm_codificar_bloque(&e, pcm, 4, b) == 6);
    assert(memcmp(b, "\x00\x00\x00\x00\x70\x2f", 6) == 0);
    assert(e.pred == 2 && e.idx == 15);
    adpcm_estado_t d;
    int16_t s[4];
    adpcm_leer_cabecera(&d, b);
    assert(adpcm_decodificar(&d, b + 4, 2, s) == 4);
    assert(s[0] == 0 && s[1] == 11 && s[2] == -19 && s[3] == 2);
}

static void ida_y_vuelta(void)
{
    static int16_t org[1600], sal[1600];
    static uint8_t b[4 + 800];
    seno(org, 1600, 8000);
    adpcm_estado_t e = { 0 }, d;
    assert(adpcm_codificar_bloque(&e, org, 1600, b) == 804);
    adpcm_leer_cabecera(&d, b);
    assert(adpcm_decodificar(&d, b + 4, 800, sal) == 1600);
    double senal = 0, ruido = 0;
    for (int i = 0; i < 1600; i++) {
        senal += (double)org[i] * org[i];
        ruido += (double)(org[i] - sal[i]) * (org[i] - sal[i]);
    }
    assert(10 * log10(senal / ruido) > 25);
}

static void bloques_independientes_y_decodificacion_por_trozos(void)
{
    static int16_t org[3200], todo[3200], parte[3200];
    static uint8_t b1[804], b2[804];
    seno(org, 3200, 8000);
    adpcm_estado_t e = { 0 };
    adpcm_codificar_bloque(&e, org, 1600, b1);
    adpcm_estado_t tras_1 = e;
    adpcm_codificar_bloque(&e, org + 1600, 1600, b2);
    // La cabecera del segundo bloque es el estado tras el primero.
    assert(b2[0] == (uint8_t)tras_1.pred && b2[1] == (uint8_t)((uint16_t)tras_1.pred >> 8) && b2[2] == tras_1.idx);
    adpcm_estado_t d;
    adpcm_leer_cabecera(&d, b1);
    adpcm_decodificar(&d, b1 + 4, 800, todo);
    adpcm_leer_cabecera(&d, b2);
    adpcm_decodificar(&d, b2 + 4, 800, todo + 1600);
    // El segundo bloque decodificado en trozos de 64 bytes (como llega del WebSocket) da lo mismo.
    adpcm_leer_cabecera(&d, b2);
    size_t m = 0;
    for (size_t i = 0; i < 800; i += 64) {
        size_t n = 800 - i < 64 ? 800 - i : 64;
        m += adpcm_decodificar(&d, b2 + 4 + i, n, parte + m);
    }
    assert(m == 1600 && memcmp(parte, todo + 1600, 3200) == 0);
}

static void extremos_e_impares(void)
{
    static int16_t cuadrada[800], sal[800];
    static uint8_t b[404];
    for (int i = 0; i < 800; i++) cuadrada[i] = (i & 1) ? -32768 : 32767;
    adpcm_estado_t e = { 0 }, d;
    assert(adpcm_codificar_bloque(&e, cuadrada, 800, b) == 404);
    adpcm_leer_cabecera(&d, b);
    assert(adpcm_decodificar(&d, b + 4, 400, sal) == 800);
    const int16_t tres[3] = { 0, 0, 0 };
    assert(adpcm_codificar_bloque(&e, tres, 3, b) == 6);      // 3 muestras → 2 bytes de nibbles
    assert(adpcm_codificar_bloque(&e, tres, 0, b) == 4);
    uint8_t cab_rara[4] = { 0x34, 0x12, 200, 0 };
    adpcm_leer_cabecera(&d, cab_rara);
    assert(d.pred == 0x1234 && d.idx == 88);                 // índice fuera de rango se acota
}

int main(void)
{
    vector_a_mano();
    ida_y_vuelta();
    bloques_independientes_y_decodificacion_por_trozos();
    extremos_e_impares();
    puts("test_adpcm: OK");
    return 0;
}

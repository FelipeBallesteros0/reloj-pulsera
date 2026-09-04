#include "adpcm.h"

static const int16_t PASOS[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88,
    97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
    724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660,
    4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
    18500, 20350, 22385, 24623, 27086, 29794, 32767
};
static const int8_t AJUSTE[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

static inline int16_t saturar(int32_t v)
{
    return v < -32768 ? -32768 : v > 32767 ? 32767 : (int16_t)v;
}

static inline uint8_t ajustar_indice(int idx, uint8_t nib)
{
    idx += AJUSTE[nib];
    return idx < 0 ? 0 : idx > 88 ? 88 : (uint8_t)idx;
}

size_t adpcm_codificar_bloque(adpcm_estado_t *e, const int16_t *pcm, size_t n, uint8_t *salida)
{
    salida[0] = (uint8_t)e->pred;
    salida[1] = (uint8_t)((uint16_t)e->pred >> 8);
    salida[2] = e->idx;
    salida[3] = 0;
    size_t fuera = ADPCM_CABECERA;
    int32_t pred = e->pred;
    int idx = e->idx;
    uint8_t byte = 0;
    for (size_t i = 0; i < n; i++) {
        int32_t paso = PASOS[idx];
        int32_t dif = pcm[i] - pred;
        uint8_t nib = 0;
        if (dif < 0) {
            nib = 8;
            dif = -dif;
        }
        int32_t vp = paso >> 3;
        if (dif >= paso) { nib |= 4; dif -= paso; vp += paso; }
        paso >>= 1;
        if (dif >= paso) { nib |= 2; dif -= paso; vp += paso; }
        paso >>= 1;
        if (dif >= paso) { nib |= 1; vp += paso; }
        pred = saturar((nib & 8) ? pred - vp : pred + vp);
        idx = ajustar_indice(idx, nib);
        if (i & 1) {
            salida[fuera++] = byte | (uint8_t)(nib << 4);
        } else {
            byte = nib;
        }
    }
    if (n & 1) {
        salida[fuera++] = byte;
    }
    e->pred = (int16_t)pred;
    e->idx = (uint8_t)idx;
    return fuera;
}

void adpcm_leer_cabecera(adpcm_estado_t *e, const uint8_t *c)
{
    e->pred = (int16_t)((uint16_t)c[0] | ((uint16_t)c[1] << 8));
    e->idx = c[2] > 88 ? 88 : c[2];
}

size_t adpcm_decodificar(adpcm_estado_t *e, const uint8_t *datos, size_t n, int16_t *pcm)
{
    int32_t pred = e->pred;
    int idx = e->idx;
    size_t fuera = 0;
    for (size_t i = 0; i < n; i++) {
        for (int mitad = 0; mitad < 2; mitad++) {
            uint8_t nib = mitad ? (datos[i] >> 4) : (datos[i] & 0x0F);
            int32_t paso = PASOS[idx];
            int32_t vp = paso >> 3;
            if (nib & 4) vp += paso;
            if (nib & 2) vp += paso >> 1;
            if (nib & 1) vp += paso >> 2;
            pred = saturar((nib & 8) ? pred - vp : pred + vp);
            idx = ajustar_indice(idx, nib);
            pcm[fuera++] = (int16_t)pred;
        }
    }
    e->pred = (int16_t)pred;
    e->idx = (uint8_t)idx;
    return fuera;
}

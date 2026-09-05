#include "bateria_registro.h"
#include <string.h>

void bateria_registro_init(bateria_registro_t *r) { memset(r, 0, sizeof(*r)); }

void bateria_registro_agregar(bateria_registro_t *r, uint16_t mv, uint8_t pct)
{
    r->mv[r->sig] = mv;
    r->pct[r->sig] = pct;
    r->sig = (uint16_t)((r->sig + 1) % BATERIA_REGISTRO_N);
    if (r->n < BATERIA_REGISTRO_N) {
        r->n++;
    }
}

// Índice de la muestra guardada hace `atras` minutos (0 = la última).
static uint16_t indice(const bateria_registro_t *r, int atras)
{
    return (uint16_t)((r->sig + BATERIA_REGISTRO_N - 1 - atras) % BATERIA_REGISTRO_N);
}

bool bateria_registro_descarga(const bateria_registro_t *r, int minutos, int *decimas_pct_h)
{
    int k = (minutos + 1 < r->n) ? minutos + 1 : r->n;   // muestras que caben en la ventana
    int lapso = k - 1;                                    // minutos entre la más vieja y la última
    if (lapso < 1 || lapso * 2 < minutos) {               // hace falta al menos media ventana
        return false;
    }
    int caida = (int)r->pct[indice(r, lapso)] - (int)r->pct[indice(r, 0)];
    *decimas_pct_h = caida * 600 / lapso;                 // % por minuto × 60 × 10
    return true;
}

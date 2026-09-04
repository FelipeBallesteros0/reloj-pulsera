#pragma once
// Micrófono (ES7210) y altavoz (ES8311) a 16 kHz. La captura corre en una tarea propia y entrega tramas mono de
// 1024 B; la reproducción consume un búfer anillo con 200 ms de reserva para no entrecortar la voz.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
#define VOZ_TASA 16000
#define VOZ_TRAMA_BYTES 1024      // 512 muestras mono
esp_err_t voz_init(void);
esp_err_t voz_capturar_iniciar(void (*cb)(const uint8_t *pcm, size_t n));
void voz_capturar_detener(void);
bool voz_capturando(void);
size_t voz_reproducir_alimentar(const uint8_t *pcm, size_t n);   // devuelve lo que cupo en el anillo
void voz_reproducir_fin(void);        // no llegará más audio: tocar lo que quede y parar
void voz_reproducir_parar(void);      // corta ya (cancelar)
bool voz_reproduciendo(void);
void voz_volumen(int pct);
#ifdef __cplusplus
}
#endif

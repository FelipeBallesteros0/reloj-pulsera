#include "voz.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "esp_codec_dev.h"
#include "bsp/esp-bsp.h"
#include "anillo.h"

static const char *TAG = "voz";
static void tarea_voz(void *p);
static esp_codec_dev_handle_t mic, alt;
static void (*cb_captura)(const uint8_t *, size_t);
static volatile bool capturando, reproduciendo, fin_audio, tocando;
static int tramas_a_descartar;             // tras reproducir, el DMA del micrófono trae lo acumulado (incluido el altavoz)
#define TRAMAS_TRAS_REPRODUCIR 8            // ~256 ms
static uint8_t mem_anillo[12 * 1024];      // 375 ms de audio mono (la reserva es de 200 ms)
static anillo_t anillo;
static TaskHandle_t tarea;
#define RESERVA (VOZ_TASA * 2 * 200 / 1000)
#define TRAMAS_POR_INFORME 32              // ~1 s de captura entre líneas de diagnóstico

esp_err_t voz_init(void)
{
    alt = bsp_audio_codec_speaker_init();
    mic = bsp_audio_codec_microphone_init();
    ESP_RETURN_ON_FALSE(alt && mic, ESP_FAIL, TAG, "no se pudo iniciar el códec");
    // 16 kHz, 16 bit, MONO en el bus: la mitad de DMA que el ejemplo de Waveshare (que usa los dos micrófonos).
    esp_codec_dev_sample_info_t fs = { .bits_per_sample = 16, .channel = 1, .sample_rate = VOZ_TASA };
    ESP_RETURN_ON_FALSE(esp_codec_dev_open(alt, &fs) == 0, ESP_FAIL, TAG, "abrir altavoz");
    ESP_RETURN_ON_FALSE(esp_codec_dev_open(mic, &fs) == 0, ESP_FAIL, TAG, "abrir micrófono");
    esp_codec_dev_set_in_gain(mic, 36.0f);   // con 24 dB la voz llegaba a -28 dBFS
    esp_codec_dev_set_out_vol(alt, 60);
    anillo_init(&anillo, mem_anillo, sizeof(mem_anillo));
    // Una sola tarea permanente para captura y reproducción (nunca ocurren a la vez): así no hay que reservar
    // pila en plena conversación, cuando la RAM está en su mínimo con TLS abierto.
    // Prioridad por encima del cliente WebSocket (5) y de LVGL (4): el DMA del altavoz no debe quedarse sin datos.
    ESP_RETURN_ON_FALSE(xTaskCreate(tarea_voz, "voz", 4096, NULL, 6, &tarea) == pdPASS, ESP_FAIL, TAG, "tarea");
    ESP_LOGI(TAG, "códec listo (16 kHz mono)");
    return ESP_OK;
}

// Diagnóstico de la captura: cada ~1 s, nivel máximo (0-32767) y cuánto tardan la lectura del códec (debería
// rondar los 32 ms de una trama) y la entrega al callback (la red).
static void medir_nivel(const int16_t *pcm, size_t n, int64_t lectura_us, int64_t entrega_us)
{
    static int nivel_max, tramas;
    static int64_t lectura_total, entrega_total;
    for (size_t i = 0; i < n; i++) {
        int v = pcm[i] < 0 ? -pcm[i] : pcm[i];
        if (v > nivel_max) {
            nivel_max = v;
        }
    }
    lectura_total += lectura_us;
    entrega_total += entrega_us;
    if (++tramas >= TRAMAS_POR_INFORME) {
        ESP_LOGI(TAG, "captura: %d tramas, nivel máx %d, lectura media %d ms, entrega media %d ms", tramas, nivel_max,
                 (int)(lectura_total / tramas / 1000), (int)(entrega_total / tramas / 1000));
        tramas = 0;
        nivel_max = 0;
        lectura_total = entrega_total = 0;
    }
}

static void tarea_voz(void *p)
{
    static int16_t buf[512];
    for (;;) {
        // La reproducción manda: en vigilia la captura no se detiene nunca y, si tuviera prioridad, la voz de
        // ANDIA jamás sonaría. Mientras se toca, el micrófono pierde tramas (da igual: el detector está parado).
        if (reproduciendo) {
            // Espera la reserva (o el fin, si la respuesta es más corta) antes de empezar a tocar.
            if (!tocando && anillo_ocupado(&anillo) < RESERVA && !fin_audio) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            tocando = true;
            size_t n = anillo_leer(&anillo, (uint8_t *)buf, sizeof(buf));   // 1024 B = 32 ms por escritura
            if (n == 0) {
                if (fin_audio) {
                    reproduciendo = false;
                    tocando = false;
                    tramas_a_descartar = TRAMAS_TRAS_REPRODUCIR;
                } else {
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
                continue;
            }
            esp_codec_dev_write(alt, buf, n);
        } else if (capturando) {
            int64_t t0 = esp_timer_get_time();
            if (esp_codec_dev_read(mic, buf, sizeof(buf)) == 0) {
                int64_t t1 = esp_timer_get_time();
                if (tramas_a_descartar > 0) {
                    tramas_a_descartar--;
                    continue;
                }
                if (cb_captura && capturando) {
                    cb_captura((const uint8_t *)buf, sizeof(buf));
                }
                medir_nivel(buf, 512, t1 - t0, esp_timer_get_time() - t1);
            } else {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        } else {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
        }
    }
}

esp_err_t voz_capturar_iniciar(void (*cb)(const uint8_t *, size_t))
{
    ESP_RETURN_ON_FALSE(mic && tarea, ESP_ERR_INVALID_STATE, TAG, "micrófono no disponible");
    if (capturando) {
        return ESP_OK;
    }
    cb_captura = cb;
    capturando = true;
    xTaskNotifyGive(tarea);
    ESP_LOGI(TAG, "captura iniciada");
    return ESP_OK;
}

void voz_capturar_detener(void)
{
    if (capturando) {
        capturando = false;
        ESP_LOGI(TAG, "captura detenida");
    }
}
bool voz_capturando(void) { return capturando; }

size_t voz_reproducir_alimentar(const uint8_t *pcm, size_t n)
{
    size_t cupo = anillo_escribir(&anillo, pcm, n);
    if (!reproduciendo) {
        fin_audio = false;
        tocando = false;
        reproduciendo = true;
        if (tarea) {
            xTaskNotifyGive(tarea);
        }
        ESP_LOGI(TAG, "reproducción iniciada");
    }
    return cupo;
}

void voz_reproducir_fin(void) { fin_audio = true; }
void voz_reproducir_parar(void) { anillo_vaciar(&anillo); fin_audio = true; reproduciendo = false; tocando = false; }
bool voz_reproduciendo(void) { return reproduciendo; }
void voz_ganancia_mic(float db)
{
    if (mic) {
        esp_codec_dev_set_in_gain(mic, db);
    }
}

void voz_volumen(int pct)
{
    if (alt) {
        esp_codec_dev_set_out_vol(alt, pct);
    }
}

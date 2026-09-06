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
static bool abierto;                       // códec abierto (canales I²S habilitados, amplificador encendido)
static int volumen_pct = 60;               // se aplican en cada apertura (el códec cerrado no acepta ajustes)
static float ganancia_db = 36.0f;          // con 24 dB la voz llegaba a -28 dBFS
static int ciclos_ocioso;                  // ciclos de 100 ms sin captura ni reproducción
#define CICLOS_PARA_CERRAR 20              // ~2 s
static struct { int huecos, escrituras; int64_t inicio_us; } rep;   // diagnóstico de la reproducción
#define TRAMAS_TRAS_REPRODUCIR 8            // ~256 ms
static uint8_t mem_anillo[16 * 1024];      // 500 ms de audio mono (la reserva es de 400 ms)
static anillo_t anillo;
static TaskHandle_t tarea;
#define RESERVA (VOZ_TASA * 2 * 400 / 1000)
#define TRAMAS_POR_INFORME 32              // ~1 s de captura entre líneas de diagnóstico

esp_err_t voz_init(void)
{
    alt = bsp_audio_codec_speaker_init();
    mic = bsp_audio_codec_microphone_init();
    ESP_RETURN_ON_FALSE(alt && mic, ESP_FAIL, TAG, "no se pudo iniciar el códec");
    // El códec no se abre aquí: lo abre la tarea de voz al primer uso y lo cierra tras ~2 s sin audio (ver abrir/cerrar).
    anillo_init(&anillo, mem_anillo, sizeof(mem_anillo));
    // Una sola tarea permanente para captura y reproducción (nunca ocurren a la vez): así no hay que reservar
    // pila en plena conversación, cuando la RAM está en su mínimo con TLS abierto.
    // Prioridad por encima del cliente WebSocket (5) y de LVGL (4): el DMA del altavoz no debe quedarse sin datos.
    ESP_RETURN_ON_FALSE(xTaskCreate(tarea_voz, "voz", 4096, NULL, 6, &tarea) == pdPASS, ESP_FAIL, TAG, "tarea");
    ESP_LOGI(TAG, "códec listo (16 kHz mono; se abre al usarse)");
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

// 16 kHz, 16 bit, MONO en el bus: la mitad de DMA que el ejemplo de Waveshare (que usa los dos micrófonos).
static esp_err_t abrir(void)
{
    if (abierto) {
        return ESP_OK;
    }
    esp_codec_dev_sample_info_t fs = { .bits_per_sample = 16, .channel = 1, .sample_rate = VOZ_TASA };
    ESP_RETURN_ON_FALSE(esp_codec_dev_open(alt, &fs) == 0, ESP_FAIL, TAG, "abrir altavoz");
    ESP_RETURN_ON_FALSE(esp_codec_dev_open(mic, &fs) == 0, ESP_FAIL, TAG, "abrir micrófono");
    esp_codec_dev_set_in_gain(mic, ganancia_db);
    esp_codec_dev_set_out_vol(alt, volumen_pct);
    tramas_a_descartar = TRAMAS_TRAS_REPRODUCIR;   // el DMA recién arrancado trae basura
    abierto = true;
    ciclos_ocioso = 0;
    ESP_LOGI(TAG, "códec abierto");
    return ESP_OK;
}

static void cerrar(void)
{
    if (!abierto) {
        return;
    }
    esp_codec_dev_close(mic);   // deshabilita los canales I²S (suelta el candado de PM del driver)
    esp_codec_dev_close(alt);   // y el ES8311 baja el GPIO6 del amplificador
    abierto = false;
    ESP_LOGI(TAG, "códec cerrado");
}

static void tarea_voz(void *p)
{
    static int16_t buf[512];
    for (;;) {
        if ((reproduciendo || capturando) && !abierto && abrir() != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        // La reproducción manda: en vigilia la captura no se detiene nunca y, si tuviera prioridad, la voz de
        // ANDIA jamás sonaría. Mientras se toca, el micrófono pierde tramas (da igual: el detector está parado).
        if (reproduciendo) {
            // Espera la reserva (o el fin, si la respuesta es más corta) antes de empezar a tocar.
            if (!tocando && anillo_ocupado(&anillo) < RESERVA && !fin_audio) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            if (!tocando) {
                rep.huecos = rep.escrituras = 0;
                rep.inicio_us = esp_timer_get_time();
            }
            tocando = true;
            size_t n = anillo_leer(&anillo, (uint8_t *)buf, sizeof(buf));   // 1024 B = 32 ms por escritura
            if (n == 0) {
                if (fin_audio) {
                    // Cola de silencio: si no, el DMA del I2S repite el último búfer de voz hasta la próxima escritura.
                    memset(buf, 0, sizeof(buf));
                    for (int k = 0; k < 4; k++) {
                        esp_codec_dev_write(alt, buf, sizeof(buf));
                    }
                    reproduciendo = false;
                    tocando = false;
                    tramas_a_descartar = TRAMAS_TRAS_REPRODUCIR;
                    ESP_LOGI(TAG, "reproducción: %d ms, %d escrituras, %d huecos (anillo vacío)",
                             (int)((esp_timer_get_time() - rep.inicio_us) / 1000), rep.escrituras, rep.huecos);
                } else {
                    rep.huecos++;
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
                continue;
            }
            rep.escrituras++;
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
        } else if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100)) == 0) {   // sin trabajo: contar hasta cerrar
            if (abierto && ++ciclos_ocioso >= CICLOS_PARA_CERRAR) {
                cerrar();
            }
        } else {
            ciclos_ocioso = 0;
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
    fin_audio = false;      // audio nuevo anula cualquier "fin" pendiente (p. ej. el de un pitido justo antes)
    if (!reproduciendo) {
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
    ganancia_db = db;
    if (mic && abierto) {
        esp_codec_dev_set_in_gain(mic, db);
    }
}

void voz_volumen(int pct)
{
    volumen_pct = pct;
    if (alt && abierto) {
        esp_codec_dev_set_out_vol(alt, pct);
    }
}

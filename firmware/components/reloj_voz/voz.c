#include "voz.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "bsp/esp-bsp.h"
#include "anillo.h"

static const char *TAG = "voz";
static esp_codec_dev_handle_t mic, alt;
static void (*cb_captura)(const uint8_t *, size_t);
static volatile bool capturando, reproduciendo, fin_audio;
static uint8_t mem_anillo[16 * 1024];      // 0,5 s de audio mono (la reserva es de 200 ms)
static anillo_t anillo;
#define RESERVA (VOZ_TASA * 2 * 200 / 1000)

esp_err_t voz_init(void)
{
    alt = bsp_audio_codec_speaker_init();
    mic = bsp_audio_codec_microphone_init();
    ESP_RETURN_ON_FALSE(alt && mic, ESP_FAIL, TAG, "no se pudo iniciar el códec");
    // Como el ejemplo de Waveshare: 16 kHz, 16 bit, 2 canales en el bus (dos micrófonos); la app trabaja en mono.
    esp_codec_dev_sample_info_t fs = { .bits_per_sample = 16, .channel = 2, .sample_rate = VOZ_TASA };
    ESP_RETURN_ON_FALSE(esp_codec_dev_open(alt, &fs) == 0, ESP_FAIL, TAG, "abrir altavoz");
    ESP_RETURN_ON_FALSE(esp_codec_dev_open(mic, &fs) == 0, ESP_FAIL, TAG, "abrir micrófono");
    esp_codec_dev_set_in_gain(mic, 24.0f);
    esp_codec_dev_set_out_vol(alt, 60);
    anillo_init(&anillo, mem_anillo, sizeof(mem_anillo));
    ESP_LOGI(TAG, "códec listo (16 kHz; estéreo en el bus, mono en la app)");
    return ESP_OK;
}

static void tarea_captura(void *p)
{
    static int16_t estereo[512 * 2];
    static int16_t mono[512];
    while (capturando) {
        if (esp_codec_dev_read(mic, estereo, sizeof(estereo)) == 0) {
            for (int i = 0; i < 512; i++) {
                mono[i] = estereo[2 * i];       // canal izquierdo
            }
            if (cb_captura && capturando) {
                cb_captura((const uint8_t *)mono, sizeof(mono));
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    vTaskDelete(NULL);
}

esp_err_t voz_capturar_iniciar(void (*cb)(const uint8_t *, size_t))
{
    if (capturando) {
        return ESP_OK;
    }
    cb_captura = cb;
    capturando = true;
    return xTaskCreate(tarea_captura, "voz_cap", 4096, NULL, 5, NULL) == pdPASS ? ESP_OK : ESP_FAIL;
}

void voz_capturar_detener(void) { capturando = false; }
bool voz_capturando(void) { return capturando; }

static void tarea_reproduccion(void *p)
{
    static int16_t mono[256];
    static int16_t estereo[256 * 2];
    // Espera la reserva (o el fin, si la respuesta es más corta) antes de empezar a tocar.
    while (reproduciendo && anillo_ocupado(&anillo) < RESERVA && !fin_audio) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    while (reproduciendo) {
        size_t n = anillo_leer(&anillo, (uint8_t *)mono, sizeof(mono));
        if (n == 0) {
            if (fin_audio) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        size_t muestras = n / 2;
        for (size_t i = 0; i < muestras; i++) {
            estereo[2 * i] = estereo[2 * i + 1] = mono[i];
        }
        esp_codec_dev_write(alt, estereo, muestras * 4);
    }
    reproduciendo = false;
    vTaskDelete(NULL);
}

size_t voz_reproducir_alimentar(const uint8_t *pcm, size_t n)
{
    size_t cupo = anillo_escribir(&anillo, pcm, n);
    if (!reproduciendo) {
        reproduciendo = true;
        fin_audio = false;
        xTaskCreate(tarea_reproduccion, "voz_rep", 4096, NULL, 5, NULL);
    }
    return cupo;
}

void voz_reproducir_fin(void) { fin_audio = true; }
void voz_reproducir_parar(void) { anillo_vaciar(&anillo); fin_audio = true; }
bool voz_reproduciendo(void) { return reproduciendo; }
void voz_volumen(int pct)
{
    if (alt) {
        esp_codec_dev_set_out_vol(alt, pct);
    }
}

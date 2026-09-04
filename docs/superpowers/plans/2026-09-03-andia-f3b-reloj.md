# ANDIA F3b — Cliente de audio en el reloj: plan de implementación

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Que el reloj hable con ANDIA: un botón de micrófono en la carátula (y PWR largo) abre una conversación por
WebSocket con la Pi, el micrófono ES7210 manda PCM 16 kHz, la respuesta se muestra en pantalla y suena por el
altavoz ES8311 con la voz de Mayuri; los `aviso` de la Pi (recordatorios, `andia "texto"`) aparecen como
notificaciones y los `temporizador` se cuentan en el reloj.

**Architecture:** Dos componentes nuevos en `firmware/components/`: `reloj_red` (WiFi STA con lista de redes y
ahorro de energía) y `reloj_voz` (códec: captura en tarea propia → cola; reproducción desde un búfer anillo con
reserva). Un tercero, `reloj_andia`, implementa el protocolo WebSocket de la spec (sección 5) sobre
`esp_websocket_client` y publica eventos a la tarea de la app por la misma cola `ble_evento_t`... no: por una cola
propia `andia_evento_t`, para no acoplar BLE y ANDIA. La UI gana una pantalla ANDIA y un botón de micrófono. El
gesto de muñeca (F3c) no entra aquí.

**Tech Stack:** ESP-IDF v5.5.5, `esp_wifi` + `lwip`, componente gestionado `espressif/esp_websocket_client ^1.8`,
`esp_codec_dev` (ya viene con el BSP), LVGL 9.5, NimBLE (sigue igual). Servidor: `~/andia` en la Pi (puerto 8001).

**Spec:** `docs/superpowers/specs/2026-09-03-andia-diseno.md` (secciones 4, 5, 7, 8 y 9). Protocolo ya implementado y
probado del lado Pi (F3a): `hola`, `inicio`, audio, `cancelar`, `ping` → `hola`, `activada`, `pensando`, `pregunta`,
`respuesta`, audio, `temporizador`, `fin`, `error`, `aviso`, `pong`.

## Global Constraints

- Audio PCM 16 000 Hz, 16 bit. **El ESP32-C6 solo tiene I2S0: `CONFIG_BSP_I2S_NUM=0`** (el BSP trae 1 por defecto).
  El códec se abre a 16 kHz / 16 bit / **2 canales** (así lo hace Waveshare con el ES7210: dos micrófonos); captura =
  canal izquierdo desentrelazado → mono; reproducción = cada muestra duplicada → estéreo. Ganancia ADC 24 dB, volumen 60.
- Tramas al servidor de 1024 B (512 muestras, 32 ms). Reproducción con búfer anillo de 32 KB (1 s) y reserva de
  **200 ms** antes de empezar (sin esto la voz sale entrecortada: lección del reloj falso).
- RAM: medir con `latido` tras conectar WiFi y tras una conversación; objetivo ≥ 60 KB libres en el peor momento.
  Búferes WiFi mínimos: `CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=4`, `DYNAMIC_RX=8`, `DYNAMIC_TX=8`, sin IRAM opt.
- La única tarea que toca LVGL sigue siendo la de la app (`main.c`); WiFi/WebSocket/audio solo encolan eventos.
- Credenciales WiFi y token de ANDIA **fuera de git**: Kconfig con valores por defecto vacíos, rellenados en `sdkconfig`
  (ya ignorado por git). Redes: casa (SSID/clave de Felipe) y hotspot `Felipe ` (con espacio final) / `***REMOVED***`.
- BLE + WiFi coexisten en un solo núcleo: `CONFIG_ESP_COEX_SW_COEXIST_ENABLE=y`. Se acepta que ANCS se corte
  durante una conversación (se recupera solo).
- Servidor: `ws://192.168.x.x:8001/ws` (LAN); `wss://` por Cloudflare queda para F3e.

---

## Estructura de archivos

```
firmware/components/reloj_red/      CMakeLists.txt, include/red.h, red.c, Kconfig
firmware/components/reloj_voz/      CMakeLists.txt, include/voz.h, voz.c, anillo.c, include/anillo.h
firmware/components/reloj_andia/    CMakeLists.txt, include/andia.h, andia.c, idf_component.yml (esp_websocket_client), Kconfig
firmware/components/reloj_ui/       ui.c (+ pantalla ANDIA, botón micrófono, icono WiFi), include/ui.h
firmware/main/main.c                integración: cola de ANDIA, temporizador local, avisos → notificaciones
firmware/sdkconfig.defaults         WiFi, coexistencia, I2S0, búferes
test_host/test_anillo.c, Makefile   anillo de audio en host
```

---

### Task 1: `reloj_red` — WiFi con lista de redes y ahorro de energía

**Files:**
- Create: `firmware/components/reloj_red/CMakeLists.txt`, `firmware/components/reloj_red/Kconfig`, `firmware/components/reloj_red/include/red.h`, `firmware/components/reloj_red/red.c`
- Modify: `firmware/sdkconfig.defaults`, `firmware/main/CMakeLists.txt` (REQUIRES reloj_red), `firmware/main/main.c`

**Interfaces:**
- Produces: `esp_err_t red_init(QueueHandle_t cola_eventos)` (arranca WiFi STA y reintenta solo); `bool red_conectada(void)`; `const char *red_ip(void)`; `const char *red_ssid(void)`; evento `red_evento_t {tipo: RED_CONECTADA | RED_DESCONECTADA}` que se manda a la cola (tipo `andia_evento_t`, ver Task 3: `ANDIA_EV_RED`).

- [ ] **Step 1: Kconfig y CMake**

`firmware/components/reloj_red/Kconfig`:
```
menu "Reloj: WiFi"
    config RELOJ_WIFI1_SSID
        string "Red 1 (casa): SSID"
        default ""
    config RELOJ_WIFI1_CLAVE
        string "Red 1 (casa): clave"
        default ""
    config RELOJ_WIFI2_SSID
        string "Red 2 (hotspot): SSID"
        default "Felipe "
        help
            El hotspot del iPhone de Felipe se llama "Felipe " con un espacio al final (se compara byte a byte).
    config RELOJ_WIFI2_CLAVE
        string "Red 2 (hotspot): clave"
        default ""
endmenu
```
`firmware/components/reloj_red/CMakeLists.txt`:
```
idf_component_register(SRCS "red.c" INCLUDE_DIRS "include" REQUIRES esp_wifi esp_netif esp_event nvs_flash reloj_andia)
```
(`reloj_andia` aporta el tipo de evento; ver Task 3. Para compilar la Task 1 sola, crear primero `reloj_andia/include/andia.h` con el enum de eventos de la Task 3.)

- [ ] **Step 2: `red.h`**

```c
#pragma once
// WiFi en modo estación con dos redes conocidas (casa, hotspot), reconexión automática y ahorro de energía.
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t red_init(QueueHandle_t cola_eventos);   // publica andia_evento_t {ANDIA_EV_RED, red_conectada}
bool red_conectada(void);
const char *red_ip(void);      // "192.168.x.x" o "--"
const char *red_ssid(void);    // red actual o "--"
#ifdef __cplusplus
}
#endif
```

- [ ] **Step 3: `red.c`** (prueba la red 1; si falla 3 veces, la red 2; y así en bucle con espera creciente)

```c
#include "red.h"
#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "andia.h"
#include "sdkconfig.h"

static const char *TAG = "red";
static QueueHandle_t cola;
static bool conectada;
static char ip[16] = "--";
static char ssid_actual[33] = "--";
static int red_idx, fallos;
static esp_netif_t *netif;

typedef struct { const char *ssid; const char *clave; } red_t;
static const red_t REDES[] = {
    { CONFIG_RELOJ_WIFI1_SSID, CONFIG_RELOJ_WIFI1_CLAVE },
    { CONFIG_RELOJ_WIFI2_SSID, CONFIG_RELOJ_WIFI2_CLAVE },
};
#define N_REDES (sizeof(REDES) / sizeof(REDES[0]))

static void publicar(bool estado)
{
    conectada = estado;
    if (cola) {
        andia_evento_t ev = { .tipo = ANDIA_EV_RED, .red_conectada = estado };
        xQueueSend(cola, &ev, 0);
    }
}

static void conectar_a(int idx)
{
    wifi_config_t cfg = { 0 };
    strncpy((char *)cfg.sta.ssid, REDES[idx].ssid, sizeof(cfg.sta.ssid));
    strncpy((char *)cfg.sta.password, REDES[idx].clave, sizeof(cfg.sta.password));
    cfg.sta.threshold.authmode = strlen(REDES[idx].clave) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    cfg.sta.listen_interval = 3;   // DTIM: despierta cada 3 beacons en ahorro de energía
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    strncpy(ssid_actual, REDES[idx].ssid, sizeof(ssid_actual) - 1);
    ESP_LOGI(TAG, "conectando a \"%s\"", REDES[idx].ssid);
    esp_wifi_connect();
}

static void siguiente_red(void)
{
    // Salta las redes sin SSID configurado.
    for (unsigned i = 0; i < N_REDES; i++) {
        red_idx = (red_idx + 1) % N_REDES;
        if (REDES[red_idx].ssid[0]) {
            return;
        }
    }
}

static void al_evento(void *arg, esp_event_base_t base, int32_t id, void *datos)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        conectar_a(red_idx);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *d = datos;
        if (conectada) {
            ESP_LOGW(TAG, "desconectado de \"%s\" (motivo %d)", ssid_actual, d->reason);
            strcpy(ip, "--");
            publicar(false);
        }
        fallos++;
        if (fallos >= 3) {
            fallos = 0;
            siguiente_red();
        }
        int espera_ms = 1000 + 1000 * (fallos % 5);
        vTaskDelay(pdMS_TO_TICKS(espera_ms));
        conectar_a(red_idx);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = datos;
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&e->ip_info.ip));
        fallos = 0;
        ESP_LOGI(TAG, "conectado a \"%s\", IP %s", ssid_actual, ip);
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
        publicar(true);
    }
}

esp_err_t red_init(QueueHandle_t cola_eventos)
{
    cola = cola_eventos;
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif");
    esp_err_t r = esp_event_loop_create_default();
    if (r != ESP_OK && r != ESP_ERR_INVALID_STATE) {
        return r;
    }
    netif = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(netif, "reloj");
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi_init");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, al_evento, NULL), TAG, "ev");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, al_evento, NULL), TAG, "ev");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "storage");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "mode");
    if (!REDES[0].ssid[0]) {
        siguiente_red();
    }
    return esp_wifi_start();
}

bool red_conectada(void) { return conectada; }
const char *red_ip(void) { return ip; }
const char *red_ssid(void) { return ssid_actual; }
```
(Incluir `esp_check.h` para `ESP_RETURN_ON_ERROR` y `freertos/task.h` para `vTaskDelay`.)

- [ ] **Step 4: sdkconfig y credenciales**

Añadir a `firmware/sdkconfig.defaults`:
```
# ---- WiFi (solo cuando ANDIA lo pide; búferes mínimos para caber en RAM) ----
CONFIG_ESP_COEX_SW_COEXIST_ENABLE=y
CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=4
CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=8
CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=8
CONFIG_ESP_WIFI_IRAM_OPT=n
CONFIG_ESP_WIFI_RX_IRAM_OPT=n
CONFIG_ESP_WIFI_NVS_ENABLED=n
# I2S: el ESP32-C6 solo tiene I2S0
CONFIG_BSP_I2S_NUM=0
```
Credenciales (fuera de git) en `firmware/sdkconfig` tras `idf.py reconfigure`:
`CONFIG_RELOJ_WIFI1_SSID="…"`, `CONFIG_RELOJ_WIFI1_CLAVE="…"`, `CONFIG_RELOJ_WIFI2_CLAVE="***REMOVED***"`.

- [ ] **Step 5: Integrar en `main.c`** (crear la cola de ANDIA, llamar `red_init(cola_andia)` tras `ble_init`, mostrar
IP/estado en el `latido` y en la UI: `ui_red(bool conectada, const char *ip)`) y compilar/flashear.

Verificación: en consola `red: conectado a "<casa>", IP 192.168.1.x`; desde WSL `ping -c 3 192.168.1.x` responde;
`latido` muestra la RAM libre tras WiFi (anotar en `docs/ram.md`). Apagar el router del hotspot/cambiar de red:
reconecta solo. BLE sigue vinculado (`BLE 2` en el latido).

- [ ] **Step 6: Commit** `git commit -m "reloj_red: WiFi con dos redes, reconexión y ahorro de energía"`

---

### Task 2: `reloj_voz` — captura y reproducción a 16 kHz con búfer anillo

**Files:**
- Create: `firmware/components/reloj_voz/CMakeLists.txt`, `include/anillo.h`, `anillo.c`, `include/voz.h`, `voz.c`
- Test: `test_host/test_anillo.c`, `test_host/Makefile`

**Interfaces:**
- Produces: `anillo_t` con `anillo_init(anillo_t *, uint8_t *mem, size_t cap)`, `anillo_escribir(anillo_t *, const uint8_t *, size_t) -> size_t`, `anillo_leer(anillo_t *, uint8_t *, size_t) -> size_t`, `anillo_ocupado(anillo_t *) -> size_t`, `anillo_vaciar(anillo_t *)` (un productor, un consumidor; sin locks: índices `volatile`).
  `voz_init(void)`; `voz_capturar_iniciar(void (*cb)(const uint8_t *pcm, size_t n))` y `voz_capturar_detener(void)` (tarea de captura: lee 512 muestras estéreo del códec, desentrelaza el canal izquierdo y llama a `cb` con 1024 B mono); `voz_reproducir_alimentar(const uint8_t *pcm, size_t n)`, `voz_reproducir_fin(void)` (marca que ya no llegará más audio: reproduce lo que quede), `voz_reproducir_parar(void)`, `bool voz_reproduciendo(void)`; `voz_volumen(int pct)`.

- [ ] **Step 1: Test del anillo (falla)**

`test_host/test_anillo.c`:
```c
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
    CHECK(anillo_escribir(&a, (uint8_t *)"fghij", 5) == 3);       // solo caben 3 más (capacidad 8)
    CHECK(anillo_leer(&a, out, 4) == 4 && memcmp(out, "abcd", 4) == 0);
    CHECK(anillo_escribir(&a, (uint8_t *)"XY", 2) == 2);          // da la vuelta
    CHECK(anillo_leer(&a, out, 8) == 6 && memcmp(out, "efghXY", 6) == 0);
    CHECK(anillo_leer(&a, out, 1) == 0);
    anillo_escribir(&a, (uint8_t *)"zz", 2);
    anillo_vaciar(&a);
    CHECK(anillo_ocupado(&a) == 0);
    printf(fallos ? "test_anillo: %d fallos\n" : "test_anillo: OK\n", fallos);
    return fallos ? 1 : 0;
}
```
Makefile: añadir `test_anillo: test_anillo.c ../firmware/components/reloj_voz/anillo.c` con `-I../firmware/components/reloj_voz/include` y sumarlo a `test`.

- [ ] **Step 2: `anillo.h` / `anillo.c`**

```c
#pragma once
#include <stddef.h>
#include <stdint.h>
typedef struct {
    uint8_t *mem;
    size_t cap;
    volatile size_t escritura;   // índice del productor
    volatile size_t lectura;     // índice del consumidor
} anillo_t;
void anillo_init(anillo_t *a, uint8_t *mem, size_t cap);
size_t anillo_ocupado(const anillo_t *a);
size_t anillo_escribir(anillo_t *a, const uint8_t *datos, size_t n);   // devuelve lo que cupo
size_t anillo_leer(anillo_t *a, uint8_t *destino, size_t n);           // devuelve lo que había
void anillo_vaciar(anillo_t *a);
```
```c
#include "anillo.h"
#include <string.h>
void anillo_init(anillo_t *a, uint8_t *mem, size_t cap) { a->mem = mem; a->cap = cap; a->escritura = a->lectura = 0; }
size_t anillo_ocupado(const anillo_t *a) { return (a->escritura + a->cap - a->lectura) % a->cap; }
static size_t libre(const anillo_t *a) { return a->cap - 1 - anillo_ocupado(a); }   // una celda queda de guarda
size_t anillo_escribir(anillo_t *a, const uint8_t *datos, size_t n)
{
    if (n > libre(a)) n = libre(a);
    size_t w = a->escritura;
    for (size_t i = 0; i < n; i++) { a->mem[w] = datos[i]; w = (w + 1) % a->cap; }
    a->escritura = w;
    return n;
}
size_t anillo_leer(anillo_t *a, uint8_t *destino, size_t n)
{
    size_t hay = anillo_ocupado(a);
    if (n > hay) n = hay;
    size_t r = a->lectura;
    for (size_t i = 0; i < n; i++) { destino[i] = a->mem[r]; r = (r + 1) % a->cap; }
    a->lectura = r;
    return n;
}
void anillo_vaciar(anillo_t *a) { a->lectura = a->escritura; }
```
`make test` → `test_anillo: OK`.

- [ ] **Step 3: `voz.h` / `voz.c`**

```c
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
```
```c
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
static TaskHandle_t t_captura, t_reproduccion;
static void (*cb_captura)(const uint8_t *, size_t);
static volatile bool capturando, reproduciendo, fin_audio;
static uint8_t mem_anillo[32 * 1024];      // 1 s de audio mono
static anillo_t anillo;
#define RESERVA (VOZ_TASA * 2 * 200 / 1000)

esp_err_t voz_init(void)
{
    alt = bsp_audio_codec_speaker_init();
    mic = bsp_audio_codec_microphone_init();
    ESP_RETURN_ON_FALSE(alt && mic, ESP_FAIL, TAG, "códec");
    esp_codec_dev_sample_info_t fs = { .bits_per_sample = 16, .channel = 2, .sample_rate = VOZ_TASA };
    ESP_RETURN_ON_FALSE(esp_codec_dev_open(alt, &fs) == 0, ESP_FAIL, TAG, "abrir altavoz");
    ESP_RETURN_ON_FALSE(esp_codec_dev_open(mic, &fs) == 0, ESP_FAIL, TAG, "abrir micrófono");
    esp_codec_dev_set_in_gain(mic, 24.0f);
    esp_codec_dev_set_out_vol(alt, 60);
    anillo_init(&anillo, mem_anillo, sizeof(mem_anillo));
    ESP_LOGI(TAG, "códec listo (16 kHz, estéreo en el bus, mono en la app)");
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
            if (cb_captura) {
                cb_captura((const uint8_t *)mono, sizeof(mono));
            }
        }
    }
    t_captura = NULL;
    vTaskDelete(NULL);
}

esp_err_t voz_capturar_iniciar(void (*cb)(const uint8_t *, size_t))
{
    if (capturando) return ESP_OK;
    cb_captura = cb;
    capturando = true;
    return xTaskCreate(tarea_captura, "voz_cap", 4096, NULL, 5, &t_captura) == pdPASS ? ESP_OK : ESP_FAIL;
}

void voz_capturar_detener(void) { capturando = false; }
bool voz_capturando(void) { return capturando; }

static void tarea_reproduccion(void *p)
{
    static int16_t mono[256];
    static int16_t estereo[256 * 2];
    // Espera la reserva (o el fin si la respuesta es corta) antes de empezar.
    while (reproduciendo && anillo_ocupado(&anillo) < RESERVA && !fin_audio) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    while (reproduciendo) {
        size_t n = anillo_leer(&anillo, (uint8_t *)mono, sizeof(mono));
        if (n == 0) {
            if (fin_audio) break;
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
    t_reproduccion = NULL;
    vTaskDelete(NULL);
}

size_t voz_reproducir_alimentar(const uint8_t *pcm, size_t n)
{
    size_t cupo = anillo_escribir(&anillo, pcm, n);
    if (!reproduciendo) {
        reproduciendo = true;
        fin_audio = false;
        xTaskCreate(tarea_reproduccion, "voz_rep", 4096, NULL, 5, &t_reproduccion);
    }
    return cupo;
}

void voz_reproducir_fin(void) { fin_audio = true; }
void voz_reproducir_parar(void) { anillo_vaciar(&anillo); fin_audio = true; }
bool voz_reproduciendo(void) { return reproduciendo; }
void voz_volumen(int pct) { if (alt) esp_codec_dev_set_out_vol(alt, pct); }
```
CMake: `idf_component_register(SRCS "voz.c" "anillo.c" INCLUDE_DIRS "include" REQUIRES waveshare__esp32_c6_touch_amoled_2_06 espressif__esp_codec_dev)`.

Ojo con `voz_reproducir_alimentar` cuando el anillo se llena (la Pi manda el audio de golpe, más rápido que 1× tiempo
real): el que alimenta (Task 3) debe reintentar con `vTaskDelay(5)` hasta que quepa, sin bloquear la tarea del
WebSocket más de ~300 ms por trozo.

- [ ] **Step 4: Prueba en hardware** (eco): en `main.c`, temporalmente, con PWR largo: capturar 2 s (contar RMS y
loguearlo) y reproducir lo grabado. Verificar: RMS sube al hablar; se oye la propia voz por el altavoz; `latido` con
la RAM. Quitar el eco al terminar la prueba (queda el comando de cancelar).

- [ ] **Step 5: Commit** `git commit -m "reloj_voz: captura y reproducción a 16 kHz con anillo y reserva"`

---

### Task 3: `reloj_andia` — cliente WebSocket del protocolo

**Files:**
- Create: `firmware/components/reloj_andia/CMakeLists.txt`, `idf_component.yml`, `Kconfig`, `include/andia.h`, `andia.c`

**Interfaces:**
- Produces:
```c
typedef enum { ANDIA_EV_RED = 0, ANDIA_EV_ESTADO, ANDIA_EV_PREGUNTA, ANDIA_EV_RESPUESTA, ANDIA_EV_FIN, ANDIA_EV_ERROR,
               ANDIA_EV_AVISO, ANDIA_EV_TEMPORIZADOR } andia_evento_tipo_t;
typedef enum { ANDIA_INACTIVA = 0, ANDIA_CONECTANDO, ANDIA_ESCUCHANDO, ANDIA_ACTIVADA, ANDIA_PENSANDO, ANDIA_HABLANDO } andia_estado_t;
typedef struct {
    andia_evento_tipo_t tipo;
    union {
        bool red_conectada;
        andia_estado_t estado;
        char texto[200];          // pregunta / respuesta / error / aviso (truncado con UTF-8 sano)
        struct { int segundos; } temporizador;
    };
    char origen[16];              // para aviso
} andia_evento_t;
esp_err_t andia_init(QueueHandle_t cola);       // no conecta todavía
esp_err_t andia_conversar(bool gesto);          // abre el WebSocket si hace falta, manda inicio y captura
void andia_cancelar(void);
andia_estado_t andia_estado(void);
```
- Consumes: `voz_*` (Task 2), `red_conectada()` (Task 1).

- [ ] **Step 1: Kconfig y manifiesto**

`Kconfig`: `RELOJ_ANDIA_URL` (string, default `ws://192.168.x.x:8001/ws`), `RELOJ_ANDIA_TOKEN` (string, default "").
`idf_component.yml`: `espressif/esp_websocket_client: "^1.8"`.
CMake: `idf_component_register(SRCS "andia.c" INCLUDE_DIRS "include" REQUIRES esp_websocket_client json reloj_voz reloj_red esp_timer)`.
(`json` = cJSON de ESP-IDF para parsear los mensajes.)

- [ ] **Step 2: `andia.c`** — máquina de estados + WebSocket

Puntos clave (código completo en el commit; aquí lo que no puede faltar):
- `esp_websocket_client_config_t { .uri = CONFIG_RELOJ_ANDIA_URL, .buffer_size = 4096, .reconnect_timeout_ms = 3000, .network_timeout_ms = 5000, .disable_auto_reconnect = false }`; `esp_websocket_register_events(cliente, WEBSOCKET_EVENT_ANY, al_evento_ws, NULL)`.
- Al `WEBSOCKET_EVENT_CONNECTED`: mandar `{"t":"hola","token":…,"reloj":"<MAC>","v":1}` (`esp_websocket_client_send_text`).
- `WEBSOCKET_EVENT_DATA`: `op_code == 0x2` (binario) → `voz_reproducir_alimentar` (reintentando si no cabe; los mensajes de 3200 B pueden llegar fragmentados: `payload_offset`/`payload_len` vs `data_len`); `op_code == 0x1` (texto) → acumular hasta `payload_len` y parsear con cJSON: `t` = `hola` (ok → estado listo), `activada` → estado ACTIVADA, `pensando` → detener captura + estado PENSANDO, `pregunta`/`respuesta` → eventos con texto (en `respuesta`, estado HABLANDO si `voz` es true), `fin` → `voz_reproducir_fin()`, detener captura si seguía, estado INACTIVA, evento FIN con `motivo`; `error` → evento ERROR; `aviso` → evento AVISO (texto + origen); `temporizador` → evento TEMPORIZADOR; `pong` → nada.
- `andia_conversar(gesto)`: si `!red_conectada()` → evento ERROR "Sin WiFi"; si el cliente no está conectado → `esp_websocket_client_start` y esperar hasta 5 s (`esp_websocket_client_is_connected`) → si no, ERROR "Sin conexión con ANDIA"; mandar `{"t":"inicio","modo":"gesto"|"boton"}`; `voz_capturar_iniciar(al_capturar)` con `al_capturar` = `esp_websocket_client_send_bin(cliente, pcm, n, pdMS_TO_TICKS(100))`; estado ESCUCHANDO (o ACTIVADA directo en modo botón, que llega como mensaje).
- `andia_cancelar()`: detener captura, `voz_reproducir_parar()`, mandar `{"t":"cancelar"}`, estado INACTIVA.
- Vigilante: si en ESCUCHANDO/PENSANDO pasan 25 s sin `fin` → cancelar con ERROR "ANDIA no respondió" (esp_timer).
- Mantener el WebSocket abierto entre conversaciones (para recibir `aviso`) y `ping` cada 30 s; si WiFi se cae, el cliente reconecta solo cuando `red_conectada()` vuelva (llamar `esp_websocket_client_stop/start` desde el evento de red).

- [ ] **Step 3: Compilar; prueba con el servidor real**: desde `main.c` (Task 4 incluye la UI, pero para esta prueba basta
PWR largo → `andia_conversar(false)`): decir "¿qué hora es?" → en consola los eventos `pregunta`, `respuesta` y
se oye a Mayuri. En el journal de la Pi: `WebSocket /ws [accepted]`. Anotar RAM libre durante la conversación.

- [ ] **Step 4: Commit** `git commit -m "reloj_andia: cliente WebSocket del protocolo (conversación, avisos, temporizador)"`

---

### Task 4: UI de ANDIA e integración en la app

**Files:**
- Modify: `firmware/components/reloj_ui/ui.c`, `include/ui.h`, `firmware/main/main.c`, `firmware/main/Kconfig.projbuild`

**Interfaces:**
- Produces en `ui.h`: `void ui_red(bool conectada, const char *ip)`; `void ui_andia_estado(andia_estado_t e)`; `void ui_andia_pregunta(const char *t)`; `void ui_andia_respuesta(const char *t)`; `void ui_andia_error(const char *t)`; `void ui_ir_andia(void)`; `void ui_al_microfono(ui_cb_t cb)` (botón de micrófono de la carátula y de la pantalla ANDIA); `void ui_al_cancelar_andia(ui_cb_t cb)`.

- [ ] **Step 1: Pantalla ANDIA** (`scr_andia`): cabecera "ANDIA" con estado ("Escuchando…", "Te escucho", "Pensando…",
"Hablando", "Lista"), un indicador animado simple (círculo que late con `lv_anim` mientras escucha), etiqueta
`pregunta` (gris, F16) y `respuesta` (blanca, F20, wrap, en caja desplazable), botón grande abajo: "Cancelar" mientras
hay conversación / "Hablar" cuando está inactiva. Gesto RIGHT o botón "‹ Reloj" vuelve. Icono WiFi en la barra de
iconos de la carátula (`LV_SYMBOL_WIFI`, azul si conectada) y botón redondo de micrófono (`LV_SYMBOL_AUDIO`, 64 px)
bajo la fecha.

- [ ] **Step 2: `main.c`**: cola `cola_andia` (16 × `andia_evento_t`); `red_init(cola_andia)`, `voz_init()`,
`andia_init(cola_andia)`; callbacks de UI: micrófono → `encender_pantalla(); ui_ir_andia(); andia_conversar(false)`;
cancelar → `andia_cancelar()`; PWR largo → igual que el micrófono. Bucle: `xQueueReceive(cola_andia, …, 0)` junto a
la cola BLE (esperar en la BLE con 250 ms y vaciar la de ANDIA sin bloquear). Manejo: `RED` → `ui_red`; `ESTADO` →
`ui_andia_estado` (+ encender pantalla y mantenerla mientras HABLANDO: `t_encendido_ms = ahora`); `PREGUNTA`/`RESPUESTA`
→ etiquetas; `FIN`/`ERROR` → estado inactiva (y el error como texto en la respuesta); `AVISO` → `notif_store_agregar`
con `uid` local (contador desde 0xA0000000, categoría OTRA) + `notif_store_poner_detalle` (app_id "cl.reloj.andia",
título = origen "ANDIA"/"Recordatorio", mensaje = texto) → `ui_notifs_refrescar` + `ui_aviso(uid)` + encender;
`TEMPORIZADOR` → `esp_timer` one-shot de N s → al vencer, aviso local "Se acabó el tiempo" + 3 pitidos cortos por el
altavoz (tono generado con `voz_reproducir_alimentar` + `voz_reproducir_fin`) + pantalla encendida.
- `apps.c`: añadir `{ "cl.reloj.andia", "ANDIA" }` a la tabla.

- [ ] **Step 3: Compilar, flashear, medir**: `latido` con RAM tras WiFi conectado y durante una conversación. Objetivo
≥ 60 KB libres en el peor momento; si no, bajar `CONFIG_BSP_DISPLAY_LVGL_BUF_HEIGHT` a 30 y el anillo a 24 KB.

- [ ] **Step 4: Commit** `git commit -m "UI ANDIA, botón de micrófono, avisos y temporizadores del asistente"`

---

### Task 5: Verificación de punta a punta y cierre

- [ ] Botón de micrófono → "¿qué hora es?" → pantalla ANDIA muestra la pregunta transcrita y la respuesta; Mayuri se
oye de corrido por el altavoz (sin cortes).
- [ ] "¿Qué clima hay mañana en Ñuñoa?" → respuesta con datos; "Ponme un temporizador de 20 segundos" → a los 20 s
pitidos + tarjeta; "Recuérdame en 1 minuto tomar agua" → al minuto `aviso` como notificación con app "ANDIA".
- [ ] Desde WSL `andia "hola reloj"` → tarjeta en el reloj.
- [ ] Cancelar a mitad de respuesta (botón) → deja de hablar.
- [ ] Apagar Fish (`servidor.sh --parar`) → respuesta solo en texto.
- [ ] WiFi caída (apagar el router del hotspot o cambiar de red) → icono gris, "Sin WiFi" al pulsar; al volver, reconecta.
- [ ] BLE sigue vinculado tras 5 conversaciones (`BLE 2` en el latido; una notificación de WhatsApp llega).
- [ ] RAM anotada en `docs/ram.md`; `docs/plan.md` y memoria actualizados; commit final.

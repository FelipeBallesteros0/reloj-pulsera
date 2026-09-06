#include "red.h"
#include <stdio.h>
#include <string.h>
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "andia.h"
#include "sdkconfig.h"

static const char *TAG = "red";
static void aplicar_ahorro(void);
static QueueHandle_t cola;
static bool conectada;
static char ip[16] = "--";
static char ssid_actual[33] = "--";
static int red_idx, fallos;
static esp_netif_t *netif;
static bool deseada;   // el WiFi debe estar encendido (lo decide el gestor de energía)

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
    snprintf((char *)cfg.sta.ssid, sizeof(cfg.sta.ssid), "%s", REDES[idx].ssid);
    snprintf((char *)cfg.sta.password, sizeof(cfg.sta.password), "%s", REDES[idx].clave);
    cfg.sta.threshold.authmode = strlen(REDES[idx].clave) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    cfg.sta.listen_interval = 3;   // en ahorro de energía despierta cada 3 beacons
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    snprintf(ssid_actual, sizeof(ssid_actual), "%s", REDES[idx].ssid);
    ESP_LOGI(TAG, "conectando a \"%s\"", REDES[idx].ssid);
    esp_wifi_connect();
}

static void siguiente_red(void)
{
    for (unsigned i = 0; i < N_REDES; i++) {   // salta las redes sin SSID configurado
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
        if (!deseada) {
            return;   // apagado nuestro: sin reintentos
        }
        fallos++;
        if (fallos >= 3) {
            fallos = 0;
            siguiente_red();
        }
        vTaskDelay(pdMS_TO_TICKS(1000 + 1000 * (fallos % 5)));
        conectar_a(red_idx);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = datos;
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&e->ip_info.ip));
        fallos = 0;
        ESP_LOGI(TAG, "conectado a \"%s\", IP %s", ssid_actual, ip);
        aplicar_ahorro();
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
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, al_evento, NULL), TAG, "ev wifi");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, al_evento, NULL), TAG, "ev ip");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "storage");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "mode");
    if (!REDES[0].ssid[0]) {
        siguiente_red();
    }
    return ESP_OK;   // no arranca: lo decide el gestor de energía (red_encender)
}

esp_err_t red_encender(void)
{
    if (deseada) {
        return ESP_OK;
    }
    deseada = true;
    fallos = 0;
    ESP_LOGI(TAG, "WiFi encendido");
    return esp_wifi_start();   // STA_START → conectar_a()
}

esp_err_t red_apagar(void)
{
    if (!deseada) {
        return ESP_OK;
    }
    deseada = false;
    ESP_LOGI(TAG, "WiFi apagado");
    esp_err_t r = esp_wifi_stop();
    if (conectada) {           // por si el evento de desconexión no llega (ya estaba caído)
        strcpy(ip, "--");
        publicar(false);
    }
    return r;
}

bool red_encendida(void) { return deseada; }
bool red_conectada(void) { return conectada; }
const char *red_ip(void) { return ip; }
const char *red_ssid(void) { return ssid_actual; }
bool red_en_casa(void) { return red_conectada() && strcmp(ssid_actual, CONFIG_RELOJ_WIFI1_SSID) == 0; }

static bool ahorro_permitido = true, ahorro_pedido = true;

static void aplicar_ahorro(void)
{
    esp_wifi_set_ps(ahorro_permitido && ahorro_pedido ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE);
}

void red_ahorro(bool activo)
{
    ahorro_pedido = activo;
    aplicar_ahorro();
}

void red_permitir_ahorro(bool permitir)
{
    if (ahorro_permitido != permitir) {
        ESP_LOGI(TAG, "ahorro de energía del WiFi %s", permitir ? "permitido (a batería)" : "desactivado (con USB)");
    }
    ahorro_permitido = permitir;
    aplicar_ahorro();
}

int red_rssi(void)
{
    wifi_ap_record_t ap;
    return (red_conectada() && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) ? ap.rssi : 0;
}

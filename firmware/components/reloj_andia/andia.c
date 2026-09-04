#include "andia.h"
#include <stdio.h>
#include <string.h>
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "esp_websocket_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "voz.h"
#include "red.h"
#include "sdkconfig.h"

static const char *TAG = "andia";
static QueueHandle_t cola;
static esp_websocket_client_handle_t ws;
static volatile andia_estado_t estado = ANDIA_INACTIVA;
static volatile bool saludado;            // el servidor aceptó el "hola"
static char acum[1024];                   // mensajes de texto que llegan fragmentados
static size_t acum_len;
static esp_timer_handle_t timer_vigilante, timer_ping;
static char id_reloj[13];

// ---------------------------------------------------------------- utilidades

static void publicar(andia_evento_t *ev)
{
    if (cola) {
        xQueueSend(cola, ev, 0);
    }
}

static void poner_estado(andia_estado_t e)
{
    if (estado == e) {
        return;
    }
    estado = e;
    andia_evento_t ev = { .tipo = ANDIA_EV_ESTADO, .estado = e };
    publicar(&ev);
}

static void publicar_texto(andia_evento_tipo_t tipo, const char *texto, const char *origen)
{
    andia_evento_t ev = { .tipo = tipo };
    if (texto) {
        // Truncar sin partir un carácter UTF-8.
        size_t n = strlen(texto);
        if (n > sizeof(ev.texto) - 1) {
            n = sizeof(ev.texto) - 1;
            while (n > 0 && ((unsigned char)texto[n] & 0xC0) == 0x80) {
                n--;
            }
        }
        memcpy(ev.texto, texto, n);
        ev.texto[n] = '\0';
    }
    if (origen) {
        snprintf(ev.origen, sizeof(ev.origen), "%s", origen);
    }
    publicar(&ev);
}

static void enviar_json(const char *json)
{
    if (ws && esp_websocket_client_is_connected(ws)) {
        esp_websocket_client_send_text(ws, json, strlen(json), pdMS_TO_TICKS(1000));
    }
}

static void terminar_conversacion(void)
{
    voz_capturar_detener();
    esp_timer_stop(timer_vigilante);
    poner_estado(ANDIA_INACTIVA);
}

// ---------------------------------------------------------------- mensajes del servidor

static void procesar_mensaje(const char *json)
{
    cJSON *m = cJSON_Parse(json);
    if (!m) {
        ESP_LOGW(TAG, "JSON inválido: %.60s", json);
        return;
    }
    const cJSON *t = cJSON_GetObjectItem(m, "t");
    const char *tipo = cJSON_IsString(t) ? t->valuestring : "";
    const cJSON *texto = cJSON_GetObjectItem(m, "texto");
    const char *txt = cJSON_IsString(texto) ? texto->valuestring : "";

    if (strcmp(tipo, "hola") == 0) {
        saludado = true;
        ESP_LOGI(TAG, "servidor listo");
    } else if (strcmp(tipo, "activada") == 0) {
        poner_estado(ANDIA_ACTIVADA);
    } else if (strcmp(tipo, "pensando") == 0) {
        voz_capturar_detener();
        poner_estado(ANDIA_PENSANDO);
    } else if (strcmp(tipo, "pregunta") == 0) {
        publicar_texto(ANDIA_EV_PREGUNTA, txt, NULL);
    } else if (strcmp(tipo, "respuesta") == 0) {
        const cJSON *voz = cJSON_GetObjectItem(m, "voz");
        publicar_texto(ANDIA_EV_RESPUESTA, txt, NULL);
        poner_estado(cJSON_IsTrue(voz) ? ANDIA_HABLANDO : ANDIA_PENSANDO);
    } else if (strcmp(tipo, "temporizador") == 0) {
        const cJSON *seg = cJSON_GetObjectItem(m, "segundos");
        andia_evento_t ev = { .tipo = ANDIA_EV_TEMPORIZADOR, .segundos = cJSON_IsNumber(seg) ? seg->valueint : 0 };
        publicar(&ev);
    } else if (strcmp(tipo, "fin") == 0) {
        const cJSON *motivo = cJSON_GetObjectItem(m, "motivo");
        voz_reproducir_fin();
        voz_capturar_detener();
        esp_timer_stop(timer_vigilante);
        publicar_texto(ANDIA_EV_FIN, cJSON_IsString(motivo) ? motivo->valuestring : "", NULL);
        poner_estado(ANDIA_INACTIVA);
    } else if (strcmp(tipo, "error") == 0) {
        terminar_conversacion();
        publicar_texto(ANDIA_EV_ERROR, txt, NULL);
    } else if (strcmp(tipo, "aviso") == 0) {
        const cJSON *origen = cJSON_GetObjectItem(m, "origen");
        publicar_texto(ANDIA_EV_AVISO, txt, cJSON_IsString(origen) ? origen->valuestring : "");
    }
    cJSON_Delete(m);
}

static void alimentar_audio(const uint8_t *datos, size_t n)
{
    // La Pi manda el audio más rápido que el tiempo real: si el anillo está lleno, esperar un poco.
    for (int intento = 0; n > 0 && intento < 60; intento++) {
        size_t cupo = voz_reproducir_alimentar(datos, n);
        datos += cupo;
        n -= cupo;
        if (n > 0) {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
}

static void al_evento_ws(void *arg, esp_event_base_t base, int32_t id, void *datos)
{
    esp_websocket_event_data_t *e = datos;
    switch (id) {
    case WEBSOCKET_EVENT_CONNECTED: {
        char hola[160];
        snprintf(hola, sizeof(hola), "{\"t\":\"hola\",\"token\":\"%s\",\"reloj\":\"%s\",\"v\":1}",
                 CONFIG_RELOJ_ANDIA_TOKEN, id_reloj);
        esp_websocket_client_send_text(ws, hola, strlen(hola), pdMS_TO_TICKS(1000));
        ESP_LOGI(TAG, "WebSocket conectado");
        break;
    }
    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_CLOSED:
        saludado = false;
        if (estado != ANDIA_INACTIVA) {
            terminar_conversacion();
            publicar_texto(ANDIA_EV_ERROR, "Se cortó la conexión con ANDIA", NULL);
        }
        ESP_LOGW(TAG, "WebSocket desconectado");
        break;
    case WEBSOCKET_EVENT_DATA:
        if (e->op_code == 0x2) {                       // binario: audio PCM (puede venir fragmentado)
            alimentar_audio((const uint8_t *)e->data_ptr, e->data_len);
        } else if (e->op_code == 0x1) {                // texto: JSON, quizá en varios fragmentos
            if (e->payload_offset == 0) {
                acum_len = 0;
            }
            size_t cabe = sizeof(acum) - 1 - acum_len;
            size_t n = e->data_len < cabe ? e->data_len : cabe;
            memcpy(acum + acum_len, e->data_ptr, n);
            acum_len += n;
            if (e->payload_offset + e->data_len >= e->payload_len) {
                acum[acum_len] = '\0';
                procesar_mensaje(acum);
            }
        }
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGW(TAG, "error de WebSocket");
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------- captura → servidor

static void al_capturar(const uint8_t *pcm, size_t n)
{
    if (ws && esp_websocket_client_is_connected(ws)) {
        esp_websocket_client_send_bin(ws, (const char *)pcm, n, pdMS_TO_TICKS(200));
    }
}

static void al_vencer_vigilante(void *arg)
{
    if (estado != ANDIA_INACTIVA && estado != ANDIA_HABLANDO) {
        ESP_LOGW(TAG, "ANDIA no respondió a tiempo");
        enviar_json("{\"t\":\"cancelar\"}");
        terminar_conversacion();
        publicar_texto(ANDIA_EV_ERROR, "ANDIA no respondió", NULL);
    }
}

static void al_ping(void *arg)
{
    if (saludado) {
        enviar_json("{\"t\":\"ping\"}");
    }
}

// ---------------------------------------------------------------- API

esp_err_t andia_init(QueueHandle_t q)
{
    cola = q;
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BT);
    snprintf(id_reloj, sizeof(id_reloj), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    esp_websocket_client_config_t cfg = {
        .uri = CONFIG_RELOJ_ANDIA_URL,
        .buffer_size = 1536,      // los mensajes grandes (audio de 3200 B) llegan fragmentados y se manejan
        .reconnect_timeout_ms = 5000,
        .network_timeout_ms = 8000,
        .task_stack = 5120,       // con TLS (wss://) la tarea necesita algo más de pila
        .ping_interval_sec = 0,
        .disable_auto_reconnect = false,
        .crt_bundle_attach = esp_crt_bundle_attach,   // raíces de confianza para wss:// (Cloudflare usa GTS)
    };
    ws = esp_websocket_client_init(&cfg);
    ESP_RETURN_ON_FALSE(ws, ESP_FAIL, TAG, "no se pudo crear el cliente");
    ESP_RETURN_ON_ERROR(esp_websocket_register_events(ws, WEBSOCKET_EVENT_ANY, al_evento_ws, NULL), TAG, "eventos");
    const esp_timer_create_args_t a1 = { .callback = al_vencer_vigilante, .name = "andia_vig" };
    const esp_timer_create_args_t a2 = { .callback = al_ping, .name = "andia_ping" };
    esp_timer_create(&a1, &timer_vigilante);
    esp_timer_create(&a2, &timer_ping);
    esp_timer_start_periodic(timer_ping, 30 * 1000000ULL);
    return ESP_OK;
}

static bool asegurar_conexion(void)
{
    if (!red_conectada()) {
        publicar_texto(ANDIA_EV_ERROR, "Sin WiFi", NULL);
        return false;
    }
    if (!esp_websocket_client_is_connected(ws)) {
        poner_estado(ANDIA_CONECTANDO);
        if (esp_websocket_client_start(ws) != ESP_OK && !esp_websocket_client_is_connected(ws)) {
            // ya estaba arrancado pero caído: reintento
            esp_websocket_client_stop(ws);
            esp_websocket_client_start(ws);
        }
    }
    for (int i = 0; i < 60 && !saludado; i++) {   // hasta 6 s
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (!saludado) {
        poner_estado(ANDIA_INACTIVA);
        publicar_texto(ANDIA_EV_ERROR, "Sin conexión con ANDIA", NULL);
        return false;
    }
    return true;
}

esp_err_t andia_conversar(bool gesto)
{
    if (estado != ANDIA_INACTIVA && estado != ANDIA_HABLANDO) {
        return ESP_ERR_INVALID_STATE;
    }
    voz_reproducir_parar();
    if (!asegurar_conexion()) {
        return ESP_FAIL;
    }
    enviar_json(gesto ? "{\"t\":\"inicio\",\"modo\":\"gesto\"}" : "{\"t\":\"inicio\",\"modo\":\"boton\"}");
    poner_estado(ANDIA_ESCUCHANDO);
    esp_timer_stop(timer_vigilante);
    esp_timer_start_once(timer_vigilante, 25 * 1000000ULL);
    return voz_capturar_iniciar(al_capturar);
}

void andia_cancelar(void)
{
    if (estado == ANDIA_INACTIVA) {
        return;
    }
    enviar_json("{\"t\":\"cancelar\"}");
    voz_reproducir_parar();
    terminar_conversacion();
}

andia_estado_t andia_estado(void) { return estado; }
bool andia_conectada(void) { return saludado && ws && esp_websocket_client_is_connected(ws); }

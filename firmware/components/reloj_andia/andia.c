#include "andia.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include <math.h>
#include "esp_websocket_client.h"
#include "esp_transport_ssl.h"
#include "esp_transport_tcp.h"
#include "esp_transport_ws.h"
#include "esp_crt_bundle.h"
#include "lwip/sockets.h"
#include "cJSON.h"
#include "voz.h"
#include "adpcm.h"
#include "red.h"
#include "sdkconfig.h"

static const char *TAG = "andia";
static QueueHandle_t cola;
static esp_websocket_client_handle_t ws;
static esp_transport_handle_t transporte, transporte_base;   // ws sobre TLS o TCP; propios para llegar al socket
static bool cliente_lan;                    // el cliente actual va por la LAN de casa (ws://) o por el túnel (wss://)
static volatile andia_estado_t estado = ANDIA_INACTIVA;
static volatile bool saludado;            // el servidor aceptó el "hola"
static char acum[1024];                   // mensajes de texto que llegan fragmentados
static size_t acum_len;
static esp_timer_handle_t timer_vigilante, timer_ping;
static char id_reloj[13];
static bool con_adpcm;                    // subida comprimida 4:1 (ADPCM) aceptada por el servidor
static bool bajada_adpcm;                 // la voz de vuelta viene en ADPCM (túnel) o en PCM (LAN, sin siseo)
static adpcm_estado_t codificador, decodificador;
static struct { int tramas, mensajes, fallidas, descartadas; int64_t total_us, max_us; } envio;   // subida de audio

// La captura no debe esperar a la red (perdería audio del DMA): encola bloques ADPCM y una tarea aparte los manda,
// juntando los que haya pendientes en un solo mensaje. En vigilia la cola guarda los últimos 640 ms sin mandarlos:
// cuando salta el detector, ese audio previo se manda primero y el "Oye" no se pierde.
#define BLOQUE_BYTES   (ADPCM_CABECERA + VOZ_TRAMA_BYTES / 4)   // 260 B = 32 ms
#define COLA_BLOQUES   20
#define BLOQUES_POR_MENSAJE 3      // 780 B: cabe en el búfer del cliente y en un registro TLS de 1024
static uint8_t cola_audio[COLA_BLOQUES][BLOQUE_BYTES];
static volatile uint8_t cola_in, cola_out;
static portMUX_TYPE cola_mux = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t tarea_envio;
static volatile bool enviando;            // hay una conversación abierta: lo capturado se manda a la Pi
static volatile bool vigilia;             // ajuste del usuario
static volatile bool en_gesto;            // conversación abierta por el detector, aún sin activación confirmada
static int64_t vigilia_pausa_hasta_us;    // enfriamiento tras un fin sin activación / sin voz
static int fallos_seguidos;               // "sin_activacion" seguidos: con 3, pausa larga
static esp_timer_handle_t timer_sigue;

static void al_capturar(const uint8_t *pcm, size_t n);

static void vaciar_cola(void)
{
    taskENTER_CRITICAL(&cola_mux);
    cola_out = cola_in;
    taskEXIT_CRITICAL(&cola_mux);
}

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
    static const char *N[] = { "inactiva", "conectando", "escuchando", "activada", "pensando", "hablando" };
    ESP_LOGI(TAG, "estado: %s → %s", N[estado], N[e]);
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

// Plazo sin noticias de la Pi: se reinicia con cada mensaje suyo (STT + LLM + TTS pueden sumar 15 s).
static void armar_vigilante_s(int segundos)
{
    esp_timer_stop(timer_vigilante);
    esp_timer_start_once(timer_vigilante, (uint64_t)segundos * 1000000ULL);
}

static void armar_vigilante(void) { armar_vigilante_s(30); }

static void informar_envio(void)
{
    if (envio.tramas || envio.descartadas) {
        ESP_LOGI(TAG, "audio: %d tramas en %d mensajes, envío medio %d ms, máx %d ms, descartadas %d, fallidas %d",
                 envio.tramas, envio.mensajes, envio.mensajes ? (int)(envio.total_us / envio.mensajes / 1000) : 0,
                 (int)(envio.max_us / 1000), envio.descartadas, envio.fallidas);
        memset(&envio, 0, sizeof(envio));
    }
}

// Cierra la subida de audio; en vigilia el micrófono sigue capturando (solo para el detector).
static void detener_envio(void)
{
    enviando = false;
    en_gesto = false;
    esp_timer_stop(timer_sigue);
    vaciar_cola();
    if (!vigilia) {
        voz_capturar_detener();
    }
}

static void terminar_conversacion(void)
{
    red_ahorro(true);
    informar_envio();
    detener_envio();
    esp_timer_stop(timer_vigilante);
    poner_estado(ANDIA_INACTIVA);
}

static void pausar_vigilia_ms(int ms) { vigilia_pausa_hasta_us = esp_timer_get_time() + (int64_t)ms * 1000; }

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
        const cJSON *audio = cJSON_GetObjectItem(m, "audio");
        const cJSON *bajada = cJSON_GetObjectItem(m, "bajada");
        con_adpcm = cJSON_IsString(audio) && strcmp(audio->valuestring, "adpcm") == 0;
        bajada_adpcm = cJSON_IsString(bajada) ? strcmp(bajada->valuestring, "adpcm") == 0 : con_adpcm;
        saludado = true;
        ESP_LOGI(TAG, "servidor listo (subida %s, bajada %s)", con_adpcm ? "adpcm" : "pcm", bajada_adpcm ? "adpcm" : "pcm");
    } else if (strcmp(tipo, "activada") == 0) {
        armar_vigilante();
        fallos_seguidos = 0;
        if (en_gesto) {
            ESP_LOGI(TAG, "vigilia: la Pi confirmó \"Oye Andia\"");
            en_gesto = false;
            red_ahorro(false);
        }
        poner_estado(ANDIA_ACTIVADA);
    } else if (strcmp(tipo, "pensando") == 0) {
        armar_vigilante_s(45);   // STT + modelo + Fish (sin streaming) pueden sumar más de 30 s
        enviando = false;
        if (!vigilia) {
            voz_capturar_detener();
        }
        informar_envio();
        if (!en_gesto) {   // en vigilia aún no se sabe si era para mí: sin UI hasta que la Pi lo confirme
            poner_estado(ANDIA_PENSANDO);
        }
    } else if (strcmp(tipo, "pregunta") == 0) {
        armar_vigilante();
        publicar_texto(ANDIA_EV_PREGUNTA, txt, NULL);
    } else if (strcmp(tipo, "respuesta") == 0) {
        const cJSON *voz = cJSON_GetObjectItem(m, "voz");
        armar_vigilante();   // también en "hablando": si el audio o el fin no llegan, no quedarse colgado
        publicar_texto(ANDIA_EV_RESPUESTA, txt, NULL);
        poner_estado(cJSON_IsTrue(voz) ? ANDIA_HABLANDO : ANDIA_PENSANDO);
    } else if (strcmp(tipo, "temporizador") == 0) {
        const cJSON *seg = cJSON_GetObjectItem(m, "segundos");
        andia_evento_t ev = { .tipo = ANDIA_EV_TEMPORIZADOR, .segundos = cJSON_IsNumber(seg) ? seg->valueint : 0 };
        publicar(&ev);
    } else if (strcmp(tipo, "sigue") == 0) {
        // Dijiste solo el nombre: pitido, "te escucho" y volver a mandar audio (tras el pitido, sin audio viejo).
        bool era_gesto = en_gesto;
        en_gesto = false;
        armar_vigilante();
        red_ahorro(false);
        vaciar_cola();
        enviando = false;
        poner_estado(ANDIA_ACTIVADA);
        andia_evento_t ev = { .tipo = ANDIA_EV_SIGUE };
        publicar(&ev);
        if (!voz_capturando()) {
            voz_capturar_iniciar(al_capturar);
        }
        esp_timer_stop(timer_sigue);
        esp_timer_start_once(timer_sigue, 350 * 1000);
        ESP_LOGI(TAG, "sigue escuchando (%s)", era_gesto ? "vigilia" : "botón");
    } else if (strcmp(tipo, "fin") == 0) {
        const cJSON *motivo = cJSON_GetObjectItem(m, "motivo");
        const char *mot = cJSON_IsString(motivo) ? motivo->valuestring : "";
        bool era_gesto = en_gesto;
        voz_reproducir_fin();
        informar_envio();
        detener_envio();
        esp_timer_stop(timer_vigilante);
        red_ahorro(true);
        if (strcmp(mot, "sin_activacion") == 0) {
            fallos_seguidos++;
            pausar_vigilia_ms(fallos_seguidos >= 3 ? 20000 : 2000);
            ESP_LOGI(TAG, "vigilia: no era para mí (%d seguidos)", fallos_seguidos);
        } else if (strcmp(mot, "sin_voz") == 0) {
            pausar_vigilia_ms(2000);
        } else {
            pausar_vigilia_ms(1500);   // que la cola del propio audio de respuesta no dispare el detector
        }
        if (!era_gesto) {
            publicar_texto(ANDIA_EV_FIN, mot, NULL);
        }
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
    for (int intento = 0; n > 0 && intento < 200; intento++) {   // hasta 1 s: el anillo es de 375 ms
        size_t cupo = voz_reproducir_alimentar(datos, n);
        datos += cupo;
        n -= cupo;
        if (n > 0) {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
    if (n > 0) {
        ESP_LOGW(TAG, "audio de respuesta descartado: %u B (anillo lleno 1 s)", (unsigned)n);
    }
}

// Un mensaje binario = un bloque ADPCM (cabecera + nibbles). Puede llegar en varios fragmentos: la cabecera
// viene en el primero y el estado del decodificador continúa en los siguientes.
static void recibir_audio(const uint8_t *d, size_t n, size_t offset)
{
    if (offset == 0 && estado == ANDIA_HABLANDO) {
        esp_timer_stop(timer_vigilante);
        esp_timer_start_once(timer_vigilante, 15 * 1000000ULL);   // cada bloque que llega renueva el plazo
    }
    if (!bajada_adpcm) {
        alimentar_audio(d, n);
        return;
    }
    if (offset == 0) {
        if (n < ADPCM_CABECERA) {
            return;
        }
        adpcm_leer_cabecera(&decodificador, d);
        d += ADPCM_CABECERA;
        n -= ADPCM_CABECERA;
    }
    int16_t pcm[128];
    while (n > 0) {
        size_t trozo = n < sizeof(pcm) / 4 ? n : sizeof(pcm) / 4;
        size_t m = adpcm_decodificar(&decodificador, d, trozo, pcm);
        alimentar_audio((const uint8_t *)pcm, m * sizeof(int16_t));
        d += trozo;
        n -= trozo;
    }
}

static void al_evento_ws(void *arg, esp_event_base_t base, int32_t id, void *datos)
{
    esp_websocket_event_data_t *e = datos;
    switch (id) {
    case WEBSOCKET_EVENT_CONNECTED: {
        // Sin Nagle: el cliente escribe cabecera y carga por separado y, con Nagle, la carga esperaba el ACK de la
        // cabecera (un viaje completo por trama de audio: 12 s para subir 2 s de voz por el hotspot).
        int fd = esp_transport_get_socket(transporte);
        int uno = 1;
        if (fd < 0 || setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &uno, sizeof(uno)) != 0) {
            ESP_LOGW(TAG, "no pude quitar Nagle al socket %d", fd);
        }
        char hola[160];
        // Subida siempre comprimida; la bajada en PCM cuando vamos por la LAN (sobra ancho de banda, sin siseo).
        snprintf(hola, sizeof(hola), "{\"t\":\"hola\",\"token\":\"%s\",\"reloj\":\"%s\",\"v\":1,\"audio\":\"adpcm\",\"bajada\":\"%s\"}",
                 CONFIG_RELOJ_ANDIA_TOKEN, id_reloj, cliente_lan ? "pcm" : "adpcm");
        esp_websocket_client_send_text(ws, hola, strlen(hola), pdMS_TO_TICKS(1000));
        ESP_LOGI(TAG, "WebSocket conectado");
        break;
    }
    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_CLOSED:
        saludado = false;
        if (estado != ANDIA_INACTIVA || en_gesto) {
            bool era_gesto = en_gesto;
            terminar_conversacion();
            if (!era_gesto) {
                publicar_texto(ANDIA_EV_ERROR, "Se cortó la conexión con ANDIA", NULL);
            }
        }
        ESP_LOGW(TAG, "WebSocket desconectado");
        break;
    case WEBSOCKET_EVENT_DATA:
        if (e->op_code == 0x2) {                       // binario: audio (puede venir fragmentado)
            recibir_audio((const uint8_t *)e->data_ptr, e->data_len, e->payload_offset);
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

static void enviar_audio(const uint8_t *datos, size_t n, int tramas)
{
    if (!ws || !esp_websocket_client_is_connected(ws)) {
        envio.fallidas += tramas;
        return;
    }
    // Un corte de red más largo que este plazo aborta la conexión (así lo hace el cliente).
    int64_t t0 = esp_timer_get_time();
    int r = esp_websocket_client_send_bin(ws, (const char *)datos, n, pdMS_TO_TICKS(3000));
    int64_t dt = esp_timer_get_time() - t0;
    envio.total_us += dt;
    if (dt > envio.max_us) {
        envio.max_us = dt;
    }
    envio.mensajes++;
    envio.tramas += tramas;
    if (r < 0) {
        envio.fallidas += tramas;
        ESP_LOGW(TAG, "audio no enviado");
    }
}

static void tarea_enviar(void *arg)
{
    static uint8_t mensaje[BLOQUES_POR_MENSAJE * BLOQUE_BYTES];
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
        while (enviando && cola_out != cola_in) {
            int k = 0;
            taskENTER_CRITICAL(&cola_mux);
            while (cola_out != cola_in && k < BLOQUES_POR_MENSAJE) {
                memcpy(mensaje + k * BLOQUE_BYTES, cola_audio[cola_out], BLOQUE_BYTES);
                cola_out = (cola_out + 1) % COLA_BLOQUES;
                k++;
            }
            taskEXIT_CRITICAL(&cola_mux);
            enviar_audio(mensaje, k * BLOQUE_BYTES, k);
        }
    }
}

// Detector de voz cercana para la vigilia: RMS de la trama frente a un piso de ruido que se adapta despacio, y
// pocos cruces por cero (las frases de activación son vocálicas: "Oye", "Hola", "Andia"; un chasquido, el teclado
// o un roce son agudos y tienen muchos). Dispara con 6 tramas seguidas (192 ms); la Pi decide si era "Oye Andia".
#define ZC_MAX_VOZ 60      // cruces por cero por trama de 512 muestras (~1900/s); un chasquido pasa de 4000/s
static void detectar_voz(const int16_t *pcm, size_t muestras)
{
    static float piso = 300;
    static int seguidas;
    int64_t suma = 0;
    int cruces = 0;
    for (size_t i = 0; i < muestras; i++) {
        suma += (int32_t)pcm[i] * pcm[i];
        if (i && ((pcm[i] < 0) != (pcm[i - 1] < 0))) {
            cruces++;
        }
    }
    float rms = sqrtf((float)(suma / (int64_t)muestras));
    bool fuerte = rms > 600 && rms > 3 * piso;
    if (!fuerte) {
        piso += (rms - piso) * 0.02f;          // ~1,5 s de constante de tiempo
        if (piso < 100) piso = 100;
        if (piso > 3000) piso = 3000;
        seguidas = 0;
        return;
    }
    if (cruces > ZC_MAX_VOZ) {                 // fuerte pero agudo: no es una vocal
        seguidas = 0;
        return;
    }
    if (++seguidas < 6) {
        return;
    }
    seguidas = 0;
    if (!vigilia || enviando || estado != ANDIA_INACTIVA || !saludado || voz_reproduciendo() ||
        esp_timer_get_time() < vigilia_pausa_hasta_us) {
        return;
    }
    ESP_LOGI(TAG, "vigilia: voz cerca (RMS %d, piso %d, %d cruces): mando la frase", (int)rms, (int)piso, cruces);
    red_ahorro(false);                         // con ahorro, cada mensaje tardaba ~110 ms y se perdían tramas
    memset(&envio, 0, sizeof(envio));
    en_gesto = true;
    armar_vigilante();
    enviar_json("{\"t\":\"inicio\",\"modo\":\"gesto\"}");
    enviando = true;                           // la cola ya tiene los últimos 640 ms: salen primero
    if (tarea_envio) {
        xTaskNotifyGive(tarea_envio);
    }
}

// Desde la tarea de voz, una trama de 1024 B (32 ms) cada vez: codificar y encolar, sin tocar la red.
static void al_capturar(const uint8_t *pcm, size_t n)
{
    if (n > VOZ_TRAMA_BYTES) {
        n = VOZ_TRAMA_BYTES;
    }
    if (!con_adpcm) {                       // servidor viejo: PCM crudo, directo
        if (enviando) {
            enviar_audio(pcm, n, 1);
        }
        return;
    }
    taskENTER_CRITICAL(&cola_mux);
    uint8_t siguiente = (cola_in + 1) % COLA_BLOQUES;
    if (siguiente == cola_out) {
        cola_out = (cola_out + 1) % COLA_BLOQUES;   // llena: cae la trama más vieja (en vigilia, rota el previo)
        if (enviando) {
            envio.descartadas++;
        }
    }
    taskEXIT_CRITICAL(&cola_mux);
    adpcm_codificar_bloque(&codificador, (const int16_t *)pcm, n / 2, cola_audio[cola_in]);
    cola_in = siguiente;
    if (enviando && tarea_envio) {
        xTaskNotifyGive(tarea_envio);
    }
    if (vigilia && !enviando) {
        detectar_voz((const int16_t *)pcm, n / 2);
    }
}

static void al_vencer_vigilante(void *arg)
{
    if (estado != ANDIA_INACTIVA) {
        ESP_LOGW(TAG, "ANDIA no respondió a tiempo (estado %d)", (int)estado);
        enviar_json("{\"t\":\"cancelar\"}");
        terminar_conversacion();
        publicar_texto(ANDIA_EV_ERROR, "ANDIA no respondió", NULL);
    }
}

// Tras "sigue": pasado el pitido, volver a mandar lo que se capture (la cola se vació antes).
static void al_reanudar_envio(void *arg)
{
    vaciar_cola();
    enviando = true;
}

static void al_ping(void *arg)
{
    if (saludado) {
        enviar_json("{\"t\":\"ping\"}");
    }
}

// ---------------------------------------------------------------- API

// "wss://host/ws" → "/ws" (el cliente solo usa la URL para el host y el puerto; la ruta la lleva el transporte).
static const char *ruta_de_url(const char *url)
{
    const char *p = strstr(url, "://");
    p = p ? strchr(p + 3, '/') : NULL;
    return p ? p : "/";
}

// "ws://host:8001/ws" → 8001; sin puerto explícito, el del esquema.
static int puerto_de_url(const char *url, int defecto)
{
    const char *p = strstr(url, "://");
    if (!p) {
        return defecto;
    }
    p += 3;
    const char *barra = strchr(p, '/');
    const char *dp = strchr(p, ':');
    if (dp && (!barra || dp < barra)) {
        return atoi(dp + 1);
    }
    return defecto;
}

// Cliente WebSocket para la red actual: en casa, ws:// directo a la Pi por la LAN (sin TLS: ~25 KB de RAM menos
// y sin los cortes del túnel); fuera, wss:// por Cloudflare. Transporte propio para llegar al socket (TCP_NODELAY).
static esp_err_t crear_cliente(bool lan)
{
    const char *url = lan ? CONFIG_RELOJ_ANDIA_URL_CASA : CONFIG_RELOJ_ANDIA_URL;
    bool tls = strncmp(url, "wss://", 6) == 0;
    transporte_base = tls ? esp_transport_ssl_init() : esp_transport_tcp_init();
    ESP_RETURN_ON_FALSE(transporte_base, ESP_ERR_NO_MEM, TAG, "transporte base");
    if (tls) {
        esp_transport_ssl_crt_bundle_attach(transporte_base, esp_crt_bundle_attach);   // raíces (Cloudflare usa GTS)
    }
    transporte = esp_transport_ws_init(transporte_base);
    ESP_RETURN_ON_FALSE(transporte, ESP_ERR_NO_MEM, TAG, "transporte WS");
    const esp_transport_ws_config_t cfg_ws = { .ws_path = ruta_de_url(url), .propagate_control_frames = true };
    ESP_RETURN_ON_ERROR(esp_transport_ws_set_config(transporte, &cfg_ws), TAG, "config WS");
    esp_websocket_client_config_t cfg = {
        .uri = url,
        // Con transporte externo el cliente no deduce el puerto del esquema (intentaba host:0).
        .port = puerto_de_url(url, tls ? 443 : 80),
        .ext_transport = transporte,
        .buffer_size = 1024,      // un bloque de bajada (1604 B) llega en dos trozos y se maneja; la subida cabe entera
        .reconnect_timeout_ms = 5000,
        .network_timeout_ms = 8000,
        .task_stack = 5120,       // con TLS (wss://) la tarea necesita algo más de pila
        .ping_interval_sec = 20,      // el hotspot cambia de IP a menudo: detectar la conexión muerta en ~50 s
        .pingpong_timeout_sec = 30,   // con 10 s saltó en plena respuesta (pong tardío por el hotspot)
        .disable_auto_reconnect = false,
    };
    ws = esp_websocket_client_init(&cfg);
    ESP_RETURN_ON_FALSE(ws, ESP_FAIL, TAG, "no se pudo crear el cliente");
    ESP_RETURN_ON_ERROR(esp_websocket_register_events(ws, WEBSOCKET_EVENT_ANY, al_evento_ws, NULL), TAG, "eventos");
    cliente_lan = lan;
    ESP_LOGI(TAG, "cliente ANDIA por %s: %s", lan ? "la LAN de casa" : "el túnel", url);
    return ESP_OK;
}

static void destruir_cliente(void)
{
    if (!ws) {
        return;
    }
    saludado = false;
    esp_websocket_client_destroy(ws);        // para la tarea y cierra si estaba conectado
    ws = NULL;
    esp_transport_destroy(transporte);
    esp_transport_destroy(transporte_base);
    transporte = transporte_base = NULL;
}

// El cliente debe corresponder a la red actual; si cambió (casa ↔ hotspot), se recrea. Solo desde la tarea de la app.
static esp_err_t asegurar_cliente(void)
{
    bool lan = red_en_casa();
    if (ws && cliente_lan == lan) {
        return ESP_OK;
    }
    if (ws) {
        ESP_LOGI(TAG, "cambió la red: recreo el cliente");
        destruir_cliente();
    }
    return crear_cliente(lan);
}

esp_err_t andia_init(QueueHandle_t q)
{
    cola = q;
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BT);
    snprintf(id_reloj, sizeof(id_reloj), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    const esp_timer_create_args_t a1 = { .callback = al_vencer_vigilante, .name = "andia_vig" };
    const esp_timer_create_args_t a2 = { .callback = al_ping, .name = "andia_ping" };
    const esp_timer_create_args_t a3 = { .callback = al_reanudar_envio, .name = "andia_sigue" };
    esp_timer_create(&a1, &timer_vigilante);
    esp_timer_create(&a2, &timer_ping);
    esp_timer_create(&a3, &timer_sigue);
    esp_timer_start_periodic(timer_ping, 30 * 1000000ULL);
    ESP_RETURN_ON_FALSE(xTaskCreate(tarea_enviar, "andia_tx", 3584, NULL, 5, &tarea_envio) == pdPASS, ESP_FAIL, TAG,
                        "tarea de envío");
    return ESP_OK;
}

static bool asegurar_conexion(void)
{
    if (!red_conectada()) {
        publicar_texto(ANDIA_EV_ERROR, "Sin WiFi", NULL);
        return false;
    }
    if (asegurar_cliente() != ESP_OK) {
        publicar_texto(ANDIA_EV_ERROR, "Sin cliente ANDIA", NULL);
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
    for (int i = 0; i < 120 && !saludado; i++) {  // hasta 12 s: por el hotspot el TLS a veces tarda o reintenta
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
    red_ahorro(false);
    if (!asegurar_conexion()) {
        red_ahorro(true);
        return ESP_FAIL;
    }
    enviar_json(gesto ? "{\"t\":\"inicio\",\"modo\":\"gesto\"}" : "{\"t\":\"inicio\",\"modo\":\"boton\"}");
    memset(&envio, 0, sizeof(envio));
    en_gesto = gesto;
    poner_estado(ANDIA_ESCUCHANDO);
    armar_vigilante();
    esp_err_t r = voz_capturar_iniciar(al_capturar);
    if (r != ESP_OK) {
        enviar_json("{\"t\":\"cancelar\"}");
        terminar_conversacion();
        publicar_texto(ANDIA_EV_ERROR, "Micrófono no disponible", NULL);
        return r;
    }
    enviando = true;                        // en vigilia la cola ya trae audio previo: mejor, no se pierde el arranque
    if (tarea_envio) {
        xTaskNotifyGive(tarea_envio);
    }
    return r;
}

void andia_vigilia(bool activa)
{
    vigilia = activa;
    ESP_LOGI(TAG, "vigilia %s", activa ? "activada" : "desactivada");
    if (activa) {
        if (red_conectada() && asegurar_cliente() == ESP_OK && !esp_websocket_client_is_connected(ws)) {
            esp_websocket_client_start(ws);   // conexión permanente: el "Oye" no puede esperar al TLS
        }
        if (!voz_capturando()) {
            voz_capturar_iniciar(al_capturar);
        }
    } else if (estado == ANDIA_INACTIVA) {
        voz_capturar_detener();
        vaciar_cola();
    }
}

bool andia_vigilia_activa(void) { return vigilia; }

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

#pragma once
// Cliente de ANDIA (asistente de voz en la Pi) por WebSocket. Los eventos van a la tarea de la app por una cola.
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ANDIA_EV_RED = 0,       // cambió el WiFi (payload: red_conectada)
    ANDIA_EV_ESTADO,        // cambió el estado de la conversación (payload: estado)
    ANDIA_EV_PREGUNTA,      // texto transcrito de lo que dijiste
    ANDIA_EV_RESPUESTA,     // texto de la respuesta (la voz llega aparte y se reproduce sola)
    ANDIA_EV_FIN,           // terminó la conversación (texto = motivo, puede ir vacío)
    ANDIA_EV_ERROR,         // algo falló (texto legible)
    ANDIA_EV_AVISO,         // mensaje empujado por la Pi (texto + origen)
    ANDIA_EV_TEMPORIZADOR,  // el reloj debe contar N segundos y avisar
    ANDIA_EV_SIGUE,         // la Pi reconoció el nombre (en vigilia, o dijiste solo el nombre): pitar
} andia_evento_tipo_t;

typedef enum {
    ANDIA_INACTIVA = 0,
    ANDIA_CONECTANDO,
    ANDIA_ESCUCHANDO,       // grabando (modo gesto: aún sin activación confirmada)
    ANDIA_ACTIVADA,         // "te escucho"
    ANDIA_PENSANDO,
    ANDIA_HABLANDO,
} andia_estado_t;

typedef struct {
    andia_evento_tipo_t tipo;
    union {
        bool red_conectada;
        andia_estado_t estado;
        int segundos;
    };
    char texto[200];        // pregunta / respuesta / error / aviso / motivo (UTF-8, truncado limpio)
    char origen[16];        // para aviso: "recordatorio", "pc", …
} andia_evento_t;

esp_err_t andia_init(QueueHandle_t cola);    // prepara el cliente; no conecta hasta la primera conversación
esp_err_t andia_conversar(bool gesto);       // abre la conexión si hace falta, manda "inicio" y empieza a capturar
void andia_cancelar(void);
// Vigilia ("Oye Andia" sin tocar nada): el micrófono captura siempre en local y, cuando hay voz cerca, manda la
// frase en modo gesto para que la Pi decida si empezaba por el nombre. Mantiene la conexión abierta.
void andia_vigilia(bool activa);
bool andia_vigilia_activa(void);
andia_estado_t andia_estado(void);
bool andia_conectada(void);                  // WebSocket abierto con la Pi

#ifdef __cplusplus
}
#endif

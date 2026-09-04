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
void red_ahorro(bool activo);  // false durante una conversación (latencia), true en reposo
#ifdef __cplusplus
}
#endif

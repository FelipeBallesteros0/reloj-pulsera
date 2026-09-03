#pragma once
#include <stdint.h>
// Cliente del Current Time Service del iPhone: descubre 0x1805/0x2A2B, publica BLE_EV_HORA y al terminar
// (con o sin éxito) llama a al_terminar(conn) para encadenar el siguiente cliente (ANCS).
void cts_leer(uint16_t conn_handle, void (*al_terminar)(uint16_t conn_handle));

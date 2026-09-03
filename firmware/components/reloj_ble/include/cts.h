#pragma once
#include <stdint.h>
// Cliente del Current Time Service del iPhone: descubre 0x1805/0x2A2B y publica BLE_EV_HORA.
void cts_leer(uint16_t conn_handle);

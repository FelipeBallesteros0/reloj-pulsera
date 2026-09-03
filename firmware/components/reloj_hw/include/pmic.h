#pragma once
// Gestor de energía AXP2101 (I2C 0x34): batería, USB, botón PWR (va al PWRON del chip, no a un GPIO) y apagado.
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool pwr_corto;     // pulsación corta del botón PWR
    bool pwr_largo;     // pulsación larga (~1 s) del botón PWR
    bool usb_puesto;    // se conectó el USB
    bool usb_quitado;   // se desconectó el USB
    bool carga_inicio;
    bool carga_fin;
} pmic_eventos_t;

esp_err_t pmic_init(i2c_master_bus_handle_t bus);
// Lee y limpia las IRQ pendientes del chip. Llamar cada ~250 ms (la línea IRQ no llega al ESP32).
void pmic_sondear(pmic_eventos_t *ev);
int  pmic_bateria_pct(void);     // 0-100, o -1 si no detecta batería
int  pmic_bateria_mv(void);
bool pmic_usb_conectado(void);
bool pmic_cargando(void);
float pmic_temperatura(void);
void pmic_apagar(void);          // corta todos los rieles: el reloj se apaga (vuelve con PWR)

#ifdef __cplusplus
}
#endif

#include "pmic.h"
#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"

static const char *TAG = "pmic";
static i2c_master_dev_handle_t dev;
static XPowersPMU pmu;

// XPowersLib no sabe de ESP-IDF: le damos lectura/escritura de registros sobre el bus I2C del BSP.
static int leer_reg(uint8_t addr, uint8_t reg, uint8_t *data, uint8_t len)
{
    (void)addr;
    return i2c_master_transmit_receive(dev, &reg, 1, data, len, 100) == ESP_OK ? 0 : -1;
}

static int escribir_reg(uint8_t addr, uint8_t reg, uint8_t *data, uint8_t len)
{
    (void)addr;
    uint8_t buf[8];
    if (len + 1 > sizeof(buf)) {
        return -1;
    }
    buf[0] = reg;
    memcpy(&buf[1], data, len);
    return i2c_master_transmit(dev, buf, len + 1, 100) == ESP_OK ? 0 : -1;
}

esp_err_t pmic_init(i2c_master_bus_handle_t bus)
{
    i2c_device_config_t cfg = {};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = AXP2101_SLAVE_ADDRESS;
    cfg.scl_speed_hz = 400000;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &cfg, &dev), TAG, "no se pudo añadir el AXP2101 al bus");
    if (!pmu.begin(AXP2101_SLAVE_ADDRESS, leer_reg, escribir_reg)) {
        ESP_LOGE(TAG, "el AXP2101 no responde en 0x34");
        dev = nullptr;
        return ESP_FAIL;
    }

    pmu.enableBattDetection();
    pmu.enableVbusVoltageMeasure();
    pmu.enableBattVoltageMeasure();
    pmu.enableSystemVoltageMeasure();
    pmu.enableTemperatureMeasure();
    // La placa no trae termistor de batería útil: sin esto la carga se comporta raro (nota de Waveshare).
    pmu.disableTSPinMeasure();

    // Cargador como en el ejemplo de Waveshare: precarga 50 mA, carga 400 mA, corte 25 mA, 4,2 V.
    pmu.setPrechargeCurr(XPOWERS_AXP2101_PRECHARGE_50MA);
    pmu.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_400MA);
    pmu.setChargerTerminationCurr(XPOWERS_AXP2101_CHG_ITERM_25MA);
    pmu.setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V2);

    // Botón PWR: mantenerlo 4 s apaga por hardware; pulsaciones corta/larga llegan como IRQ.
    pmu.setPowerKeyPressOffTime(XPOWERS_POWEROFF_4S);
    pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    pmu.clearIrqStatus();
    pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ | XPOWERS_AXP2101_PKEY_LONG_IRQ |
                  XPOWERS_AXP2101_VBUS_INSERT_IRQ | XPOWERS_AXP2101_VBUS_REMOVE_IRQ |
                  XPOWERS_AXP2101_BAT_CHG_START_IRQ | XPOWERS_AXP2101_BAT_CHG_DONE_IRQ);

    ESP_LOGI(TAG, "AXP2101 listo: batería %d %% (%u mV), USB %s, %s", pmu.getBatteryPercent(),
             pmu.getBattVoltage(), pmu.isVbusIn() ? "conectado" : "no", pmu.isCharging() ? "cargando" : "sin cargar");
    return ESP_OK;
}

void pmic_sondear(pmic_eventos_t *ev)
{
    memset(ev, 0, sizeof(*ev));
    if (!dev) {
        return;
    }
    pmu.getIrqStatus();
    ev->pwr_corto = pmu.isPekeyShortPressIrq();
    ev->pwr_largo = pmu.isPekeyLongPressIrq();
    ev->usb_puesto = pmu.isVbusInsertIrq();
    ev->usb_quitado = pmu.isVbusRemoveIrq();
    ev->carga_inicio = pmu.isBatChargeStartIrq();
    ev->carga_fin = pmu.isBatChargeDoneIrq();
    pmu.clearIrqStatus();
}

int pmic_bateria_pct(void) { return (dev && pmu.isBatteryConnect()) ? pmu.getBatteryPercent() : -1; }
int pmic_bateria_mv(void) { return dev ? pmu.getBattVoltage() : 0; }
bool pmic_usb_conectado(void) { return dev && pmu.isVbusIn(); }
bool pmic_cargando(void) { return dev && pmu.isCharging(); }
float pmic_temperatura(void) { return dev ? pmu.getTemperature() : 0.0f; }
void pmic_apagar(void)
{
    if (dev) {
        ESP_LOGW(TAG, "apagando el reloj");
        pmu.shutdown();
    }
}

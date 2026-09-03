#include "rtc_pcf85063.h"
#include <string.h>
#include "esp_log.h"
#include "esp_check.h"

#define PCF85063_ADDR        0x51
#define REG_CONTROL_1        0x00
#define REG_SECONDS          0x04   // 0x04..0x0A: seg, min, hora, día, día-semana, mes, año
#define SECONDS_OS_BIT       0x80   // "oscillator stopped": la hora no es fiable
#define I2C_TIMEOUT_MS       100

static const char *TAG = "rtc";
static i2c_master_dev_handle_t dev;

static uint8_t bcd2bin(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
static uint8_t bin2bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

static esp_err_t leer(uint8_t reg, uint8_t *buf, size_t n)
{
    return i2c_master_transmit_receive(dev, &reg, 1, buf, n, I2C_TIMEOUT_MS);
}

static esp_err_t escribir(uint8_t reg, const uint8_t *buf, size_t n)
{
    uint8_t tmp[16];
    if (n + 1 > sizeof(tmp)) {
        return ESP_ERR_INVALID_SIZE;
    }
    tmp[0] = reg;
    memcpy(&tmp[1], buf, n);
    return i2c_master_transmit(dev, tmp, n + 1, I2C_TIMEOUT_MS);
}

esp_err_t rtc_pcf85063_init(i2c_master_bus_handle_t bus)
{
    ESP_RETURN_ON_FALSE(bus != NULL, ESP_ERR_INVALID_ARG, TAG, "bus nulo");
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCF85063_ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &cfg, &dev), TAG, "no se pudo añadir el PCF85063");

    // Control_1: modo 24 h, oscilador en marcha, capacitancia 7 pF (bit 0 = 0) → 0x00
    uint8_t ctrl = 0x00;
    ESP_RETURN_ON_ERROR(escribir(REG_CONTROL_1, &ctrl, 1), TAG, "el PCF85063 no responde en 0x51");

    uint8_t seg = 0;
    ESP_RETURN_ON_ERROR(leer(REG_SECONDS, &seg, 1), TAG, "lectura fallida");
    ESP_LOGI(TAG, "PCF85063 listo (%s)", (seg & SECONDS_OS_BIT) ? "hora NO fiable: oscilador detenido" : "hora conservada");
    return ESP_OK;
}

esp_err_t rtc_pcf85063_get(struct tm *t)
{
    ESP_RETURN_ON_FALSE(dev != NULL, ESP_ERR_INVALID_STATE, TAG, "sin init");
    uint8_t r[7];
    ESP_RETURN_ON_ERROR(leer(REG_SECONDS, r, sizeof(r)), TAG, "lectura fallida");
    memset(t, 0, sizeof(*t));
    t->tm_sec  = bcd2bin(r[0] & 0x7F);
    t->tm_min  = bcd2bin(r[1] & 0x7F);
    t->tm_hour = bcd2bin(r[2] & 0x3F);
    t->tm_mday = bcd2bin(r[3] & 0x3F);
    t->tm_wday = r[4] & 0x07;
    t->tm_mon  = bcd2bin(r[5] & 0x1F) - 1;      // struct tm: 0-11
    t->tm_year = bcd2bin(r[6]) + 100;            // struct tm: años desde 1900; el chip cuenta desde 2000
    t->tm_isdst = -1;
    return (r[0] & SECONDS_OS_BIT) ? ESP_ERR_INVALID_STATE : ESP_OK;
}

esp_err_t rtc_pcf85063_set(const struct tm *t)
{
    ESP_RETURN_ON_FALSE(dev != NULL, ESP_ERR_INVALID_STATE, TAG, "sin init");
    ESP_RETURN_ON_FALSE(t->tm_year >= 100 && t->tm_year <= 199, ESP_ERR_INVALID_ARG, TAG, "año fuera de 2000-2099");
    uint8_t r[7] = {
        bin2bcd((uint8_t)t->tm_sec),             // bit OS = 0 → hora fiable
        bin2bcd((uint8_t)t->tm_min),
        bin2bcd((uint8_t)t->tm_hour),
        bin2bcd((uint8_t)t->tm_mday),
        (uint8_t)(t->tm_wday & 0x07),
        bin2bcd((uint8_t)(t->tm_mon + 1)),
        bin2bcd((uint8_t)(t->tm_year - 100)),
    };
    return escribir(REG_SECONDS, r, sizeof(r));
}

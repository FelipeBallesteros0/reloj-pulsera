#include "cts.h"
#include "cts_parse.h"
#include "ble.h"
#include "esp_log.h"
#include "host/ble_hs.h"

static const char *TAG = "cts";
static uint16_t svc_inicio, svc_fin;
static void (*cb_fin)(uint16_t);

static void terminar(uint16_t conn)
{
    if (cb_fin) {
        cb_fin(conn);
    }
}

static int al_leer(uint16_t conn, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg)
{
    if (err->status != 0 || !attr) {
        ESP_LOGW(TAG, "lectura de Current Time falló: %d", err->status);
        terminar(conn);
        return 0;
    }
    uint8_t buf[10];
    uint16_t n = OS_MBUF_PKTLEN(attr->om);
    if (n > sizeof(buf)) {
        n = sizeof(buf);
    }
    os_mbuf_copydata(attr->om, 0, n, buf);
    ble_evento_t ev = { .tipo = BLE_EV_HORA };
    if (cts_parse(buf, n, &ev.hora)) {
        ESP_LOGI(TAG, "hora del iPhone: %04d-%02d-%02d %02d:%02d:%02d", ev.hora.tm_year + 1900, ev.hora.tm_mon + 1,
                 ev.hora.tm_mday, ev.hora.tm_hour, ev.hora.tm_min, ev.hora.tm_sec);
        ble_publicar(&ev);
    } else {
        ESP_LOGW(TAG, "Current Time inválido (%d bytes)", n);
    }
    terminar(conn);
    return 0;
}

static int al_descubrir_chr(uint16_t conn, const struct ble_gatt_error *err, const struct ble_gatt_chr *chr, void *arg)
{
    static bool leyendo;
    if (err->status == 0 && chr) {
        leyendo = true;
        ble_gattc_read(conn, chr->val_handle, al_leer, NULL);
    } else if (err->status == BLE_HS_EDONE) {
        if (!leyendo) {
            ESP_LOGW(TAG, "sin característica Current Time");
            terminar(conn);
        }
        leyendo = false;
    } else {
        ESP_LOGW(TAG, "sin característica Current Time: %d", err->status);
        terminar(conn);
    }
    return 0;
}

static int al_descubrir_svc(uint16_t conn, const struct ble_gatt_error *err, const struct ble_gatt_svc *svc, void *arg)
{
    if (err->status == 0 && svc) {
        svc_inicio = svc->start_handle;
        svc_fin = svc->end_handle;
    } else if (err->status == BLE_HS_EDONE) {
        if (svc_inicio) {
            ble_gattc_disc_chrs_by_uuid(conn, svc_inicio, svc_fin, BLE_UUID16_DECLARE(0x2A2B), al_descubrir_chr, NULL);
        } else {
            ESP_LOGW(TAG, "el teléfono no expone el Current Time Service");
            terminar(conn);
        }
    } else {
        ESP_LOGW(TAG, "descubrimiento CTS: %d", err->status);
        terminar(conn);
    }
    return 0;
}

void cts_leer(uint16_t conn, void (*al_terminar)(uint16_t))
{
    cb_fin = al_terminar;
    svc_inicio = svc_fin = 0;
    ble_gattc_disc_svc_by_uuid(conn, BLE_UUID16_DECLARE(0x1805), al_descubrir_svc, NULL);
}

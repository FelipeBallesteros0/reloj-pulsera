#include "ble.h"
#include "cts.h"
#include "ancs.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_check.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

void ble_store_config_init(void);   // la aporta el store NVS de NimBLE (CONFIG_BT_NIMBLE_NVS_PERSIST)

static const char *TAG = "ble";
static QueueHandle_t cola;
static uint8_t own_addr_type;
static uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;
static ble_estado_t estado = BLE_ANUNCIANDO;
static char direccion[18] = "--";

// UUID del Apple Notification Center Service, 7905F431-B5CE-4E99-A40F-4B1E122D00D0, en orden little-endian.
static const ble_uuid128_t UUID_ANCS = BLE_UUID128_INIT(0xd0, 0x00, 0x2d, 0x12, 0x1e, 0x4b, 0x0f, 0xa4,
                                                         0x99, 0x4e, 0xce, 0xb5, 0x31, 0xf4, 0x05, 0x79);

static int gap_evento(struct ble_gap_event *ev, void *arg);

void ble_publicar(const ble_evento_t *ev)
{
    if (cola) {
        xQueueSend(cola, ev, 0);
    }
}

static void publicar_estado(ble_estado_t e)
{
    estado = e;
    ble_evento_t ev = { .tipo = BLE_EV_ESTADO, .estado = e };
    ble_publicar(&ev);
}

static void anunciar(void)
{
    // Todo en el paquete principal (máx. 31 B): flags (3) + solicitud de ANCS (18) + nombre corto (2 + n).
    // Así iOS no depende de la respuesta de escaneo. Con "Reloj" son 28 B; el nombre largo iría en la respuesta.
    struct ble_hs_adv_fields f = { 0 };
    struct ble_hs_adv_fields rsp = { 0 };
    const char *nombre = ble_svc_gap_device_name();
    size_t largo = strlen(nombre);
    bool cabe = 3 + 18 + 2 + largo <= 31;

    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.sol_uuids128 = &UUID_ANCS;
    f.sol_num_uuids128 = 1;
    if (cabe) {
        f.name = (uint8_t *)nombre;
        f.name_len = largo;
        f.name_is_complete = 1;
    } else {
        rsp.name = (uint8_t *)nombre;
        rsp.name_len = largo;
        rsp.name_is_complete = 1;
    }
    int rc = ble_gap_adv_set_fields(&f);
    if (rc) {
        ESP_LOGE(TAG, "adv_set_fields rc=%d", rc);
        return;
    }
    rc = ble_gap_adv_rsp_set_fields(&rsp);
    if (rc) {
        ESP_LOGE(TAG, "adv_rsp_set_fields rc=%d", rc);
        return;
    }
    struct ble_gap_adv_params p = { 0 };
    p.conn_mode = BLE_GAP_CONN_MODE_UND;
    p.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &p, gap_evento, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "adv_start rc=%d", rc);
        return;
    }
    ESP_LOGI(TAG, "anunciando como \"%s\" (nombre %s)", nombre, cabe ? "en el anuncio" : "en la respuesta de escaneo");
    publicar_estado(BLE_ANUNCIANDO);
}

static void borrar_bond_del_par(uint16_t conn)
{
    struct ble_gap_conn_desc d;
    if (ble_gap_conn_find(conn, &d) == 0) {
        ble_store_util_delete_peer(&d.peer_id_addr);
    }
}

static int gap_evento(struct ble_gap_event *ev, void *arg)
{
    switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (ev->connect.status == 0) {
            conn_handle = ev->connect.conn_handle;
            ESP_LOGI(TAG, "conectado (handle %d)", conn_handle);
            publicar_estado(BLE_CONECTADO);
            ble_gattc_exchange_mtu(conn_handle, NULL, NULL);
            int rc = ble_gap_security_initiate(conn_handle);   // pide cifrado + bonding → llega ENC_CHANGE
            if (rc) {
                ESP_LOGW(TAG, "security_initiate rc=%d", rc);
            }
        } else {
            ESP_LOGW(TAG, "conexión fallida: %d", ev->connect.status);
            conn_handle = BLE_HS_CONN_HANDLE_NONE;
            anunciar();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "desconectado (motivo %d)", ev->disconnect.reason);
        conn_handle = BLE_HS_CONN_HANDLE_NONE;
        ancs_desconectado();
        anunciar();
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        anunciar();
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        if (ev->enc_change.status == 0) {
            struct ble_gap_conn_desc d;
            ble_gap_conn_find(ev->enc_change.conn_handle, &d);
            ESP_LOGI(TAG, "enlace cifrado (bonded=%d, autenticado=%d)", d.sec_state.bonded, d.sec_state.authenticated);
            publicar_estado(BLE_VINCULADO);
            cts_leer(ev->enc_change.conn_handle, ancs_iniciar);   // hora primero, luego notificaciones
        } else {
            // Caso típico: el iPhone "olvidó" el reloj pero nosotros aún guardábamos su bond.
            ESP_LOGW(TAG, "cifrado falló (status %d): borro el bond y corto para reemparejar limpio", ev->enc_change.status);
            borrar_bond_del_par(ev->enc_change.conn_handle);
            ble_gap_terminate(ev->enc_change.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        // El iPhone quiere emparejar de nuevo con un bond ya existente: se borra el viejo y se acepta.
        ESP_LOGI(TAG, "re-emparejamiento pedido por el teléfono");
        borrar_bond_del_par(ev->repeat_pairing.conn_handle);
        return BLE_GAP_REPEAT_PAIRING_RETRY;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        ESP_LOGW(TAG, "acción de passkey %d (no esperada con Just Works)", ev->passkey.params.action);
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU %d", ev->mtu.value);
        return 0;

    case BLE_GAP_EVENT_NOTIFY_RX: {
        uint8_t buf[256];
        uint16_t n = OS_MBUF_PKTLEN(ev->notify_rx.om);
        if (n > sizeof(buf)) {
            n = sizeof(buf);
        }
        os_mbuf_copydata(ev->notify_rx.om, 0, n, buf);
        ancs_notificacion_rx(ev->notify_rx.attr_handle, buf, n);
        return 0;
    }

    default:
        return 0;
    }
}

static void al_sincronizar(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc) {
        ESP_LOGE(TAG, "sin dirección BLE: %d", rc);
        return;
    }
    ble_hs_id_infer_auto(0, &own_addr_type);
    uint8_t a[6];
    ble_hs_id_copy_addr(own_addr_type, a, NULL);
    snprintf(direccion, sizeof(direccion), "%02X:%02X:%02X:%02X:%02X:%02X", a[5], a[4], a[3], a[2], a[1], a[0]);
    ESP_LOGI(TAG, "dirección %s, bonds guardados: %s", direccion, ble_tiene_bond() ? "sí" : "no");
    anunciar();
}

static void al_reiniciar(int motivo) { ESP_LOGW(TAG, "host reiniciado: %d", motivo); }

static void tarea_host(void *p)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t ble_init(const char *nombre, QueueHandle_t q)
{
    cola = q;
    ESP_RETURN_ON_ERROR(nimble_port_init(), TAG, "nimble_port_init");

    ble_hs_cfg.reset_cb = al_reiniciar;
    ble_hs_cfg.sync_cb = al_sincronizar;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;   // Just Works: el iPhone solo pregunta "¿Enlazar?"
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;                         // LE Secure Connections
    // ENC para el cifrado; ID (la IRK) para reconocer la dirección aleatoria del iPhone al reconectar.
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set(nombre);
    ble_store_config_init();

    nimble_port_freertos_init(tarea_host);
    return ESP_OK;
}

ble_estado_t ble_estado(void) { return estado; }

bool ble_tiene_bond(void)
{
    int n = 0;
    ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC, &n);
    return n > 0;
}

void ble_olvidar(void)
{
    if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    ble_store_clear();
    ESP_LOGI(TAG, "bonds borrados");
}

const char *ble_direccion(void) { return direccion; }

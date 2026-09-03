#include "ancs.h"
#include "ble.h"
#include "ancs_proto.h"
#include "notif_store.h"
#include "apps.h"
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "host/ble_hs.h"

static const char *TAG = "ancs";

// UUIDs de ANCS (little-endian)
static const ble_uuid128_t UUID_SVC = BLE_UUID128_INIT(0xd0, 0x00, 0x2d, 0x12, 0x1e, 0x4b, 0x0f, 0xa4, 0x99, 0x4e, 0xce, 0xb5, 0x31, 0xf4, 0x05, 0x79);
// Notification Source 9FBF120D-6301-42D9-8C58-25E699A21DBD
static const ble_uuid128_t UUID_NS = BLE_UUID128_INIT(0xbd, 0x1d, 0xa2, 0x99, 0xe6, 0x25, 0x58, 0x8c, 0xd9, 0x42, 0x01, 0x63, 0x0d, 0x12, 0xbf, 0x9f);
// Control Point 69D1D8F3-45E1-49A8-9821-9BBDFDAAD9D9
static const ble_uuid128_t UUID_CP = BLE_UUID128_INIT(0xd9, 0xd9, 0xaa, 0xfd, 0xbd, 0x9b, 0x21, 0x98, 0xa8, 0x49, 0xe1, 0x45, 0xf3, 0xd8, 0xd1, 0x69);
// Data Source 22EAC6E9-24D6-4BB5-BE44-B36ACE7C7BFB
static const ble_uuid128_t UUID_DS = BLE_UUID128_INIT(0xfb, 0x7b, 0x7c, 0xce, 0x6a, 0xb3, 0x44, 0xbe, 0xb5, 0x4b, 0xd6, 0x24, 0xe9, 0xc6, 0xea, 0x22);

static uint16_t conn = BLE_HS_CONN_HANDLE_NONE;
static uint16_t svc_inicio, svc_fin;
static uint16_t h_ns, h_cp, h_ds, h_ns_cccd, h_ds_cccd;
static bool listo;

// Cola de peticiones al Control Point: una pendiente a la vez (el iPhone responde en orden por el Data Source).
typedef struct { uint8_t tipo; uint32_t uid; char app_id[ANCS_LARGO_APP_ID]; } peticion_t;
#define COLA_MAX 16
static peticion_t cola[COLA_MAX];
static int cola_n, cola_ini;
static bool pendiente;
static uint32_t pendiente_uid;
static uint8_t pendiente_flags;   // flags de la NS para saber si avisar
static ancs_ds_t acum;
static esp_timer_handle_t timer_espera;

static void publicar_notif(uint8_t tipo, uint32_t uid, bool aviso)
{
    ble_evento_t ev = { .tipo = BLE_EV_NOTIF, .notif = { .tipo = tipo, .uid = uid, .aviso = aviso } };
    ble_publicar(&ev);
}

static void siguiente_peticion(void);

static int al_escribir_cp(uint16_t c, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg);

static void enviar(const peticion_t *p)
{
    uint8_t cmd[64];
    size_t n = 0;
    if (p->tipo == ANCS_CMD_GET_NOTIF_ATTRS) {
        n = ancs_cmd_get_attrs(cmd, sizeof(cmd), p->uid);
    } else {
        n = ancs_cmd_get_app_attrs(cmd, sizeof(cmd), p->app_id);
    }
    ancs_ds_reset(&acum);
    pendiente = true;
    pendiente_uid = p->uid;
    int rc = ble_gattc_write_flat(conn, h_cp, cmd, n, al_escribir_cp, NULL);
    if (rc) {
        ESP_LOGW(TAG, "escritura al Control Point falló rc=%d", rc);
        pendiente = false;
        siguiente_peticion();
        return;
    }
    esp_timer_start_once(timer_espera, 4000000);   // 4 s por si el iPhone no responde
}

static void siguiente_peticion(void)
{
    if (pendiente || cola_n == 0 || conn == BLE_HS_CONN_HANDLE_NONE) {
        return;
    }
    peticion_t p = cola[cola_ini];
    cola_ini = (cola_ini + 1) % COLA_MAX;
    cola_n--;
    enviar(&p);
}

static void encolar(uint8_t tipo, uint32_t uid, const char *app_id)
{
    if (cola_n == COLA_MAX) {
        ESP_LOGW(TAG, "cola de peticiones llena: descarto uid %lu", (unsigned long)uid);
        return;
    }
    peticion_t *p = &cola[(cola_ini + cola_n) % COLA_MAX];
    p->tipo = tipo;
    p->uid = uid;
    if (app_id) {
        strncpy(p->app_id, app_id, sizeof(p->app_id) - 1);
        p->app_id[sizeof(p->app_id) - 1] = '\0';
    } else {
        p->app_id[0] = '\0';
    }
    cola_n++;
    siguiente_peticion();
}

static int al_escribir_cp(uint16_t c, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg)
{
    if (err->status != 0) {
        // 0xA0-0xA3: errores propios de ANCS (p. ej. 0xA2 = la notificación ya no existe)
        ESP_LOGW(TAG, "el iPhone rechazó la petición: status 0x%x", err->status);
        esp_timer_stop(timer_espera);
        pendiente = false;
        siguiente_peticion();
    }
    return 0;
}

static void al_vencer_espera(void *arg)
{
    if (pendiente) {
        ESP_LOGW(TAG, "sin respuesta del Data Source en 4 s (uid %lu)", (unsigned long)pendiente_uid);
        pendiente = false;
        siguiente_peticion();
    }
}

static void respuesta_completa(void)
{
    esp_timer_stop(timer_espera);
    pendiente = false;
    if (acum.buf[0] == ANCS_CMD_GET_NOTIF_ATTRS) {
        ancs_attrs_t at;
        if (ancs_parse_attrs(acum.buf, acum.len, &at)) {
            notif_store_poner_detalle(at.uid, &at);
            ESP_LOGI(TAG, "notificación %lu: [%s] %s — %.40s", (unsigned long)at.uid, at.app_id, at.titulo, at.mensaje);
            publicar_notif(BLE_NOTIF_DETALLE, at.uid, !(pendiente_flags & (ANCS_FLAG_SILENCIOSA | ANCS_FLAG_PREEXISTENTE)));
            if (!apps_nombre(at.app_id) && !apps_en_cache(at.app_id) && at.app_id[0]) {
                encolar(ANCS_CMD_GET_APP_ATTRS, 0, at.app_id);
            }
        } else {
            ESP_LOGW(TAG, "respuesta de atributos mal formada (%u bytes)", (unsigned)acum.len);
        }
    } else {
        char id[ANCS_LARGO_APP_ID], nombre[ANCS_LARGO_NOMBRE_APP];
        if (ancs_parse_app_attrs(acum.buf, acum.len, id, sizeof(id), nombre, sizeof(nombre)) && nombre[0]) {
            apps_cachear(id, nombre);
            ESP_LOGI(TAG, "app %s = \"%s\"", id, nombre);
            publicar_notif(BLE_NOTIF_APP, 0, false);
        }
    }
    siguiente_peticion();
}

bool ancs_notificacion_rx(uint16_t attr_handle, const uint8_t *datos, uint16_t largo)
{
    if (attr_handle == h_ns && h_ns) {
        ancs_ns_t ns;
        if (!ancs_parse_ns(datos, largo, &ns)) {
            return true;
        }
        switch (ns.evento) {
        case ANCS_EV_AGREGADA:
        case ANCS_EV_MODIFICADA:
            notif_store_agregar(ns.uid, ns.categoria, ns.flags, (uint32_t)(esp_timer_get_time() / 1000));
            pendiente_flags = ns.flags;
            publicar_notif(ns.evento == ANCS_EV_AGREGADA ? BLE_NOTIF_NUEVA : BLE_NOTIF_MODIFICADA, ns.uid, false);
            encolar(ANCS_CMD_GET_NOTIF_ATTRS, ns.uid, NULL);
            break;
        case ANCS_EV_QUITADA:
            notif_store_quitar(ns.uid);
            publicar_notif(BLE_NOTIF_QUITADA, ns.uid, false);
            break;
        }
        return true;
    }
    if (attr_handle == h_ds && h_ds) {
        int r = ancs_ds_alimentar(&acum, datos, largo);
        if (r == 1) {
            respuesta_completa();
        } else if (r < 0) {
            ESP_LOGW(TAG, "fragmento del Data Source inválido; descarto");
            esp_timer_stop(timer_espera);
            pendiente = false;
            siguiente_peticion();
        }
        return true;
    }
    return false;
}

// --- Descubrimiento y suscripción -------------------------------------------------------------

static void suscribir(uint16_t c, uint16_t cccd);
static int al_suscribir(uint16_t c, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg)
{
    uint16_t cual = (uint16_t)(uintptr_t)arg;
    if (err->status != 0) {
        ESP_LOGW(TAG, "suscripción a %s falló: %d", cual == h_ds_cccd ? "Data Source" : "Notification Source", err->status);
        return 0;
    }
    if (cual == h_ds_cccd) {
        suscribir(c, h_ns_cccd);
    } else {
        listo = true;
        ESP_LOGI(TAG, "suscrito: ya llegan las notificaciones del iPhone");
        ble_evento_t ev = { .tipo = BLE_EV_ANCS, .ancs_listo = true };
        ble_publicar(&ev);
    }
    return 0;
}

// Fin del rango de descriptores de una característica: justo antes de la declaración de la siguiente.
static uint16_t fin_descriptores(uint16_t val)
{
    uint16_t sig = 0xFFFF;
    const uint16_t otros[3] = { h_ns, h_cp, h_ds };
    for (int i = 0; i < 3; i++) {
        if (otros[i] > val && otros[i] < sig) {
            sig = otros[i];
        }
    }
    return sig == 0xFFFF ? svc_fin : (uint16_t)(sig - 2);   // la siguiente ocupa (declaración = val-1, valor)
}

static int al_suscribir(uint16_t c, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg);

static void suscribir(uint16_t c, uint16_t cccd)
{
    static const uint8_t on[2] = { 0x01, 0x00 };
    int rc = ble_gattc_write_flat(c, cccd, on, sizeof(on), al_suscribir, (void *)(uintptr_t)cccd);
    if (rc) {
        ESP_LOGW(TAG, "no pude escribir el CCCD %d: rc=%d", cccd, rc);
    }
}

static int al_descubrir_dsc(uint16_t c, const struct ble_gatt_error *err, uint16_t chr_val_handle,
                            const struct ble_gatt_dsc *dsc, void *arg)
{
    if (err->status == 0 && dsc) {
        char u[40];
        ble_uuid_to_str(&dsc->uuid.u, u);
        ESP_LOGI(TAG, "descriptor de %d: handle %d uuid %s", chr_val_handle, dsc->handle, u);
        if (ble_uuid_cmp(&dsc->uuid.u, BLE_UUID16_DECLARE(0x2902)) == 0) {
            if (chr_val_handle == h_ns) {
                h_ns_cccd = dsc->handle;
            } else if (chr_val_handle == h_ds) {
                h_ds_cccd = dsc->handle;
            }
        }
    } else if (err->status == BLE_HS_EDONE) {
        if (chr_val_handle == h_ns) {
            int rc = ble_gattc_disc_all_dscs(c, h_ds, fin_descriptores(h_ds), al_descubrir_dsc, NULL);
            if (rc) {
                ESP_LOGW(TAG, "disc_all_dscs(DS) rc=%d", rc);
            }
        } else if (h_ns_cccd && h_ds_cccd) {
            ESP_LOGI(TAG, "CCCD: NS=%d DS=%d; suscribiendo", h_ns_cccd, h_ds_cccd);
            suscribir(c, h_ds_cccd);   // primero el Data Source, luego el Notification Source
        } else {
            ESP_LOGW(TAG, "no encontré los descriptores CCCD de ANCS (NS=%d DS=%d)", h_ns_cccd, h_ds_cccd);
        }
    } else {
        ESP_LOGW(TAG, "descubrimiento de descriptores de %d: status %d", chr_val_handle, err->status);
    }
    return 0;
}

static int al_descubrir_chr(uint16_t c, const struct ble_gatt_error *err, const struct ble_gatt_chr *chr, void *arg)
{
    if (err->status == 0 && chr) {
        if (ble_uuid_cmp(&chr->uuid.u, &UUID_NS.u) == 0) {
            h_ns = chr->val_handle;
        } else if (ble_uuid_cmp(&chr->uuid.u, &UUID_CP.u) == 0) {
            h_cp = chr->val_handle;
        } else if (ble_uuid_cmp(&chr->uuid.u, &UUID_DS.u) == 0) {
            h_ds = chr->val_handle;
        }
    } else if (err->status == BLE_HS_EDONE) {
        if (h_ns && h_cp && h_ds) {
            ESP_LOGI(TAG, "características ANCS: NS=%d CP=%d DS=%d (servicio %d-%d)", h_ns, h_cp, h_ds, svc_inicio, svc_fin);
            int rc = ble_gattc_disc_all_dscs(c, h_ns, fin_descriptores(h_ns), al_descubrir_dsc, NULL);
            if (rc) {
                ESP_LOGW(TAG, "disc_all_dscs(NS) rc=%d", rc);
            }
        } else {
            ESP_LOGW(TAG, "faltan características ANCS (NS=%d CP=%d DS=%d)", h_ns, h_cp, h_ds);
        }
    } else {
        ESP_LOGW(TAG, "descubrimiento de características ANCS: %d", err->status);
    }
    return 0;
}

static int al_descubrir_svc(uint16_t c, const struct ble_gatt_error *err, const struct ble_gatt_svc *svc, void *arg)
{
    if (err->status == 0 && svc) {
        svc_inicio = svc->start_handle;
        svc_fin = svc->end_handle;
    } else if (err->status == BLE_HS_EDONE) {
        if (svc_inicio) {
            ble_gattc_disc_all_chrs(c, svc_inicio, svc_fin, al_descubrir_chr, NULL);
        } else {
            ESP_LOGW(TAG, "el teléfono no expone ANCS (¿no es un iPhone?)");
        }
    } else {
        ESP_LOGW(TAG, "descubrimiento ANCS: %d", err->status);
    }
    return 0;
}

void ancs_iniciar(uint16_t c)
{
    if (!timer_espera) {
        const esp_timer_create_args_t a = { .callback = al_vencer_espera, .name = "ancs_espera" };
        esp_timer_create(&a, &timer_espera);
    }
    conn = c;
    svc_inicio = svc_fin = 0;
    h_ns = h_cp = h_ds = h_ns_cccd = h_ds_cccd = 0;
    listo = false;
    cola_n = cola_ini = 0;
    pendiente = false;
    ble_gattc_disc_svc_by_uuid(c, &UUID_SVC.u, al_descubrir_svc, NULL);
}

void ancs_desconectado(void)
{
    if (timer_espera) {
        esp_timer_stop(timer_espera);
    }
    conn = BLE_HS_CONN_HANDLE_NONE;
    listo = false;
    pendiente = false;
    cola_n = cola_ini = 0;
    h_ns = h_cp = h_ds = 0;
}

bool ancs_listo(void) { return listo; }

bool ancs_accion(uint32_t uid, bool positiva)
{
    if (!listo || conn == BLE_HS_CONN_HANDLE_NONE) {
        return false;
    }
    uint8_t cmd[8];
    size_t n = ancs_cmd_accion(cmd, sizeof(cmd), uid, positiva ? ANCS_ACCION_POSITIVA : ANCS_ACCION_NEGATIVA);
    int rc = ble_gattc_write_flat(conn, h_cp, cmd, n, NULL, NULL);
    if (rc) {
        ESP_LOGW(TAG, "acción sobre %lu falló rc=%d", (unsigned long)uid, rc);
    }
    return rc == 0;
}

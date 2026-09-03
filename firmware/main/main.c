// Tarea principal del reloj: arranque del hardware, cola de eventos BLE, sondeo del PMIC y control de la pantalla.
// Es la única tarea que toca LVGL (siempre bajo pantalla_lock).
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "bsp/esp-bsp.h"
#include "pantalla.h"
#include "pmic.h"
#include "rtc_pcf85063.h"
#include "boton_boot.h"
#include "ble.h"
#include "ancs.h"
#include "ancs_proto.h"
#include "notif_store.h"
#include "ui.h"
#include "sdkconfig.h"

static const char *TAG = "reloj";
static QueueHandle_t cola;
static int64_t t_apagado_ms;
static SemaphoreHandle_t mtx_notif;

static void notif_lock(void) { xSemaphoreTake(mtx_notif, portMAX_DELAY); }
static void notif_unlock(void) { xSemaphoreGive(mtx_notif); }

static void log_ram(const char *momento)
{
    ESP_LOGI(TAG, "RAM libre %s: %u B (bloque mayor %u B, mínimo histórico %u B)", momento,
             (unsigned)esp_get_free_heap_size(), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
             (unsigned)esp_get_minimum_free_heap_size());
}

static int64_t ahora_ms(void) { return esp_timer_get_time() / 1000; }

static void aplicar_hora(const struct tm *t)
{
    struct tm copia = *t;
    struct timeval tv = { .tv_sec = mktime(&copia) };
    settimeofday(&tv, NULL);
    if (rtc_pcf85063_set(t) == ESP_OK) {
        ESP_LOGI(TAG, "hora guardada en el RTC");
    }
}

static void cb_olvidar(void)
{
    ble_olvidar();
}

// Desde la UI (tarea LVGL): "Descartar" quita la notificación también en el iPhone.
static void cb_descartar(uint32_t uid)
{
    ancs_accion(uid, false);
    notif_store_quitar(uid);
    ui_notifs_refrescar();   // ya estamos en la tarea LVGL, con su mutex tomado
}

static void cb_llamada(uint32_t uid, bool contestar)
{
    ESP_LOGI(TAG, "llamada %lu: %s", (unsigned long)uid, contestar ? "contestar" : "rechazar");
    ancs_accion(uid, contestar);
}

static void refrescar_notifs(void)
{
    pantalla_lock(0);
    ui_notifs_refrescar();
    pantalla_unlock();
}

static void evento_notif(const ble_evento_t *ev)
{
    notif_t n;
    bool existe = notif_store_buscar(ev->notif.uid, &n);
    switch (ev->notif.tipo) {
    case BLE_NOTIF_NUEVA:
        refrescar_notifs();
        if (existe && n.categoria == ANCS_CAT_LLAMADA && !(n.flags & ANCS_FLAG_PREEXISTENTE)) {
            pantalla_encender();
            pantalla_lock(0);
            ui_llamada(ev->notif.uid, true);
            pantalla_unlock();
        }
        break;
    case BLE_NOTIF_MODIFICADA:
    case BLE_NOTIF_APP:
        refrescar_notifs();
        break;
    case BLE_NOTIF_DETALLE:
        refrescar_notifs();
        if (ev->notif.aviso && existe && n.categoria != ANCS_CAT_LLAMADA) {
            pantalla_encender();
            pantalla_lock(0);
            ui_aviso(ev->notif.uid);
            pantalla_unlock();
        } else if (existe && n.categoria == ANCS_CAT_LLAMADA) {
            pantalla_lock(0);
            uint32_t en_curso;
            if (ui_en_llamada(&en_curso) && en_curso == ev->notif.uid) {
                ui_llamada(ev->notif.uid, true);   // ahora con el nombre del que llama
            }
            pantalla_unlock();
        }
        break;
    case BLE_NOTIF_QUITADA:
        pantalla_lock(0);
        ui_llamada(ev->notif.uid, false);
        ui_notifs_refrescar();
        pantalla_unlock();
        break;
    }
}

static void apagar_pantalla(void)
{
    pantalla_lock(0);
    ui_ir_reloj();   // así el toque que la despierta no cae sobre ningún botón
    pantalla_unlock();
    pantalla_apagar();
    t_apagado_ms = ahora_ms();
}

static void refrescar_bateria(void)
{
    pantalla_lock(0);
    ui_bateria(pmic_bateria_pct(), pmic_cargando(), pmic_usb_conectado());
    ui_ram((unsigned)(esp_get_free_heap_size() / 1024));
    pantalla_unlock();
    ESP_LOGI(TAG, "latido: RAM libre %u B, batería %d %% (%d mV), USB %s, BLE %d, pantalla %s",
             (unsigned)esp_get_free_heap_size(), pmic_bateria_pct(), pmic_bateria_mv(),
             pmic_usb_conectado() ? "sí" : "no", (int)ble_estado(), pantalla_encendida() ? "on" : "off");
    ESP_LOGI(TAG, "notificaciones: %d guardadas, %d sin leer, ANCS %s", notif_store_cantidad(), notif_store_no_leidas(),
             ancs_listo() ? "suscrito" : "no");
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    setenv("TZ", CONFIG_RELOJ_TZ, 1);
    tzset();
    log_ram("al arrancar");

    cola = xQueueCreate(16, sizeof(ble_evento_t));
    mtx_notif = xSemaphoreCreateMutex();
    notif_store_set_lock(notif_lock, notif_unlock);
    notif_store_init();
    ESP_ERROR_CHECK(pantalla_init());
    log_ram("tras pantalla + LVGL");

    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (pmic_init(bus) != ESP_OK) {
        ESP_LOGW(TAG, "sin PMIC: no habrá batería ni botón PWR");
    }
    boton_boot_init();
    struct tm t;
    if (rtc_pcf85063_init(bus) == ESP_OK && rtc_pcf85063_get(&t) == ESP_OK) {
        struct timeval tv = { .tv_sec = mktime(&t) };
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "hora del RTC: %04d-%02d-%02d %02d:%02d:%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                 t.tm_hour, t.tm_min, t.tm_sec);
    } else {
        ESP_LOGW(TAG, "RTC sin hora fiable; la pondrá el iPhone al vincular");
    }

    pantalla_lock(0);
    ui_init(CONFIG_RELOJ_NOMBRE_BLE, pantalla_brillo_actual());
    ui_al_olvidar(cb_olvidar);
    ui_al_cambiar_brillo(pantalla_brillo);
    ui_al_descartar(cb_descartar);
    ui_al_llamada(cb_llamada);
    pantalla_unlock();
    refrescar_bateria();
    log_ram("con la UI creada");

    ESP_ERROR_CHECK(ble_init(CONFIG_RELOJ_NOMBRE_BLE, cola));
    vTaskDelay(pdMS_TO_TICKS(300));
    log_ram("tras NimBLE");
    pantalla_lock(0);
    ui_ble(ble_estado(), ble_tiene_bond(), ble_direccion());
    pantalla_unlock();

    time_t ultimo_seg = 0;
    int64_t ultimo_bat = 0;
    ble_evento_t ev;
    while (true) {
        if (xQueueReceive(cola, &ev, pdMS_TO_TICKS(250))) {
            switch (ev.tipo) {
            case BLE_EV_ESTADO:
                pantalla_lock(0);
                ui_ble(ev.estado, ble_tiene_bond(), ble_direccion());
                pantalla_unlock();
                break;
            case BLE_EV_HORA:
                aplicar_hora(&ev.hora);
                ultimo_seg = 0;
                break;
            case BLE_EV_NOTIF:
                evento_notif(&ev);
                break;
            case BLE_EV_ANCS:
                ESP_LOGI(TAG, "notificaciones del iPhone %s", ev.ancs_listo ? "activas" : "inactivas");
                break;
            }
        }

        // Botón PWR (llega por el PMIC) y eventos de USB/carga.
        pmic_eventos_t pe;
        pmic_sondear(&pe);
        if (pe.pwr_corto || pe.pwr_largo) {
            ESP_LOGI(TAG, "botón PWR %s", pe.pwr_largo ? "largo" : "corto");
        }
        if (pe.pwr_corto) {
            if (pantalla_encendida()) {
                apagar_pantalla();
            } else {
                pantalla_encender();
            }
        }
        if (pe.pwr_largo) {
            pantalla_encender();
            pantalla_lock(0);
            ui_ir_ajustes();
            pantalla_unlock();
        }
        if (pe.usb_puesto || pe.usb_quitado || pe.carga_inicio || pe.carga_fin) {
            refrescar_bateria();
        }

        // Reloj y batería.
        time_t ahora = time(NULL);
        if (ahora != ultimo_seg) {
            ultimo_seg = ahora;
            localtime_r(&ahora, &t);
            pantalla_lock(0);
            ui_hora(&t);
            pantalla_unlock();
        }
        if (ahora_ms() - ultimo_bat > 10000) {
            ultimo_bat = ahora_ms();
            refrescar_bateria();
        }

        // Pantalla: se apaga sola por inactividad y despierta con un toque.
        uint32_t sin_tocar = pantalla_ms_sin_tocar();
        if (pantalla_encendida()) {
            if (sin_tocar > (uint32_t)CONFIG_RELOJ_PANTALLA_SEGUNDOS * 1000u) {
                apagar_pantalla();
            }
        } else if ((int64_t)sin_tocar < ahora_ms() - t_apagado_ms) {
            pantalla_encender();   // hubo un toque después de apagarla
        }
    }
}

// Tarea principal del reloj: arranque del hardware, cola de eventos BLE, sondeo del PMIC y control de la pantalla.
// Es la única tarea que toca LVGL (siempre bajo pantalla_lock).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "bsp/esp-bsp.h"
#include "pantalla.h"
#include "pmic.h"
#include "imu.h"
#include "rtc_pcf85063.h"
#include "boton_boot.h"
#include "ble.h"
#include "ancs.h"
#include "ancs_proto.h"
#include "notif_store.h"
#include "ui.h"
#include "red.h"
#include "andia.h"
#include "voz.h"
#include "bateria_registro.h"
#include "energia.h"
#include "sdkconfig.h"

static const char *TAG = "reloj";
static QueueHandle_t cola;
static QueueHandle_t cola_andia;
static int64_t t_encendido_ms;      // último encendido por evento (notificación, botón): cuenta como actividad
typedef enum { MOTIVO_USUARIO, MOTIVO_AVISO } motivo_t;   // por qué se encendió la pantalla
static bool pantalla_por_usuario = true;                  // arranque = usuario
static SemaphoreHandle_t mtx_notif;
static bateria_registro_t reg_bat;   // una muestra por minuto: descarga en %/h en "Acerca de"

static void notif_lock(void) { xSemaphoreTake(mtx_notif, portMAX_DELAY); }
static void notif_unlock(void) { xSemaphoreGive(mtx_notif); }

static void encender_pantalla(motivo_t motivo);
static void energia_refrescar(void);

static void log_ram(const char *momento)
{
    ESP_LOGI(TAG, "RAM libre %s: %u B (bloque mayor %u B, mínimo histórico %u B)", momento,
             (unsigned)esp_get_free_heap_size(), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
             (unsigned)esp_get_minimum_free_heap_size());
}

static int64_t ahora_ms(void) { return esp_timer_get_time() / 1000; }

#ifdef CONFIG_RELOJ_CONTROL_USB
// Control remoto por el USB-Serial/JTAG para grabar demos sin tocar el reloj (ver Kconfig). Los gestos los anima
// esta tarea con el dedo virtual; los botones y el encendido los ejecuta la tarea principal (cola_ctl).
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
typedef enum { CTL_PWR, CTL_PWRL, CTL_ENCENDER } ctl_t;
static QueueHandle_t cola_ctl;

static bool ctl_pantalla(void)
{
    if (!pantalla_encendida()) {
        ctl_t c = CTL_ENCENDER;
        xQueueSend(cola_ctl, &c, 0);
        for (int i = 0; i < 40 && !pantalla_encendida(); i++) vTaskDelay(pdMS_TO_TICKS(50));
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    return pantalla_encendida();
}

static void ctl_gesto(int x1, int y1, int x2, int y2, int ms)
{
    int pasos = ms / 15 > 0 ? ms / 15 : 1;
    pantalla_dedo_virtual(true, x1, y1);
    vTaskDelay(pdMS_TO_TICKS(80));
    for (int i = 1; i <= pasos && ms > 0; i++) {
        pantalla_dedo_virtual(true, x1 + (x2 - x1) * i / pasos, y1 + (y2 - y1) * i / pasos);
        vTaskDelay(pdMS_TO_TICKS(15));
    }
    vTaskDelay(pdMS_TO_TICKS(60));
    pantalla_dedo_virtual(false, x2, y2);
}

static void ctl_ejecutar(const char *l)
{
    int a, b, c, d, e;
    char dir[8];
    ctl_t cmd;
    if (strcmp(l, "pwr") == 0 || strcmp(l, "pwrl") == 0) {
        cmd = l[3] ? CTL_PWRL : CTL_PWR;
        xQueueSend(cola_ctl, &cmd, 0);
    } else if (sscanf(l, "tap %d %d", &a, &b) == 2) {
        if (ctl_pantalla()) ctl_gesto(a, b, a, b, 0);
    } else if (sscanf(l, "drag %d %d %d %d %d", &a, &b, &c, &d, &e) == 5) {
        if (ctl_pantalla()) ctl_gesto(a, b, c, d, e);
    } else if (sscanf(l, "swipe %7s", dir) == 1) {
        if (!ctl_pantalla()) return;
        // En vertical, por la derecha: el centro de la carátula tiene el botón de ANDIA.
        if (strcmp(dir, "up") == 0) ctl_gesto(340, 400, 340, 120, 220);
        else if (strcmp(dir, "down") == 0) ctl_gesto(340, 100, 340, 380, 220);
        else if (strcmp(dir, "left") == 0) ctl_gesto(350, 251, 60, 251, 220);
        else if (strcmp(dir, "right") == 0) ctl_gesto(60, 251, 350, 251, 220);
        else { ESP_LOGW(TAG, "control: dirección desconocida '%s'", dir); return; }
    } else {
        ESP_LOGW(TAG, "control: orden desconocida '%s'", l);
        return;
    }
    ESP_LOGI(TAG, "control: %s", l);
}

static void tarea_control(void *arg)
{
    (void)arg;
    char linea[64];
    int n = 0;
    while (true) {
        uint8_t ch;
        if (usb_serial_jtag_read_bytes(&ch, 1, portMAX_DELAY) != 1) continue;
        if (ch == '\r') continue;
        if (ch != '\n') {
            if (n < (int)sizeof(linea) - 1) linea[n++] = (char)ch;
            continue;
        }
        linea[n] = 0;
        n = 0;
        if (linea[0]) ctl_ejecutar(linea);
    }
}

static void control_usb_iniciar(void)
{
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    if (usb_serial_jtag_driver_install(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "control: no se pudo instalar el driver USB-Serial/JTAG");
        return;
    }
    usb_serial_jtag_vfs_use_driver();
    cola_ctl = xQueueCreate(4, sizeof(ctl_t));
    xTaskCreate(tarea_control, "control_usb", 3072, NULL, 3, NULL);
    ESP_LOGI(TAG, "control: órdenes por USB activadas (tap, swipe, drag, pwr, pwrl)");
}
#endif

// Desde cuándo está sin cargador, para leer la descarga de una noche en "Acerca de" sin sacar cuentas.
static int64_t t_sin_usb_ms = -1;   // -1 = con USB
static bool sin_usb_desde_arranque; // arrancó ya sin USB: no se sabe cuándo se desconectó
static int pct_sin_usb, mv_sin_usb;

static void marcar_usb(bool conectado, bool al_arrancar)
{
    if (conectado) {
        t_sin_usb_ms = -1;
    } else if (t_sin_usb_ms < 0) {
        t_sin_usb_ms = ahora_ms();
        sin_usb_desde_arranque = al_arrancar;
        pct_sin_usb = pmic_bateria_pct();
        mv_sin_usb = pmic_bateria_mv();
    }
}

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

// ---- ANDIA: conversación, avisos como notificaciones y temporizador local ----
static uint32_t uid_local = 0xA0000000;
static esp_timer_handle_t timer_andia;

static void cb_microfono(void)
{
    encender_pantalla(MOTIVO_USUARIO);
    esp_err_t r = andia_conversar(false);
    if (r == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "ANDIA ya está en una conversación");
    } else if (r != ESP_OK) {
        ESP_LOGW(TAG, "no se pudo iniciar la conversación: %s", esp_err_to_name(r));
    }
}

static void cb_cancelar_andia(void)
{
    andia_cancelar();
}

static void pitidos(int n);

// ---- Ajustes del usuario (persisten en NVS, espacio "ajustes") ----
// "Oye Andia" solo si el usuario lo activa: impide el reposo (micrófono y WiFi siempre encendidos).
static struct { int volumen, brillo, tiempo_s, sensibilidad; bool vigilia; } aj = { 60, 60, 20, 100, false };

static void ajustes_cargar(void)
{
    nvs_handle_t h;
    if (nvs_open("ajustes", NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    int32_t v;
    if (nvs_get_i32(h, "volumen", &v) == ESP_OK) aj.volumen = v;
    if (nvs_get_i32(h, "brillo", &v) == ESP_OK) aj.brillo = v;
    if (nvs_get_i32(h, "tiempo", &v) == ESP_OK) aj.tiempo_s = v;
    if (nvs_get_i32(h, "sensib", &v) == ESP_OK) aj.sensibilidad = v;
    if (nvs_get_i32(h, "vigilia", &v) == ESP_OK) aj.vigilia = v != 0;   // se guarda como i32 (ajustes_guardar); leerlo como u8 fallaba por tipo
    nvs_close(h);
}

static void ajustes_guardar(const char *clave, int valor)
{
    nvs_handle_t h;
    if (nvs_open("ajustes", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_i32(h, clave, valor);
        nvs_commit(h);
        nvs_close(h);
    }
}

static float ganancia_db(int pct) { return 12.0f + 25.5f * pct / 100.0f; }   // 12-37,5 dB del ES7210

// "Oye Andia" siempre: el micrófono vigila en local y solo manda audio cuando hay voz cerca.
#define vigilia_deseada (aj.vigilia)

static void cb_vigilia(bool activa)
{
    aj.vigilia = activa;
    ajustes_guardar("vigilia", activa);
    andia_vigilia(activa);
    energia_refrescar();
}

static void cb_brillo(int pct) { aj.brillo = pct; ajustes_guardar("brillo", pct); pantalla_brillo(pct); }
static void cb_volumen(int pct) { aj.volumen = pct; ajustes_guardar("volumen", pct); voz_volumen(pct); }
static void cb_tiempo_pantalla(int s) { aj.tiempo_s = s; ajustes_guardar("tiempo", s); }
static void cb_sensibilidad(int pct) { aj.sensibilidad = pct; ajustes_guardar("sensib", pct); voz_ganancia_mic(ganancia_db(pct)); }
static void cb_probar_sonido(void) { pitidos(1); }

static void cb_apagar(void)
{
    ESP_LOGI(TAG, "apagado pedido desde Configuración");
    pmic_apagar();
}

static void refrescar_wifi_ui(void)
{
    ui_wifi(red_ssid(), red_ip(), red_en_casa(), andia_conectada(), red_rssi());
}

// Onda cuadrada de `hz` durante `ms` (≤ 150) directo al anillo de reproducción.
static void tono(int hz, int ms)
{
    static int16_t buf[VOZ_TASA * 150 / 1000];
    size_t n = (size_t)VOZ_TASA * ms / 1000;
    int medio_periodo = VOZ_TASA / hz / 2;
    for (size_t i = 0; i < n && i < sizeof(buf) / 2; i++) {
        buf[i] = (int16_t)(12000 * ((i / medio_periodo) % 2 ? 1 : -1));
    }
    voz_reproducir_alimentar((uint8_t *)buf, n * 2);
}

static void silencio(int ms)
{
    static const int16_t nada[VOZ_TASA * 100 / 1000];
    voz_reproducir_alimentar((const uint8_t *)nada, (size_t)VOZ_TASA * ms / 1000 * 2);
}

static void pitidos(int n)
{
    for (int k = 0; k < n; k++) {
        tono(880, 120);
        silencio(100);
    }
    voz_reproducir_fin();
}

static void aviso_local(const char *titulo, const char *mensaje, const char *origen)
{
    uint32_t uid = uid_local++;
    notif_store_agregar(uid, ANCS_CAT_OTRA, 0, (uint32_t)(esp_timer_get_time() / 1000));
    ancs_attrs_t at = { .uid = uid };
    snprintf(at.app_id, sizeof(at.app_id), "cl.reloj.andia");
    snprintf(at.titulo, sizeof(at.titulo), "%s", titulo);
    snprintf(at.mensaje, sizeof(at.mensaje), "%s", mensaje);
    time_t ahora = time(NULL);
    struct tm tt;
    localtime_r(&ahora, &tt);
    strftime(at.fecha, sizeof(at.fecha), "%Y%m%dT%H%M%S", &tt);
    notif_store_poner_detalle(uid, &at);
    encender_pantalla(MOTIVO_AVISO);
    pantalla_lock(0);
    ui_notifs_refrescar();
    ui_aviso(uid);
    pantalla_unlock();
    ESP_LOGI(TAG, "aviso (%s): %s", origen, mensaje);
}

static void al_vencer_temporizador(void *arg)
{
    andia_evento_t ev = { .tipo = ANDIA_EV_AVISO };
    snprintf(ev.texto, sizeof(ev.texto), "Se acabó el tiempo");
    snprintf(ev.origen, sizeof(ev.origen), "temporizador");
    xQueueSend(cola_andia, &ev, 0);
}

static void evento_andia(const andia_evento_t *ev)
{
    switch (ev->tipo) {
    case ANDIA_EV_RED:
        andia_red(ev->red_conectada);
        pantalla_lock(0);
        ui_red(ev->red_conectada, red_ip());
        refrescar_wifi_ui();
        pantalla_unlock();
        if (ev->red_conectada && vigilia_deseada) {
            andia_vigilia(true);
        }
        break;
    case ANDIA_EV_ESTADO:
        if (ev->estado != ANDIA_INACTIVA) {
            encender_pantalla(MOTIVO_AVISO);
        }
        pantalla_lock(0);
        if (ev->estado == ANDIA_ACTIVADA && !ui_en_andia()) {
            ui_ir_andia();   // la Pi confirmó "Oye Andia" desde la vigilia
        }
        ui_andia_estado(ev->estado);
        pantalla_unlock();
        break;
    case ANDIA_EV_SIGUE:            // dijiste solo el nombre: pitido y a escuchar la pregunta
        pitidos(1);
        encender_pantalla(MOTIVO_AVISO);
        break;
    case ANDIA_EV_PREGUNTA:
        pantalla_lock(0);
        ui_andia_pregunta(ev->texto);
        pantalla_unlock();
        break;
    case ANDIA_EV_RESPUESTA:
        encender_pantalla(MOTIVO_AVISO);
        pantalla_lock(0);
        ui_andia_respuesta(ev->texto);
        pantalla_unlock();
        break;
    case ANDIA_EV_FIN:
        pantalla_lock(0);
        ui_andia_estado(ANDIA_INACTIVA);
        if (strcmp(ev->texto, "sin_activacion") == 0 && ui_en_andia()) {
            ui_andia_error("No escuché mi nombre.");
        } else if (strcmp(ev->texto, "sin_voz") == 0) {
            ui_andia_error("No escuché nada.");
        }
        pantalla_unlock();
        break;
    case ANDIA_EV_ERROR:
        encender_pantalla(MOTIVO_AVISO);
        pantalla_lock(0);
        ui_andia_estado(ANDIA_INACTIVA);
        ui_andia_error(ev->texto);
        pantalla_unlock();
        break;
    case ANDIA_EV_AVISO:
        if (strcmp(ev->origen, "temporizador") == 0) {
            pitidos(3);
            aviso_local("Temporizador", ev->texto, ev->origen);
        } else {
            pitidos(1);
            aviso_local(strcmp(ev->origen, "recordatorio") == 0 ? "Recordatorio" : "ANDIA", ev->texto, ev->origen);
        }
        break;
    case ANDIA_EV_TEMPORIZADOR:
        if (!timer_andia) {
            const esp_timer_create_args_t a = { .callback = al_vencer_temporizador, .name = "temporizador" };
            esp_timer_create(&a, &timer_andia);
        }
        esp_timer_stop(timer_andia);
        esp_timer_start_once(timer_andia, (uint64_t)ev->segundos * 1000000ULL);
        ESP_LOGI(TAG, "temporizador de %d s", ev->segundos);
        break;
    }
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
            encender_pantalla(MOTIVO_AVISO);
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
            encender_pantalla(MOTIVO_AVISO);
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

static void encender_pantalla(motivo_t motivo)
{
    t_encendido_ms = ahora_ms();                  // cada evento cuenta como actividad
    if (motivo == MOTIVO_USUARIO) {
        pantalla_por_usuario = true;
    }
    if (pantalla_encendida()) {
        return;
    }
    pantalla_por_usuario = (motivo == MOTIVO_USUARIO);
    // La hora al día antes de que el panel se vea (LVGL aún parado: solo cambia el texto; dibuja al reanudar).
    time_t ahora = time(NULL);
    struct tm t;
    localtime_r(&ahora, &t);
    pantalla_lock(0);
    ui_hora(&t);
    pantalla_unlock();
    pantalla_encender();
    energia_refrescar();
}

static void apagar_pantalla(void)
{
    pantalla_lock(0);
    ui_ir_reloj();   // así el toque que la despierta no cae sobre ningún botón
    pantalla_unlock();
    pantalla_apagar();
    energia_refrescar();
}

// Le cuenta al gestor de energía el estado actual; barato, se llama en cada vuelta del bucle y tras cada cambio.
static void energia_refrescar(void)
{
    energia_entradas_t e = {
        .usb = pmic_usb_conectado(),
        .vigilia = aj.vigilia,
        .pantalla = pantalla_encendida(),
        .pantalla_por_usuario = pantalla_por_usuario,
        .conversacion = andia_estado() != ANDIA_INACTIVA,
    };
    energia_actualizar(&e);
}

static void texto_descarga(char *b, size_t cap, int minutos)
{
    int d;
    if (bateria_registro_descarga(&reg_bat, minutos, &d)) {
        snprintf(b, cap, "%s%d,%d %%/h", d < 0 ? "-" : "", abs(d) / 10, abs(d) % 10);
    } else {
        snprintf(b, cap, "--");
    }
}

static const char *motivo_arranque(void)
{
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "encendido";
    case ESP_RST_SW:       return "software";
    case ESP_RST_PANIC:    return "panic";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_USB:      return "USB";
    case ESP_RST_JTAG:     return "JTAG";
    case ESP_RST_EXT:      return "externo";
    default:               return "otro";
    }
}

// "hace N min: 72 %" o "hace N min: --" si aún no hay muestra.
static void texto_hace(char *b, size_t cap, int minutos)
{
    uint16_t mv;
    uint8_t pct;
    if (bateria_registro_muestra(&reg_bat, minutos, &mv, &pct)) {
        snprintf(b, cap, "%u %%", pct);
    } else {
        snprintf(b, cap, "--");
    }
}

// Texto de energía para "Acerca de" (una muestra de batería por minuto; ver bateria_registro). El medidor del
// AXP2101 marca 100 % mientras carga y cae a la carga real minutos después de quitar el USB: por eso se muestran
// los mV y el % de hace 30 min y 2 h, para juzgar la ventana.
static void refrescar_energia_ui(void)
{
    char a[24], c[24], h30[12], h120[12], r[64], u[112];
    static char t[400];   // estática: la tarea main tiene poca pila
    uint32_t rs, rn;
    int rp;
    if (energia_ultimo_reposo(&rs, &rp, &rn)) {
        snprintf(r, sizeof(r), "\nÚltimo reposo %lu min: dormido %d %% (%lu sueños)", (unsigned long)(rs / 60), rp, (unsigned long)rn);
    } else {
        r[0] = 0;
    }
    if (t_sin_usb_ms < 0) {
        snprintf(u, sizeof(u), "\nCon cargador");
    } else {
        int64_t min = (ahora_ms() - t_sin_usb_ms) / 60000;
        int bajada = pct_sin_usb - pmic_bateria_pct();
        char media[24] = "--";
        if (min >= 30) {
            int x10 = (int)(bajada * 600 / min);   // décimas de %/h
            snprintf(media, sizeof(media), "%d,%d %%/h", x10 / 10, x10 < 0 ? -x10 % 10 : x10 % 10);
        }
        snprintf(u, sizeof(u), "\nSin cargador hace %d h %02d min%s, desde %d %% (%d mV) · media %s",
                 (int)(min / 60), (int)(min % 60), sin_usb_desde_arranque ? " (desde el arranque)" : "",
                 pct_sin_usb, mv_sin_usb, media);
    }
    texto_descarga(a, sizeof(a), 30);
    texto_descarga(c, sizeof(c), 120);
    texto_hace(h30, sizeof(h30), 30);
    texto_hace(h120, sizeof(h120), 120);
    snprintf(t, sizeof(t), "Modo %s desde hace %lu min%s\nBat %d %% %d mV · hace 30 min %s · 2 h %s\nDescarga %s (30 min) · %s (2 h)%s\nArranque hace %lu min (%s)",
             energia_modo_nombre(energia_modo()), (unsigned long)(energia_segundos_en_modo() / 60), r,
             pmic_bateria_pct(), pmic_bateria_mv(), h30, h120, a, c, u,
             (unsigned long)(esp_timer_get_time() / 60000000), motivo_arranque());
    if (pantalla_encendida()) {
        pantalla_lock(0);
        ui_energia(t);
        pantalla_unlock();
    }
}

static void refrescar_bateria(void)
{
    if (pantalla_encendida()) {
        pantalla_lock(0);
        ui_bateria(pmic_bateria_pct(), pmic_cargando(), pmic_usb_conectado());
        ui_ram((unsigned)(esp_get_free_heap_size() / 1024));
        pantalla_unlock();
    }
    ESP_LOGI(TAG, "latido: RAM libre %u B (bloque mayor %u B), batería %d %% (%d mV), USB %s, BLE %d, pantalla %s, WiFi %d dBm",
             (unsigned)esp_get_free_heap_size(), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
             pmic_bateria_pct(), pmic_bateria_mv(),
             pmic_usb_conectado() ? "sí" : "no", (int)ble_estado(), pantalla_encendida() ? "on" : "off", red_rssi());
    red_permitir_ahorro(!pmic_usb_conectado());
    ESP_LOGI(TAG, "notificaciones: %d guardadas, %d sin leer, ANCS %s; WiFi %s (%s)", notif_store_cantidad(),
             notif_store_no_leidas(), ancs_listo() ? "suscrito" : "no", red_conectada() ? red_ssid() : "sin red", red_ip());
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
    ajustes_cargar();
    bateria_registro_init(&reg_bat);
    ESP_LOGI(TAG, "arranque por %s", motivo_arranque());
    log_ram("al arrancar");

    cola = xQueueCreate(16, sizeof(ble_evento_t));
    mtx_notif = xSemaphoreCreateMutex();
    notif_store_set_lock(notif_lock, notif_unlock);
    notif_store_init();
    ESP_ERROR_CHECK(pantalla_init());
    pantalla_brillo(aj.brillo);
    log_ram("tras pantalla + LVGL");

    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (pmic_init(bus) != ESP_OK) {
        ESP_LOGW(TAG, "sin PMIC: no habrá batería ni botón PWR");
    }
    boton_boot_init();
    imu_init(bus);
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
    ui_al_cambiar_brillo(cb_brillo);
    ui_al_descartar(cb_descartar);
    ui_al_llamada(cb_llamada);
    ui_al_microfono(cb_microfono);
    ui_al_cancelar_andia(cb_cancelar_andia);
    ui_al_vigilia(cb_vigilia);
    ui_al_cambiar_volumen(cb_volumen);
    ui_al_probar_sonido(cb_probar_sonido);
    ui_al_cambiar_tiempo_pantalla(cb_tiempo_pantalla);
    ui_al_cambiar_sensibilidad(cb_sensibilidad);
    ui_al_apagar(cb_apagar);
    ui_ajustes(aj.volumen, aj.tiempo_s, aj.sensibilidad);
    ui_vigilia(vigilia_deseada);
    pantalla_unlock();
    marcar_usb(pmic_usb_conectado(), true);
    refrescar_bateria();
    log_ram("con la UI creada");
#ifdef CONFIG_RELOJ_CONTROL_USB
    control_usb_iniciar();
#endif

    ESP_ERROR_CHECK(ble_init(CONFIG_RELOJ_NOMBRE_BLE, cola));
    vTaskDelay(pdMS_TO_TICKS(300));
    log_ram("tras NimBLE");

    if (voz_init() != ESP_OK) {
        ESP_LOGW(TAG, "audio no disponible");
    }
    voz_volumen(aj.volumen);
    voz_ganancia_mic(ganancia_db(aj.sensibilidad));
    log_ram("tras el códec de audio");
    cola_andia = xQueueCreate(6, sizeof(andia_evento_t));
    if (red_init(cola_andia) != ESP_OK) {
        ESP_LOGW(TAG, "WiFi no arrancó");
    }
    ESP_ERROR_CHECK(energia_init());
    energia_refrescar();   // arranque = pantalla por el usuario: WiFi encendido
    if (andia_init(cola_andia) != ESP_OK) {
        ESP_LOGW(TAG, "cliente ANDIA no disponible");
    }
    log_ram("tras arrancar WiFi");
    pantalla_lock(0);
    ui_ble(ble_estado(), ble_tiene_bond(), ble_direccion());
    pantalla_unlock();

    time_t ultimo_seg = 0;
    int64_t ultimo_bat = 0, ultimo_wifi_ui = 0, ultimo_muestra = -60000;
    ble_evento_t ev;
    while (true) {
        if (xQueueReceive(cola, &ev, pdMS_TO_TICKS(ui_en_sensores() ? 100 : 250))) {
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

        // Eventos de ANDIA / red (sin bloquear).
        andia_evento_t ea;
        while (cola_andia && xQueueReceive(cola_andia, &ea, 0)) {
            evento_andia(&ea);
        }

        // Botón PWR (llega por el PMIC) y eventos de USB/carga.
        pmic_eventos_t pe;
        pmic_sondear(&pe);
        if (pe.pwr_corto || pe.pwr_largo) {
            ESP_LOGI(TAG, "botón PWR %s", pe.pwr_largo ? "largo" : "corto");
        }
#ifdef CONFIG_RELOJ_CONTROL_USB
        ctl_t cc;
        while (cola_ctl && xQueueReceive(cola_ctl, &cc, 0)) {
            if (cc == CTL_PWR) pe.pwr_corto = true;
            else if (cc == CTL_PWRL) pe.pwr_largo = true;
            else if (!pantalla_encendida()) encender_pantalla(MOTIVO_USUARIO);
        }
#endif
        if (pe.pwr_corto) {
            if (pantalla_encendida()) {
                apagar_pantalla();
            } else {
                encender_pantalla(MOTIVO_USUARIO);
            }
        }
        if (pe.pwr_largo) {
            pantalla_lock(0);
            ui_ir_andia();
            pantalla_unlock();
            cb_microfono();
        }
        if (pe.usb_puesto || pe.usb_quitado || pe.carga_inicio || pe.carga_fin) {
            marcar_usb(pmic_usb_conectado(), false);
            refrescar_bateria();
            red_permitir_ahorro(!pmic_usb_conectado());
        }

        // App "Sensores": mientras está abierta, sensores encendidos, leer el IMU y refrescar (el bucle espera 100 ms).
        static bool imu_activo;
        if (ui_en_sensores() != imu_activo) {
            imu_activo = ui_en_sensores();
            imu_activar(imu_activo);
        }
        if (ui_en_sensores()) {
            float ax, ay, az, gx, gy, gz;
            if (imu_leer(&ax, &ay, &az, &gx, &gy, &gz)) {
                pantalla_lock(0);
                ui_sensores(ax, ay, az, gx, gy, gz);
                pantalla_unlock();
            }
        }
        if (pantalla_encendida() && ahora_ms() - ultimo_wifi_ui > 5000) {
            ultimo_wifi_ui = ahora_ms();
            pantalla_lock(0);
            refrescar_wifi_ui();
            pantalla_unlock();
        }

        // Traza de calibración del táctil: coordenadas mientras hay un dedo apoyado (cada 250 ms).
        int tx, ty;
        if (pantalla_punto_tocado(&tx, &ty)) {
            ESP_LOGD(TAG, "toque en (%d, %d)", tx, ty);
        }

        // Reloj y batería.
        time_t ahora = time(NULL);
        if (pantalla_encendida() && ahora != ultimo_seg) {
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
        if (ahora_ms() - ultimo_muestra >= 60000) {
            ultimo_muestra = ahora_ms();
            if (pmic_bateria_pct() >= 0) {
                bateria_registro_agregar(&reg_bat, (uint16_t)pmic_bateria_mv(), (uint8_t)pmic_bateria_pct());
            }
            refrescar_energia_ui();
        }

        // Toque: enciende la pantalla apagada; en una pantalla encendida por un aviso, pasa a ser "por el usuario".
        if (pantalla_toque_pendiente()) {
            if (!pantalla_encendida()) {
                encender_pantalla(MOTIVO_USUARIO);
            } else {
                pantalla_por_usuario = true;
            }
        }
        // Apagado por inactividad (con LVGL en marcha su cuenta de inactividad es fiable).
        int64_t limite = (int64_t)aj.tiempo_s * 1000;
        if (pantalla_encendida() && (int64_t)pantalla_ms_sin_tocar() > limite && ahora_ms() - t_encendido_ms > limite) {
            apagar_pantalla();
        }
        energia_refrescar();
    }
}

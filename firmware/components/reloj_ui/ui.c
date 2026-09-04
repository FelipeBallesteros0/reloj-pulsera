#include "ui.h"
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "esp_app_desc.h"
#include "notif_store.h"
#include "apps.h"

LV_FONT_DECLARE(fuente_16);
LV_FONT_DECLARE(fuente_20);
LV_FONT_DECLARE(fuente_24);
LV_FONT_DECLARE(fuente_72);
LV_FONT_DECLARE(emoji_20);

// Copias en RAM de las fuentes de texto con la fuente de emojis como respaldo (las generadas son const, en flash).
static lv_font_t f16, f20, f24, femoji;
#define F16 (&f16)
#define F20 (&f20)
#define F24 (&f24)

static const char *DIAS[] = { "domingo", "lunes", "martes", "miércoles", "jueves", "viernes", "sábado" };
static const char *MESES[] = { "enero", "febrero", "marzo", "abril", "mayo", "junio", "julio", "agosto",
                               "septiembre", "octubre", "noviembre", "diciembre" };

static lv_obj_t *scr_reloj, *scr_ajustes, *scr_lista, *scr_detalle, *scr_llamada;
static lv_obj_t *lbl_iconos, *lbl_hora, *lbl_fecha, *lbl_notifs, *lbl_pie;
static lv_obj_t *lbl_ble_estado, *lbl_ble_dir, *lbl_ble_bond, *lbl_acerca, *slider_brillo;
static lv_obj_t *lista_cont, *lbl_lista_vacia;
static lv_obj_t *det_cab, *det_titulo, *det_sub, *det_msg;
static lv_obj_t *llam_titulo, *llam_sub;
static lv_obj_t *popup, *pop_app, *pop_titulo, *pop_msg;
static lv_timer_t *popup_timer;
static uint32_t uid_detalle, uid_popup, uid_llamada;
static bool hay_llamada;
static lv_obj_t *scr_antes_llamada;

static ui_cb_t cb_olvidar;
static ui_cb_int_t cb_brillo;
static ui_cb_uid_t cb_descartar;
static ui_cb_llamada_t cb_llamada;
static char nombre[32];

static lv_color_t GRIS(void) { return lv_palette_main(LV_PALETTE_GREY); }
static lv_color_t AZUL(void) { return lv_palette_main(LV_PALETTE_BLUE); }

static lv_obj_t *etiqueta(lv_obj_t *padre, const lv_font_t *f, lv_color_t color, const char *texto)
{
    lv_obj_t *l = lv_label_create(padre);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, texto);
    return l;
}

static lv_obj_t *boton(lv_obj_t *padre, const char *texto, uint32_t color, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(padre);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_pad_hor(b, 22, 0);
    lv_obj_set_style_pad_ver(b, 12, 0);
    lv_obj_set_style_min_height(b, 52, 0);   // objetivo táctil cómodo para el dedo
    lv_obj_t *l = etiqueta(b, F20, lv_color_white(), texto);
    lv_obj_center(l);
    if (cb) {
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    }
    return b;
}

static void pantalla_negra(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
}

// "yyyyMMddTHHmmSS" → "HH:MM"; si no hay fecha, cadena vacía.
static void hora_de_fecha(const char *fecha, char *out, size_t cap)
{
    if (fecha && strlen(fecha) >= 13) {
        snprintf(out, cap, "%c%c:%c%c", fecha[9], fecha[10], fecha[11], fecha[12]);
    } else {
        snprintf(out, cap, "%s", "");
    }
}

static const char *nombre_app(const notif_t *n, char *buf, int cap) { return apps_nombre_o_bundle(n->app_id, buf, cap); }

// ---------------------------------------------------------------- gestos y navegación

// Convención del teléfono: deslizar hacia abajo (desde arriba) = notificaciones; hacia arriba = ajustes.
static void gesto_reloj(lv_event_t *e)
{
    lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_active());
    if (d == LV_DIR_BOTTOM) {
        ui_ir_lista();
    } else if (d == LV_DIR_TOP) {
        ui_ir_ajustes();
    }
}

static void gesto_ajustes(lv_event_t *e)
{
    if (lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_BOTTOM) {
        ui_ir_reloj();
    }
}

static void gesto_lista(lv_event_t *e)
{
    lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_active());
    if (d == LV_DIR_RIGHT || d == LV_DIR_TOP) {
        ui_ir_reloj();
    }
}

static void gesto_detalle(lv_event_t *e)
{
    if (lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_RIGHT) {
        ui_ir_lista();
    }
}

static void al_pulsar_volver_reloj(lv_event_t *e) { ui_ir_reloj(); }
static void al_pulsar_volver_lista(lv_event_t *e) { ui_ir_lista(); }

static lv_obj_t *btn_olvidar_lbl;
static lv_timer_t *olvidar_timer;

static void desarmar_olvidar(lv_timer_t *t)
{
    lv_label_set_text(btn_olvidar_lbl, "Olvidar iPhone");
    olvidar_timer = NULL;
}

static void al_pulsar_olvidar(lv_event_t *e)
{
    if (!olvidar_timer) {
        // Primer toque: pedir confirmación durante 5 s (evita borrar el bond por un roce).
        lv_label_set_text(btn_olvidar_lbl, "¿Seguro? Toca otra vez");
        olvidar_timer = lv_timer_create(desarmar_olvidar, 5000, NULL);
        lv_timer_set_repeat_count(olvidar_timer, 1);
        return;
    }
    lv_timer_delete(olvidar_timer);
    olvidar_timer = NULL;
    lv_label_set_text(btn_olvidar_lbl, "Olvidar iPhone");
    if (cb_olvidar) {
        cb_olvidar();
    }
}

static void al_mover_brillo(lv_event_t *e)
{
    if (cb_brillo) {
        cb_brillo((int)lv_slider_get_value(slider_brillo));
    }
}

static void al_pulsar_descartar(lv_event_t *e)
{
    uint32_t uid = uid_detalle;
    ui_ir_lista();
    if (cb_descartar) {
        cb_descartar(uid);
    }
}

static void al_pulsar_contestar(lv_event_t *e)
{
    if (cb_llamada) {
        cb_llamada(uid_llamada, true);
    }
}

static void al_pulsar_rechazar(lv_event_t *e)
{
    if (cb_llamada) {
        cb_llamada(uid_llamada, false);
    }
    ui_llamada(uid_llamada, false);
}

static void al_tocar_item(lv_event_t *e)
{
    lv_obj_t *b = lv_event_get_current_target(e);
    ui_ir_detalle((uint32_t)(uintptr_t)lv_obj_get_user_data(b));
}

static void al_tocar_popup(lv_event_t *e)
{
    lv_obj_add_flag(popup, LV_OBJ_FLAG_HIDDEN);
    ui_ir_detalle(uid_popup);
}

static void al_vencer_popup(lv_timer_t *t)
{
    lv_obj_add_flag(popup, LV_OBJ_FLAG_HIDDEN);
    popup_timer = NULL;
}

// ---------------------------------------------------------------- pantallas

static void crear_reloj(void)
{
    scr_reloj = lv_obj_create(NULL);
    pantalla_negra(scr_reloj);
    lv_obj_add_event_cb(scr_reloj, gesto_reloj, LV_EVENT_GESTURE, NULL);

    lbl_iconos = etiqueta(scr_reloj, &lv_font_montserrat_16, GRIS(), LV_SYMBOL_BLUETOOTH "  " LV_SYMBOL_BATTERY_EMPTY " --%");
    lv_obj_align(lbl_iconos, LV_ALIGN_TOP_MID, 0, 18);

    lbl_hora = etiqueta(scr_reloj, &fuente_72, lv_color_white(), "--:--");
    lv_obj_align(lbl_hora, LV_ALIGN_CENTER, 0, -40);

    lbl_fecha = etiqueta(scr_reloj, F20, GRIS(), "");
    lv_obj_align(lbl_fecha, LV_ALIGN_CENTER, 0, 20);

    lbl_notifs = etiqueta(scr_reloj, F20, AZUL(), "");
    lv_obj_align(lbl_notifs, LV_ALIGN_CENTER, 0, 70);

    lbl_pie = etiqueta(scr_reloj, F16, GRIS(), "Abajo: notificaciones · arriba: ajustes");
    lv_obj_align(lbl_pie, LV_ALIGN_BOTTOM_MID, 0, -24);
}

static void crear_ajustes(int brillo_pct)
{
    scr_ajustes = lv_obj_create(NULL);
    pantalla_negra(scr_ajustes);
    lv_obj_add_event_cb(scr_ajustes, gesto_ajustes, LV_EVENT_GESTURE, NULL);

    lv_obj_t *volver = boton(scr_ajustes, LV_SYMBOL_DOWN " Reloj", 0x222222, al_pulsar_volver_reloj);
    lv_obj_align(volver, LV_ALIGN_TOP_LEFT, 16, 12);

    lv_obj_t *titulo = etiqueta(scr_ajustes, F24, lv_color_white(), "Ajustes");
    lv_obj_align(titulo, LV_ALIGN_TOP_MID, 40, 22);

    lv_obj_t *l = etiqueta(scr_ajustes, F16, GRIS(), "Brillo");
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 20, 78);
    slider_brillo = lv_slider_create(scr_ajustes);
    lv_obj_set_size(slider_brillo, 360, 14);
    lv_obj_align(slider_brillo, LV_ALIGN_TOP_MID, 0, 108);
    lv_slider_set_range(slider_brillo, 5, 100);
    lv_slider_set_value(slider_brillo, brillo_pct, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider_brillo, al_mover_brillo, LV_EVENT_VALUE_CHANGED, NULL);

    l = etiqueta(scr_ajustes, F20, lv_color_white(), "Bluetooth");
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 20, 160);
    char buf[64];
    snprintf(buf, sizeof(buf), "Nombre: %s", nombre);
    l = etiqueta(scr_ajustes, F16, GRIS(), buf);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 20, 194);
    lbl_ble_estado = etiqueta(scr_ajustes, F16, lv_color_white(), "Anunciando…");
    lv_obj_align(lbl_ble_estado, LV_ALIGN_TOP_LEFT, 20, 220);
    lbl_ble_dir = etiqueta(scr_ajustes, F16, GRIS(), "Dirección: --");
    lv_obj_align(lbl_ble_dir, LV_ALIGN_TOP_LEFT, 20, 246);
    lbl_ble_bond = etiqueta(scr_ajustes, F16, GRIS(), "Emparejamiento guardado: no");
    lv_obj_align(lbl_ble_bond, LV_ALIGN_TOP_LEFT, 20, 272);

    lv_obj_t *olvidar = boton(scr_ajustes, "Olvidar iPhone", 0x7a1c1c, al_pulsar_olvidar);
    lv_obj_set_width(olvidar, 300);
    lv_obj_align(olvidar, LV_ALIGN_TOP_LEFT, 20, 306);
    btn_olvidar_lbl = lv_obj_get_child(olvidar, 0);
    l = etiqueta(scr_ajustes, F16, GRIS(), "(en el iPhone: Ajustes > Bluetooth > Omitir)");
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 20, 352);

    l = etiqueta(scr_ajustes, F20, lv_color_white(), "Acerca de");
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 20, 400);
    lbl_acerca = etiqueta(scr_ajustes, F16, GRIS(), "");
    lv_obj_align(lbl_acerca, LV_ALIGN_TOP_LEFT, 20, 434);
    lv_obj_t *pie = etiqueta(scr_ajustes, F16, GRIS(), "Desliza hacia abajo: reloj");
    lv_obj_align(pie, LV_ALIGN_BOTTOM_MID, 0, -12);
}

static void crear_lista(void)
{
    scr_lista = lv_obj_create(NULL);
    pantalla_negra(scr_lista);
    lv_obj_add_event_cb(scr_lista, gesto_lista, LV_EVENT_GESTURE, NULL);

    lv_obj_t *volver = boton(scr_lista, LV_SYMBOL_LEFT " Reloj", 0x222222, al_pulsar_volver_reloj);
    lv_obj_align(volver, LV_ALIGN_TOP_LEFT, 12, 10);
    lv_obj_t *titulo = etiqueta(scr_lista, F24, lv_color_white(), "Notificaciones");
    lv_obj_align(titulo, LV_ALIGN_TOP_MID, 50, 22);

    lista_cont = lv_obj_create(scr_lista);
    lv_obj_set_size(lista_cont, 410, 502 - 70);
    lv_obj_align(lista_cont, LV_ALIGN_TOP_MID, 0, 70);
    lv_obj_set_style_bg_opa(lista_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lista_cont, 0, 0);
    lv_obj_set_style_pad_all(lista_cont, 8, 0);
    lv_obj_set_style_pad_row(lista_cont, 8, 0);
    lv_obj_set_flex_flow(lista_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(lista_cont, LV_DIR_VER);

    lbl_lista_vacia = etiqueta(scr_lista, F20, GRIS(), "Sin notificaciones");
    lv_obj_align(lbl_lista_vacia, LV_ALIGN_CENTER, 0, 0);
}

static void crear_detalle(void)
{
    scr_detalle = lv_obj_create(NULL);
    pantalla_negra(scr_detalle);
    lv_obj_add_event_cb(scr_detalle, gesto_detalle, LV_EVENT_GESTURE, NULL);

    lv_obj_t *volver = boton(scr_detalle, LV_SYMBOL_LEFT "  Lista", 0x222222, al_pulsar_volver_lista);
    lv_obj_align(volver, LV_ALIGN_TOP_LEFT, 12, 10);
    lv_obj_t *descartar = boton(scr_detalle, LV_SYMBOL_TRASH "  Descartar", 0x7a1c1c, al_pulsar_descartar);
    lv_obj_set_width(descartar, 380);
    lv_obj_align(descartar, LV_ALIGN_BOTTOM_MID, 0, -12);

    det_cab = etiqueta(scr_detalle, F16, AZUL(), "");
    lv_obj_align(det_cab, LV_ALIGN_TOP_LEFT, 20, 74);
    det_titulo = etiqueta(scr_detalle, F24, lv_color_white(), "");
    lv_obj_set_width(det_titulo, 370);
    lv_label_set_long_mode(det_titulo, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(det_titulo, LV_ALIGN_TOP_LEFT, 20, 100);
    det_sub = etiqueta(scr_detalle, F16, GRIS(), "");
    lv_obj_set_width(det_sub, 370);
    lv_label_set_long_mode(det_sub, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align_to(det_sub, det_titulo, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);

    lv_obj_t *caja = lv_obj_create(scr_detalle);
    lv_obj_set_size(caja, 390, 502 - 210 - 80);
    lv_obj_align(caja, LV_ALIGN_TOP_MID, 0, 200);
    lv_obj_set_style_bg_opa(caja, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(caja, 0, 0);
    lv_obj_set_style_pad_all(caja, 10, 0);
    lv_obj_set_scroll_dir(caja, LV_DIR_VER);
    det_msg = etiqueta(caja, F20, lv_color_white(), "");
    lv_obj_set_width(det_msg, 370);
    lv_label_set_long_mode(det_msg, LV_LABEL_LONG_MODE_WRAP);
}

static void crear_llamada(void)
{
    scr_llamada = lv_obj_create(NULL);
    pantalla_negra(scr_llamada);
    lv_obj_t *l = etiqueta(scr_llamada, F20, GRIS(), "Llamada entrante");
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 70);
    llam_titulo = etiqueta(scr_llamada, F24, lv_color_white(), "");
    lv_obj_set_width(llam_titulo, 370);
    lv_obj_set_style_text_align(llam_titulo, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(llam_titulo, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(llam_titulo, LV_ALIGN_TOP_MID, 0, 120);
    llam_sub = etiqueta(scr_llamada, F16, GRIS(), "");
    lv_obj_align(llam_sub, LV_ALIGN_TOP_MID, 0, 200);

    lv_obj_t *b = boton(scr_llamada, LV_SYMBOL_CALL "  Contestar", 0x1f7a2e, al_pulsar_contestar);
    lv_obj_set_width(b, 300);
    lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, -120);
    b = boton(scr_llamada, LV_SYMBOL_CLOSE "  Rechazar", 0x7a1c1c, al_pulsar_rechazar);
    lv_obj_set_width(b, 300);
    lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, -50);
}

static void crear_popup(void)
{
    popup = lv_obj_create(lv_layer_top());
    lv_obj_set_size(popup, 394, 150);
    lv_obj_align(popup, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_bg_color(popup, lv_color_hex(0x1f2a3a), 0);
    lv_obj_set_style_border_color(popup, AZUL(), 0);
    lv_obj_set_style_border_width(popup, 2, 0);
    lv_obj_set_style_radius(popup, 16, 0);
    lv_obj_set_style_pad_all(popup, 12, 0);
    lv_obj_remove_flag(popup, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(popup, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(popup, al_tocar_popup, LV_EVENT_CLICKED, NULL);

    pop_app = etiqueta(popup, F16, AZUL(), "");
    lv_obj_align(pop_app, LV_ALIGN_TOP_LEFT, 0, 0);
    pop_titulo = etiqueta(popup, F20, lv_color_white(), "");
    lv_obj_set_width(pop_titulo, 366);
    lv_label_set_long_mode(pop_titulo, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(pop_titulo, LV_ALIGN_TOP_LEFT, 0, 26);
    pop_msg = etiqueta(popup, F16, GRIS(), "");
    lv_obj_set_width(pop_msg, 366);
    lv_obj_set_height(pop_msg, 70);
    lv_label_set_long_mode(pop_msg, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(pop_msg, LV_ALIGN_TOP_LEFT, 0, 54);
}

void ui_init(const char *nombre_ble, int brillo_pct)
{
    // Cadena de respaldo: texto → emojis → símbolos de LVGL (flechas, papelera, etc., que viven en Montserrat).
    femoji = emoji_20; femoji.fallback = &lv_font_montserrat_16;
    f16 = fuente_16; f16.fallback = &femoji;
    f20 = fuente_20; f20.fallback = &femoji;
    f24 = fuente_24; f24.fallback = &femoji;
    strncpy(nombre, nombre_ble, sizeof(nombre) - 1);
    crear_reloj();
    crear_ajustes(brillo_pct);
    crear_lista();
    crear_detalle();
    crear_llamada();
    crear_popup();
    ui_ram(0);
    lv_screen_load(scr_reloj);
}

// ---------------------------------------------------------------- carátula y ajustes

void ui_hora(const struct tm *t)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%02d:%02d", t->tm_hour, t->tm_min);
    lv_label_set_text(lbl_hora, buf);
    snprintf(buf, sizeof(buf), "%s %d de %s", DIAS[t->tm_wday % 7], t->tm_mday, MESES[t->tm_mon % 12]);
    lv_label_set_text(lbl_fecha, buf);
}

static const char *icono_bateria(int pct)
{
    if (pct < 0) return LV_SYMBOL_BATTERY_EMPTY;
    if (pct > 87) return LV_SYMBOL_BATTERY_FULL;
    if (pct > 62) return LV_SYMBOL_BATTERY_3;
    if (pct > 37) return LV_SYMBOL_BATTERY_2;
    if (pct > 12) return LV_SYMBOL_BATTERY_1;
    return LV_SYMBOL_BATTERY_EMPTY;
}

static ble_estado_t ble_actual = BLE_ANUNCIANDO;
static int bat_pct = -1;
static bool bat_cargando, bat_usb, red_ok;
static char red_ip_txt[16] = "--";
static unsigned ram_kb;

static void refrescar_iconos(void)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%s%s  %s%s  %s %s%d%%", LV_SYMBOL_BLUETOOTH, ble_actual == BLE_VINCULADO ? "" : "?",
             LV_SYMBOL_WIFI, red_ok ? "" : "?",
             icono_bateria(bat_pct), bat_cargando ? LV_SYMBOL_CHARGE " " : (bat_usb ? LV_SYMBOL_USB " " : ""),
             bat_pct < 0 ? 0 : bat_pct);
    lv_label_set_text(lbl_iconos, buf);
    lv_obj_set_style_text_color(lbl_iconos, (ble_actual == BLE_VINCULADO || red_ok) ? AZUL() : GRIS(), 0);
}

void ui_red(bool conectada, const char *ip)
{
    red_ok = conectada;
    snprintf(red_ip_txt, sizeof(red_ip_txt), "%s", ip ? ip : "--");
    refrescar_iconos();
    char buf[96];
    snprintf(buf, sizeof(buf), "Versión %s · RAM libre %u KB · WiFi %s", esp_app_get_description()->version, ram_kb, red_ip_txt);
    lv_label_set_text(lbl_acerca, buf);
}

void ui_bateria(int pct, bool cargando, bool usb)
{
    bat_pct = pct;
    bat_cargando = cargando;
    bat_usb = usb;
    refrescar_iconos();
}

void ui_ble(ble_estado_t estado, bool tiene_bond, const char *direccion)
{
    ble_actual = estado;
    refrescar_iconos();
    const char *txt = estado == BLE_VINCULADO ? "Vinculado con el iPhone"
                    : estado == BLE_CONECTADO ? "Conectado, cifrando…"
                    : (tiene_bond ? "Esperando al iPhone…" : "Visible: conéctate desde nRF Connect");
    lv_label_set_text(lbl_ble_estado, txt);
    lv_obj_set_style_text_color(lbl_ble_estado, estado == BLE_VINCULADO ? AZUL() : lv_color_white(), 0);
    char buf[48];
    snprintf(buf, sizeof(buf), "Dirección: %s", direccion);
    lv_label_set_text(lbl_ble_dir, buf);
    lv_label_set_text(lbl_ble_bond, tiene_bond ? "Emparejamiento guardado: sí" : "Emparejamiento guardado: no");
    lv_label_set_text(lbl_pie, estado == BLE_VINCULADO ? "Abajo: notificaciones · arriba: ajustes"
                               : "Sin iPhone · abajo: ajustes");
}

void ui_ram(unsigned kb)
{
    ram_kb = kb;
    char buf[96];
    snprintf(buf, sizeof(buf), "Versión %s · RAM libre %u KB · WiFi %s", esp_app_get_description()->version, kb, red_ip_txt);
    lv_label_set_text(lbl_acerca, buf);
}

// ---------------------------------------------------------------- notificaciones

static void poner_contador(void)
{
    int n = notif_store_no_leidas();
    int total = notif_store_cantidad();
    if (total <= 0) {
        lv_label_set_text(lbl_notifs, "");
    } else {
        char buf[48];
        if (n > 0) {
            snprintf(buf, sizeof(buf), "%d sin leer de %d", n, total);
        } else {
            snprintf(buf, sizeof(buf), "%d notificaci%s", total, total == 1 ? "ón" : "ones");
        }
        lv_label_set_text(lbl_notifs, buf);
    }
}

static void crear_item(const notif_t *n)
{
    char app[32], hora[8], cab[64];
    hora_de_fecha(n->fecha, hora, sizeof(hora));
    snprintf(cab, sizeof(cab), "%s%s%s", nombre_app(n, app, sizeof(app)), hora[0] ? " · " : "", hora);

    lv_obj_t *b = lv_button_create(lista_cont);
    lv_obj_set_width(b, LV_PCT(100));
    lv_obj_set_height(b, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(b, lv_color_hex(n->leida ? 0x161616 : 0x1f2a3a), 0);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_pad_all(b, 12, 0);
    lv_obj_set_style_pad_row(b, 4, 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_user_data(b, (void *)(uintptr_t)n->uid);
    lv_obj_add_event_cb(b, al_tocar_item, LV_EVENT_CLICKED, NULL);

    etiqueta(b, F16, n->leida ? GRIS() : AZUL(), cab);
    lv_obj_t *l = etiqueta(b, F20, lv_color_white(), n->titulo[0] ? n->titulo : (n->con_detalle ? "(sin título)" : "…"));
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    if (n->mensaje[0]) {
        l = etiqueta(b, F16, GRIS(), n->mensaje);
        lv_obj_set_width(l, LV_PCT(100));
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    }
}

static void reconstruir_lista(void)
{
    lv_obj_clean(lista_cont);
    notif_t n;
    int total = notif_store_cantidad();
    for (int i = 0; i < total; i++) {
        if (notif_store_obtener(i, &n)) {
            crear_item(&n);
        }
    }
    if (total == 0) {
        lv_obj_remove_flag(lbl_lista_vacia, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(lbl_lista_vacia, LV_OBJ_FLAG_HIDDEN);
    }
}

static void poner_detalle(const notif_t *n)
{
    char app[32], hora[8], cab[64];
    hora_de_fecha(n->fecha, hora, sizeof(hora));
    snprintf(cab, sizeof(cab), "%s%s%s", nombre_app(n, app, sizeof(app)), hora[0] ? " · " : "", hora);
    lv_label_set_text(det_cab, cab);
    lv_label_set_text(det_titulo, n->titulo[0] ? n->titulo : "(sin título)");
    lv_label_set_text(det_sub, n->subtitulo);
    lv_obj_align_to(det_sub, det_titulo, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);
    lv_label_set_text(det_msg, n->mensaje[0] ? n->mensaje : (n->con_detalle ? "" : "Cargando…"));
}

void ui_notifs_refrescar(void)
{
    poner_contador();
    lv_obj_t *act = lv_screen_active();
    if (act == scr_lista) {
        reconstruir_lista();
    } else if (act == scr_detalle) {
        notif_t n;
        if (notif_store_buscar(uid_detalle, &n)) {
            poner_detalle(&n);
        } else {
            ui_ir_lista();   // el iPhone la quitó
        }
    }
}

void ui_aviso(uint32_t uid)
{
    notif_t n;
    if (!notif_store_buscar(uid, &n) || lv_screen_active() == scr_llamada) {
        return;
    }
    char app[32];
    uid_popup = uid;
    lv_label_set_text(pop_app, nombre_app(&n, app, sizeof(app)));
    lv_label_set_text(pop_titulo, n.titulo[0] ? n.titulo : "(sin título)");
    lv_label_set_text(pop_msg, n.mensaje);
    lv_obj_remove_flag(popup, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(popup);
    if (popup_timer) {
        lv_timer_delete(popup_timer);
    }
    popup_timer = lv_timer_create(al_vencer_popup, 6000, NULL);
    lv_timer_set_repeat_count(popup_timer, 1);
}

void ui_llamada(uint32_t uid, bool mostrar)
{
    if (mostrar) {
        notif_t n;
        if (!notif_store_buscar(uid, &n)) {
            return;
        }
        if (lv_screen_active() != scr_llamada) {
            scr_antes_llamada = lv_screen_active();
        }
        uid_llamada = uid;
        hay_llamada = true;
        lv_label_set_text(llam_titulo, n.titulo[0] ? n.titulo : "Llamada");
        lv_label_set_text(llam_sub, n.subtitulo[0] ? n.subtitulo : n.mensaje);
        lv_obj_add_flag(popup, LV_OBJ_FLAG_HIDDEN);
        lv_screen_load(scr_llamada);
    } else if (hay_llamada && lv_screen_active() == scr_llamada && uid == uid_llamada) {
        hay_llamada = false;
        lv_screen_load(scr_antes_llamada ? scr_antes_llamada : scr_reloj);
    }
}

bool ui_en_llamada(uint32_t *uid)
{
    if (lv_screen_active() != scr_llamada) {
        return false;
    }
    if (uid) {
        *uid = uid_llamada;
    }
    return true;
}

// ---------------------------------------------------------------- navegación y callbacks

void ui_ir_reloj(void)
{
    lv_screen_load_anim(scr_reloj, LV_SCR_LOAD_ANIM_FADE_IN, 150, 0, false);
}

void ui_ir_ajustes(void) { lv_screen_load_anim(scr_ajustes, LV_SCR_LOAD_ANIM_MOVE_TOP, 200, 0, false); }

void ui_ir_lista(void)
{
    reconstruir_lista();
    lv_obj_scroll_to_y(lista_cont, 0, LV_ANIM_OFF);
    lv_screen_load_anim(scr_lista, LV_SCR_LOAD_ANIM_MOVE_BOTTOM, 200, 0, false);
}

void ui_ir_detalle(uint32_t uid)
{
    notif_t n;
    if (!notif_store_buscar(uid, &n)) {
        return;
    }
    uid_detalle = uid;
    notif_store_marcar_leida(uid);
    poner_detalle(&n);
    poner_contador();
    lv_screen_load_anim(scr_detalle, LV_SCR_LOAD_ANIM_MOVE_LEFT, 150, 0, false);
}

bool ui_en_reloj(void) { return lv_screen_active() == scr_reloj; }
void ui_al_olvidar(ui_cb_t cb) { cb_olvidar = cb; }
void ui_al_cambiar_brillo(ui_cb_int_t cb) { cb_brillo = cb; }
void ui_al_descartar(ui_cb_uid_t cb) { cb_descartar = cb; }
void ui_al_llamada(ui_cb_llamada_t cb) { cb_llamada = cb; }

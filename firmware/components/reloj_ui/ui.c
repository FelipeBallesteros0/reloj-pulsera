#include "ui.h"
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "esp_app_desc.h"

LV_FONT_DECLARE(fuente_16);
LV_FONT_DECLARE(fuente_20);
LV_FONT_DECLARE(fuente_24);
LV_FONT_DECLARE(fuente_72);

static const char *DIAS[] = { "domingo", "lunes", "martes", "miércoles", "jueves", "viernes", "sábado" };
static const char *MESES[] = { "enero", "febrero", "marzo", "abril", "mayo", "junio", "julio", "agosto",
                               "septiembre", "octubre", "noviembre", "diciembre" };

static lv_obj_t *scr_reloj, *scr_ajustes;
static lv_obj_t *lbl_iconos, *lbl_hora, *lbl_fecha, *lbl_notifs, *lbl_pie;
static lv_obj_t *lbl_ble_estado, *lbl_ble_dir, *lbl_ble_bond, *lbl_acerca, *slider_brillo;
static ui_cb_t cb_olvidar;
static ui_cb_int_t cb_brillo;
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

static void pantalla_negra(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
}

static void gesto_reloj(lv_event_t *e)
{
    lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_active());
    if (d == LV_DIR_BOTTOM) {
        ui_ir_ajustes();
    }
}

static void gesto_ajustes(lv_event_t *e)
{
    lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_active());
    if (d == LV_DIR_TOP) {
        ui_ir_reloj();
    }
}

static void al_pulsar_volver(lv_event_t *e) { ui_ir_reloj(); }

static void al_pulsar_olvidar(lv_event_t *e)
{
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

static void crear_reloj(void)
{
    scr_reloj = lv_obj_create(NULL);
    pantalla_negra(scr_reloj);
    lv_obj_add_event_cb(scr_reloj, gesto_reloj, LV_EVENT_GESTURE, NULL);

    lbl_iconos = etiqueta(scr_reloj, &lv_font_montserrat_16, GRIS(), LV_SYMBOL_BLUETOOTH "  " LV_SYMBOL_BATTERY_EMPTY " --%");
    lv_obj_align(lbl_iconos, LV_ALIGN_TOP_MID, 0, 18);

    lbl_hora = etiqueta(scr_reloj, &fuente_72, lv_color_white(), "--:--");
    lv_obj_align(lbl_hora, LV_ALIGN_CENTER, 0, -40);

    lbl_fecha = etiqueta(scr_reloj, &fuente_20, GRIS(), "");
    lv_obj_align(lbl_fecha, LV_ALIGN_CENTER, 0, 20);

    lbl_notifs = etiqueta(scr_reloj, &fuente_20, AZUL(), "");
    lv_obj_align(lbl_notifs, LV_ALIGN_CENTER, 0, 70);

    lbl_pie = etiqueta(scr_reloj, &fuente_16, GRIS(), "Desliza hacia abajo: ajustes");
    lv_obj_align(lbl_pie, LV_ALIGN_BOTTOM_MID, 0, -24);
}

static void crear_ajustes(int brillo_pct)
{
    scr_ajustes = lv_obj_create(NULL);
    pantalla_negra(scr_ajustes);
    lv_obj_add_event_cb(scr_ajustes, gesto_ajustes, LV_EVENT_GESTURE, NULL);

    lv_obj_t *volver = lv_button_create(scr_ajustes);
    lv_obj_set_style_bg_color(volver, lv_color_hex(0x222222), 0);
    lv_obj_align(volver, LV_ALIGN_TOP_LEFT, 16, 12);
    etiqueta(volver, &fuente_16, lv_color_white(), LV_SYMBOL_UP " Reloj");
    lv_obj_add_event_cb(volver, al_pulsar_volver, LV_EVENT_CLICKED, NULL);

    lv_obj_t *titulo = etiqueta(scr_ajustes, &fuente_24, lv_color_white(), "Ajustes");
    lv_obj_align(titulo, LV_ALIGN_TOP_MID, 30, 18);

    lv_obj_t *l = etiqueta(scr_ajustes, &fuente_16, GRIS(), "Brillo");
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 20, 78);
    slider_brillo = lv_slider_create(scr_ajustes);
    lv_obj_set_size(slider_brillo, 360, 14);
    lv_obj_align(slider_brillo, LV_ALIGN_TOP_MID, 0, 108);
    lv_slider_set_range(slider_brillo, 5, 100);
    lv_slider_set_value(slider_brillo, brillo_pct, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider_brillo, al_mover_brillo, LV_EVENT_VALUE_CHANGED, NULL);

    l = etiqueta(scr_ajustes, &fuente_20, lv_color_white(), "Bluetooth");
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 20, 160);
    char buf[64];
    snprintf(buf, sizeof(buf), "Nombre: %s", nombre);
    l = etiqueta(scr_ajustes, &fuente_16, GRIS(), buf);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 20, 194);
    lbl_ble_estado = etiqueta(scr_ajustes, &fuente_16, lv_color_white(), "Anunciando…");
    lv_obj_align(lbl_ble_estado, LV_ALIGN_TOP_LEFT, 20, 220);
    lbl_ble_dir = etiqueta(scr_ajustes, &fuente_16, GRIS(), "Dirección: --");
    lv_obj_align(lbl_ble_dir, LV_ALIGN_TOP_LEFT, 20, 246);
    lbl_ble_bond = etiqueta(scr_ajustes, &fuente_16, GRIS(), "Emparejamiento guardado: no");
    lv_obj_align(lbl_ble_bond, LV_ALIGN_TOP_LEFT, 20, 272);

    lv_obj_t *olvidar = lv_button_create(scr_ajustes);
    lv_obj_set_style_bg_color(olvidar, lv_palette_darken(LV_PALETTE_RED, 2), 0);
    lv_obj_align(olvidar, LV_ALIGN_TOP_LEFT, 20, 306);
    etiqueta(olvidar, &fuente_16, lv_color_white(), "Olvidar iPhone");
    lv_obj_add_event_cb(olvidar, al_pulsar_olvidar, LV_EVENT_CLICKED, NULL);
    l = etiqueta(scr_ajustes, &fuente_16, GRIS(), "(en el iPhone: Ajustes > Bluetooth > Omitir)");
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 20, 352);

    l = etiqueta(scr_ajustes, &fuente_20, lv_color_white(), "Acerca de");
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 20, 400);
    lbl_acerca = etiqueta(scr_ajustes, &fuente_16, GRIS(), "");
    lv_obj_align(lbl_acerca, LV_ALIGN_TOP_LEFT, 20, 434);
    lv_obj_t *pie = etiqueta(scr_ajustes, &fuente_16, GRIS(), "Desliza hacia arriba: reloj");
    lv_obj_align(pie, LV_ALIGN_BOTTOM_MID, 0, -12);
}

void ui_init(const char *nombre_ble, int brillo_pct)
{
    strncpy(nombre, nombre_ble, sizeof(nombre) - 1);
    crear_reloj();
    crear_ajustes(brillo_pct);
    ui_ram(0);
    lv_screen_load(scr_reloj);
}

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
static bool bat_cargando, bat_usb;

static void refrescar_iconos(void)
{
    char buf[48];
    snprintf(buf, sizeof(buf), "%s%s  %s %s%d%%", LV_SYMBOL_BLUETOOTH, ble_actual == BLE_VINCULADO ? "" : "?",
             icono_bateria(bat_pct), bat_cargando ? LV_SYMBOL_CHARGE " " : (bat_usb ? LV_SYMBOL_USB " " : ""), bat_pct < 0 ? 0 : bat_pct);
    lv_label_set_text(lbl_iconos, buf);
    lv_obj_set_style_text_color(lbl_iconos, ble_actual == BLE_VINCULADO ? AZUL() : GRIS(), 0);
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
                    : (tiene_bond ? "Esperando al iPhone…" : "Visible: en el iPhone, Ajustes > Bluetooth");
    lv_label_set_text(lbl_ble_estado, txt);
    lv_obj_set_style_text_color(lbl_ble_estado, estado == BLE_VINCULADO ? AZUL() : lv_color_white(), 0);
    char buf[48];
    snprintf(buf, sizeof(buf), "Dirección: %s", direccion);
    lv_label_set_text(lbl_ble_dir, buf);
    lv_label_set_text(lbl_ble_bond, tiene_bond ? "Emparejamiento guardado: sí" : "Emparejamiento guardado: no");
    lv_label_set_text(lbl_pie, estado == BLE_VINCULADO ? "Desliza hacia abajo: ajustes"
                               : "Sin iPhone · desliza hacia abajo: ajustes");
}

void ui_ram(unsigned kb)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "Versión %s · RAM libre %u KB", esp_app_get_description()->version, kb);
    lv_label_set_text(lbl_acerca, buf);
}

void ui_notif_pendientes(int n)
{
    if (n <= 0) {
        lv_label_set_text(lbl_notifs, "");
    } else {
        char buf[40];
        snprintf(buf, sizeof(buf), "%d notificaci%s", n, n == 1 ? "ón" : "ones");
        lv_label_set_text(lbl_notifs, buf);
    }
}

void ui_al_olvidar(ui_cb_t cb) { cb_olvidar = cb; }
void ui_al_cambiar_brillo(ui_cb_int_t cb) { cb_brillo = cb; }
void ui_ir_reloj(void) { lv_screen_load_anim(scr_reloj, LV_SCR_LOAD_ANIM_MOVE_BOTTOM, 200, 0, false); }
void ui_ir_ajustes(void) { lv_screen_load_anim(scr_ajustes, LV_SCR_LOAD_ANIM_MOVE_TOP, 200, 0, false); }
bool ui_en_reloj(void) { return lv_screen_active() == scr_reloj; }

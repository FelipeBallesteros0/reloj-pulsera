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
#define FICONO (&lv_font_montserrat_48)     // símbolos grandes del menú de apps
#define FSIMB (&lv_font_montserrat_16)      // símbolos pequeños

// La pantalla tiene las esquinas redondeadas con un radio de ~120 px (medido: el arco de 100 se corta, el de 120
// no). Regla: un punto (x, y) cerca de una esquina se ve si dista menos de ESQ_R del centro del arco, que está a
// (ESQ_R, ESQ_R) de esa esquina. De ahí salen estos márgenes; los botones son píldoras, que entran mejor en el arco.
#define ESQ_R        124
#define CAB_X        44          // botón "volver": su extremo redondeado queda dentro del arco superior izquierdo
#define CAB_Y        18
#define CAB_ALTO     44
#define TITULO_Y     26
#define PIE_MARGEN   18          // botón inferior de 52 px: margen al borde de abajo
#define BOTON_ANCHO  320         // ancho máximo de un botón píldora pegado abajo
#define COL_X        32          // columna de texto en pantallas densas (a esa x se ve desde y=38 hasta 464)
#define POPUP_ANCHO  350
#define POPUP_Y      30
#define ANCHO        410
#define ALTO         502

static const char *DIAS[] = { "domingo", "lunes", "martes", "miércoles", "jueves", "viernes", "sábado" };
static const char *MESES[] = { "enero", "febrero", "marzo", "abril", "mayo", "junio", "julio", "agosto",
                               "septiembre", "octubre", "noviembre", "diciembre" };

// Pantallas permanentes.
static lv_obj_t *scr_reloj, *scr_menu, *scr_ajustes, *scr_lista, *scr_detalle, *scr_llamada, *scr_andia;
// Pantallas que se crean al abrirlas y se borran al salir (RAM).
static lv_obj_t *scr_sub, *scr_sensores, *scr_flappy;

static lv_obj_t *and_estado, *and_pregunta, *and_respuesta, *and_boton, *and_boton_lbl, *btn_mic;
static andia_estado_t and_est = ANDIA_INACTIVA;
static lv_obj_t *lbl_iconos, *lbl_hora, *lbl_fecha, *lbl_notifs, *lbl_pie;
static lv_obj_t *lista_cont, *lbl_lista_vacia;
static lv_obj_t *det_cab, *det_titulo, *det_sub, *det_msg;
static lv_obj_t *llam_titulo, *llam_sub;
static lv_obj_t *popup, *pop_app, *pop_titulo, *pop_msg;
static lv_timer_t *popup_timer;
static uint32_t uid_detalle, uid_popup, uid_llamada;
static bool hay_llamada;
static lv_obj_t *scr_antes_llamada;

static ui_cb_t cb_olvidar, cb_microfono, cb_cancelar_andia, cb_probar, cb_apagar;
static ui_cb_int_t cb_brillo, cb_volumen, cb_tiempo, cb_sensibilidad;
static ui_cb_bool_t cb_vigilia;
static ui_cb_uid_t cb_descartar;
static ui_cb_llamada_t cb_llamada;
static char nombre[32];

// Estado que se muestra en las secciones de Configuración (existan o no en pantalla en ese momento).
static struct {
    int volumen, tiempo_s, sensibilidad, brillo;
    bool vigilia;
    char ble_estado[48], ble_dir[48], ble_bond[40];
    bool ble_vinculado;
    char wifi_ssid[40], wifi_ip[20];
    bool wifi_ok, en_casa, andia_ok;
    unsigned ram_kb;
} aj = { 60, 20, 100, 60, true, "Anunciando…", "Dirección: --", "Emparejamiento guardado: no", false, "--", "--",
         false, false, false, 0 };

typedef enum { SEC_SONIDO, SEC_PANTALLA, SEC_ANDIA, SEC_BLUETOOTH, SEC_WIFI, SEC_ACERCA, SEC_APAGAR, SEC_N } seccion_t;
static seccion_t sub_tipo;
// Widgets de la sección abierta que se refrescan en vivo (NULL si no están).
static lv_obj_t *sub_ble_estado, *sub_ble_dir, *sub_ble_bond, *sub_wifi[4], *sub_acerca, *sub_tiempo_btn[4];
static lv_obj_t *btn_olvidar_lbl, *btn_apagar_lbl;
static lv_timer_t *olvidar_timer, *apagar_timer;

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
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);   // píldora: sus extremos redondos caben en las esquinas
    lv_obj_set_style_pad_hor(b, 24, 0);
    lv_obj_set_style_pad_ver(b, 12, 0);
    lv_obj_set_style_min_height(b, 52, 0);   // objetivo táctil cómodo para el dedo
    lv_obj_t *l = etiqueta(b, F20, lv_color_white(), texto);
    lv_obj_center(l);
    if (cb) {
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    }
    return b;
}

// Cabecera común: botón "volver" fuera del arco de la esquina y título centrado a su derecha.
static lv_obj_t *cabecera(lv_obj_t *scr, const char *volver, lv_event_cb_t cb, const char *titulo)
{
    lv_obj_t *b = boton(scr, volver, 0x222222, cb);
    lv_obj_set_style_min_height(b, CAB_ALTO, 0);
    lv_obj_set_style_pad_ver(b, 8, 0);
    lv_obj_set_style_pad_hor(b, 18, 0);
    lv_obj_align(b, LV_ALIGN_TOP_LEFT, CAB_X, CAB_Y);
    lv_obj_t *t = etiqueta(scr, F24, lv_color_white(), titulo);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 50, TITULO_Y);
    return b;
}

static void pantalla_negra(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *deslizador(lv_obj_t *scr, int y, int min, int max, int valor, lv_event_cb_t cb)
{
    lv_obj_t *s = lv_slider_create(scr);
    lv_obj_set_size(s, ANCHO - 2 * COL_X, 16);
    lv_obj_align(s, LV_ALIGN_TOP_MID, 0, y);
    lv_slider_set_range(s, min, max);
    lv_slider_set_value(s, valor, LV_ANIM_OFF);
    lv_obj_add_event_cb(s, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return s;
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

// ---------------------------------------------------------------- pantallas efímeras

static void flappy_parar(void);

// Sale de una pantalla creada al vuelo: LVGL la borra al terminar la animación (auto_del) y se olvida su puntero.
static void salir_efimera(lv_obj_t *destino, lv_screen_load_anim_t anim)
{
    lv_obj_t *vieja = lv_screen_active();
    if (vieja == scr_sub) {
        scr_sub = NULL;
        sub_ble_estado = sub_ble_dir = sub_ble_bond = sub_acerca = NULL;
        memset(sub_wifi, 0, sizeof(sub_wifi));
        memset(sub_tiempo_btn, 0, sizeof(sub_tiempo_btn));
        btn_olvidar_lbl = btn_apagar_lbl = NULL;
        if (olvidar_timer) { lv_timer_delete(olvidar_timer); olvidar_timer = NULL; }
        if (apagar_timer) { lv_timer_delete(apagar_timer); apagar_timer = NULL; }
    } else if (vieja == scr_sensores) {
        scr_sensores = NULL;
    } else if (vieja == scr_flappy) {
        flappy_parar();
        scr_flappy = NULL;
    } else {
        lv_screen_load_anim(destino, anim, 150, 0, false);
        return;
    }
    lv_screen_load_anim(destino, anim, 150, 0, true);
}

// ---------------------------------------------------------------- gestos y navegación

// Carátula: deslizar hacia arriba = menú de apps; hacia abajo = notificaciones (así lo quiere Felipe).
static void gesto_reloj(lv_event_t *e)
{
    lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_active());
    if (d == LV_DIR_TOP) {
        ui_ir_menu();
    } else if (d == LV_DIR_BOTTOM) {
        ui_ir_lista();
    }
}

static void gesto_menu(lv_event_t *e)
{
    if (lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_BOTTOM) {
        ui_ir_reloj();
    }
}

static void gesto_ajustes(lv_event_t *e)
{
    if (lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_RIGHT) {
        ui_ir_menu();
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

static void gesto_andia(lv_event_t *e)
{
    if (lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_RIGHT) {
        ui_ir_reloj();
    }
}

static void gesto_sub(lv_event_t *e)
{
    if (lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_RIGHT) {
        salir_efimera(scr_ajustes, LV_SCR_LOAD_ANIM_MOVE_RIGHT);
    }
}

static void gesto_app(lv_event_t *e)
{
    if (lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_RIGHT) {
        salir_efimera(scr_menu, LV_SCR_LOAD_ANIM_MOVE_RIGHT);
    }
}

static void al_pulsar_volver_reloj(lv_event_t *e) { ui_ir_reloj(); }
static void al_pulsar_volver_menu(lv_event_t *e) { ui_ir_menu(); }
static void al_pulsar_volver_lista(lv_event_t *e) { ui_ir_lista(); }
static void al_pulsar_volver_ajustes(lv_event_t *e) { salir_efimera(scr_ajustes, LV_SCR_LOAD_ANIM_MOVE_RIGHT); }
static void al_pulsar_salir_app(lv_event_t *e) { salir_efimera(scr_menu, LV_SCR_LOAD_ANIM_MOVE_RIGHT); }

static void al_pulsar_microfono(lv_event_t *e)
{
    ui_ir_andia();
    if (cb_microfono) {
        cb_microfono();
    }
}

static void al_pulsar_boton_andia(lv_event_t *e)
{
    if (and_est == ANDIA_INACTIVA) {
        if (cb_microfono) {
            cb_microfono();
        }
    } else if (cb_cancelar_andia) {
        cb_cancelar_andia();
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

// ---------------------------------------------------------------- carátula

static void refrescar_pie(void);

static void crear_reloj(void)
{
    scr_reloj = lv_obj_create(NULL);
    pantalla_negra(scr_reloj);
    lv_obj_add_event_cb(scr_reloj, gesto_reloj, LV_EVENT_GESTURE, NULL);

    lbl_iconos = etiqueta(scr_reloj, FSIMB, GRIS(), LV_SYMBOL_BLUETOOTH "  " LV_SYMBOL_BATTERY_EMPTY " --%");
    lv_obj_align(lbl_iconos, LV_ALIGN_TOP_MID, 0, 18);

    lbl_hora = etiqueta(scr_reloj, &fuente_72, lv_color_white(), "--:--");
    lv_obj_align(lbl_hora, LV_ALIGN_CENTER, 0, -40);

    lbl_fecha = etiqueta(scr_reloj, F20, GRIS(), "");
    lv_obj_align(lbl_fecha, LV_ALIGN_CENTER, 0, 20);

    lbl_notifs = etiqueta(scr_reloj, F20, AZUL(), "");
    lv_obj_align(lbl_notifs, LV_ALIGN_CENTER, 0, 70);

    lbl_pie = etiqueta(scr_reloj, F16, GRIS(), "");
    lv_obj_align(lbl_pie, LV_ALIGN_BOTTOM_MID, 0, -34);

    // Botón de micrófono: abre la pantalla ANDIA y empieza a escuchar.
    btn_mic = lv_button_create(scr_reloj);
    lv_obj_set_size(btn_mic, 72, 72);
    lv_obj_set_style_radius(btn_mic, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn_mic, lv_color_hex(0x1f3a5a), 0);
    lv_obj_align(btn_mic, LV_ALIGN_BOTTOM_MID, 0, -64);
    lv_obj_t *ic = etiqueta(btn_mic, FSIMB, lv_color_white(), LV_SYMBOL_AUDIO);
    lv_obj_center(ic);
    lv_obj_add_event_cb(btn_mic, al_pulsar_microfono, LV_EVENT_CLICKED, NULL);
    refrescar_pie();
}

// ---------------------------------------------------------------- menú de apps

typedef enum { APP_AJUSTES, APP_ANDIA, APP_SENSORES, APP_FLAPPY } app_t;
static void abrir_sensores(void);
static void abrir_flappy(void);

static void al_tocar_app(lv_event_t *e)
{
    switch ((app_t)(uintptr_t)lv_event_get_user_data(e)) {
    case APP_AJUSTES: ui_ir_ajustes(); break;
    case APP_ANDIA: ui_ir_andia(); break;
    case APP_SENSORES: abrir_sensores(); break;
    case APP_FLAPPY: abrir_flappy(); break;
    }
}

// Icono redondo de 140 px con el símbolo grande dentro y el nombre debajo; 2×2 por pantalla.
static void icono_app(const char *simbolo, const char *nom, uint32_t color, int x, int y, app_t app)
{
    lv_obj_t *b = lv_button_create(scr_menu);
    lv_obj_set_size(b, 140, 140);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_align(b, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_t *s = etiqueta(b, FICONO, lv_color_white(), simbolo);
    lv_obj_center(s);
    lv_obj_add_event_cb(b, al_tocar_app, LV_EVENT_CLICKED, (void *)(uintptr_t)app);
    lv_obj_t *l = etiqueta(scr_menu, F20, lv_color_white(), nom);
    lv_obj_align_to(l, b, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);
}

static void crear_menu(void)
{
    scr_menu = lv_obj_create(NULL);
    pantalla_negra(scr_menu);
    lv_obj_add_event_cb(scr_menu, gesto_menu, LV_EVENT_GESTURE, NULL);
    lv_obj_t *t = etiqueta(scr_menu, F24, lv_color_white(), "Apps");
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 28);
    // Centros en (115,160) (295,160) (115,350) (295,350): los círculos quedan dentro de los arcos de las esquinas.
    icono_app(LV_SYMBOL_SETTINGS, "Configuración", 0x3a3a3a, 45, 90, APP_AJUSTES);
    icono_app(LV_SYMBOL_AUDIO, "ANDIA", 0x1f3a5a, 225, 90, APP_ANDIA);
    icono_app(LV_SYMBOL_GPS, "Sensores", 0x1f5a3a, 45, 280, APP_SENSORES);
    icono_app(LV_SYMBOL_PLAY, "Flappy", 0x7a5a1c, 225, 280, APP_FLAPPY);
    lv_obj_t *pie = etiqueta(scr_menu, F16, GRIS(), "Abajo: reloj");
    lv_obj_align(pie, LV_ALIGN_BOTTOM_MID, 0, -14);
}

// ---------------------------------------------------------------- ANDIA

static void crear_andia(void)
{
    scr_andia = lv_obj_create(NULL);
    pantalla_negra(scr_andia);
    lv_obj_add_event_cb(scr_andia, gesto_andia, LV_EVENT_GESTURE, NULL);
    cabecera(scr_andia, LV_SYMBOL_LEFT "  Reloj", al_pulsar_volver_reloj, "ANDIA");

    and_estado = etiqueta(scr_andia, F16, AZUL(), "Lista");
    lv_obj_align(and_estado, LV_ALIGN_TOP_MID, 0, 74);

    and_pregunta = etiqueta(scr_andia, F16, GRIS(), "");
    lv_obj_set_width(and_pregunta, 370);
    lv_label_set_long_mode(and_pregunta, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(and_pregunta, LV_ALIGN_TOP_LEFT, 20, 104);

    lv_obj_t *caja = lv_obj_create(scr_andia);
    lv_obj_set_size(caja, 390, ALTO - 170 - 52 - PIE_MARGEN - 8);
    lv_obj_align(caja, LV_ALIGN_TOP_MID, 0, 170);
    lv_obj_set_style_bg_opa(caja, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(caja, 0, 0);
    lv_obj_set_style_pad_all(caja, 10, 0);
    lv_obj_set_scroll_dir(caja, LV_DIR_VER);
    and_respuesta = etiqueta(caja, F20, lv_color_white(), "Toca el botón y habla.");
    lv_obj_set_width(and_respuesta, 370);
    lv_label_set_long_mode(and_respuesta, LV_LABEL_LONG_MODE_WRAP);

    and_boton = boton(scr_andia, LV_SYMBOL_AUDIO "  Hablar", 0x1f3a5a, al_pulsar_boton_andia);
    lv_obj_set_width(and_boton, BOTON_ANCHO);
    lv_obj_align(and_boton, LV_ALIGN_BOTTOM_MID, 0, -PIE_MARGEN);
    and_boton_lbl = lv_obj_get_child(and_boton, 0);
}

// ---------------------------------------------------------------- Configuración: lista de secciones

static void abrir_seccion(seccion_t s);

static void al_tocar_seccion(lv_event_t *e)
{
    abrir_seccion((seccion_t)(uintptr_t)lv_event_get_user_data(e));
}

// Celda de la cuadrícula de Configuración: icono redondo de 130 px con el nombre debajo (como el menú de apps).
static void icono_seccion(lv_obj_t *cont, const char *simbolo, const char *texto, uint32_t color, seccion_t s)
{
    lv_obj_t *celda = lv_obj_create(cont);
    lv_obj_set_size(celda, 165, 176);
    lv_obj_set_style_bg_opa(celda, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(celda, 0, 0);
    lv_obj_set_style_pad_all(celda, 0, 0);
    lv_obj_remove_flag(celda, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *b = lv_button_create(celda);
    lv_obj_set_size(b, 130, 130);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_align(b, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_add_event_cb(b, al_tocar_seccion, LV_EVENT_CLICKED, (void *)(uintptr_t)s);
    lv_obj_t *ic = etiqueta(b, FICONO, lv_color_white(), simbolo);
    lv_obj_center(ic);
    lv_obj_t *l = etiqueta(celda, F20, lv_color_white(), texto);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 138);
}

static void crear_ajustes(void)
{
    scr_ajustes = lv_obj_create(NULL);
    pantalla_negra(scr_ajustes);
    lv_obj_add_event_cb(scr_ajustes, gesto_ajustes, LV_EVENT_GESTURE, NULL);
    cabecera(scr_ajustes, LV_SYMBOL_LEFT " Apps", al_pulsar_volver_menu, "Configuración");

    lv_obj_t *cont = lv_obj_create(scr_ajustes);
    lv_obj_set_size(cont, ANCHO, ALTO - 72);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 72);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_hor(cont, 30, 0);
    lv_obj_set_style_pad_top(cont, 12, 0);
    lv_obj_set_style_pad_bottom(cont, 70, 0);   // la última fila puede subir por encima del arco inferior
    lv_obj_set_style_pad_row(cont, 14, 0);
    lv_obj_set_style_pad_column(cont, 20, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW_WRAP);   // dos columnas de iconos; se desplaza hacia abajo
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);
    icono_seccion(cont, LV_SYMBOL_VOLUME_MAX, "Sonido", 0x1f3a5a, SEC_SONIDO);
    icono_seccion(cont, LV_SYMBOL_IMAGE, "Pantalla", 0x3a3a5a, SEC_PANTALLA);
    icono_seccion(cont, LV_SYMBOL_AUDIO, "ANDIA", 0x1f5a3a, SEC_ANDIA);
    icono_seccion(cont, LV_SYMBOL_BLUETOOTH, "Bluetooth", 0x1c4a7a, SEC_BLUETOOTH);
    icono_seccion(cont, LV_SYMBOL_WIFI, "WiFi", 0x3a5a1f, SEC_WIFI);
    icono_seccion(cont, LV_SYMBOL_LIST, "Acerca de", 0x3a3a3a, SEC_ACERCA);
    icono_seccion(cont, LV_SYMBOL_POWER, "Apagar", 0x7a1c1c, SEC_APAGAR);
}

// ---------------------------------------------------------------- Configuración: secciones (efímeras)

static void al_mover_volumen(lv_event_t *e)
{
    aj.volumen = (int)lv_slider_get_value(lv_event_get_target(e));
    if (cb_volumen) cb_volumen(aj.volumen);
}
static void al_mover_brillo(lv_event_t *e)
{
    aj.brillo = (int)lv_slider_get_value(lv_event_get_target(e));
    if (cb_brillo) cb_brillo(aj.brillo);
}
static void al_mover_sensibilidad(lv_event_t *e)
{
    aj.sensibilidad = (int)lv_slider_get_value(lv_event_get_target(e));
    if (cb_sensibilidad) cb_sensibilidad(aj.sensibilidad);
}
static void al_pulsar_probar(lv_event_t *e) { if (cb_probar) cb_probar(); }

static void al_cambiar_vigilia(lv_event_t *e)
{
    aj.vigilia = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    refrescar_pie();
    if (cb_vigilia) cb_vigilia(aj.vigilia);
}

static const int TIEMPOS[4] = { 10, 20, 30, 60 };

static void pintar_tiempos(void)
{
    for (int i = 0; i < 4; i++) {
        if (sub_tiempo_btn[i]) {
            lv_obj_set_style_bg_color(sub_tiempo_btn[i], lv_color_hex(TIEMPOS[i] == aj.tiempo_s ? 0x1f3a5a : 0x2a2a2a), 0);
        }
    }
}

static void al_pulsar_tiempo(lv_event_t *e)
{
    aj.tiempo_s = TIEMPOS[(int)(uintptr_t)lv_event_get_user_data(e)];
    pintar_tiempos();
    if (cb_tiempo) cb_tiempo(aj.tiempo_s);
}

static void desarmar_olvidar(lv_timer_t *t)
{
    if (btn_olvidar_lbl) lv_label_set_text(btn_olvidar_lbl, "Olvidar iPhone");
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
    if (cb_olvidar) cb_olvidar();
}

static void desarmar_apagar(lv_timer_t *t)
{
    if (btn_apagar_lbl) lv_label_set_text(btn_apagar_lbl, LV_SYMBOL_POWER "  Apagar el reloj");
    apagar_timer = NULL;
}

static void al_pulsar_apagar(lv_event_t *e)
{
    if (!apagar_timer) {
        lv_label_set_text(btn_apagar_lbl, "¿Seguro? Toca otra vez");
        apagar_timer = lv_timer_create(desarmar_apagar, 5000, NULL);
        lv_timer_set_repeat_count(apagar_timer, 1);
        return;
    }
    lv_timer_delete(apagar_timer);
    apagar_timer = NULL;
    if (cb_apagar) cb_apagar();
}

static lv_obj_t *texto_col(const lv_font_t *f, lv_color_t color, const char *t, int y)
{
    lv_obj_t *l = etiqueta(scr_sub, f, color, t);
    lv_obj_set_width(l, ANCHO - 2 * COL_X);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, COL_X, y);
    return l;
}

static void texto_acerca(char *buf, size_t cap)
{
    snprintf(buf, cap, "Reloj\nVersión %s\nRAM libre %u KB\nESP32-C6 · AMOLED 410×502\nWiFi %s",
             esp_app_get_description()->version, aj.ram_kb, aj.wifi_ip);
}

static void pintar_wifi(void)
{
    if (!sub_wifi[0]) return;
    char b[64];
    snprintf(b, sizeof(b), "Red: %s", aj.wifi_ok ? aj.wifi_ssid : "sin conexión");
    lv_label_set_text(sub_wifi[0], b);
    snprintf(b, sizeof(b), "IP: %s", aj.wifi_ok ? aj.wifi_ip : "--");
    lv_label_set_text(sub_wifi[1], b);
    lv_label_set_text(sub_wifi[2], !aj.wifi_ok ? "ANDIA: sin red" : aj.en_casa ? "ANDIA por la LAN de casa (sin TLS)"
                                                                               : "ANDIA por el túnel de Cloudflare (TLS)");
    lv_label_set_text(sub_wifi[3], aj.andia_ok ? "Conectado a ANDIA: sí" : "Conectado a ANDIA: no");
}

static void pintar_ble(void)
{
    if (!sub_ble_estado) return;
    lv_label_set_text(sub_ble_estado, aj.ble_estado);
    lv_obj_set_style_text_color(sub_ble_estado, aj.ble_vinculado ? AZUL() : lv_color_white(), 0);
    lv_label_set_text(sub_ble_dir, aj.ble_dir);
    lv_label_set_text(sub_ble_bond, aj.ble_bond);
}

static void abrir_seccion(seccion_t s)
{
    static const char *TITULOS[SEC_N] = { "Sonido", "Pantalla", "ANDIA", "Bluetooth", "WiFi", "Acerca de", "Apagar" };
    sub_tipo = s;
    scr_sub = lv_obj_create(NULL);
    pantalla_negra(scr_sub);
    lv_obj_add_event_cb(scr_sub, gesto_sub, LV_EVENT_GESTURE, NULL);
    cabecera(scr_sub, LV_SYMBOL_LEFT " Atrás", al_pulsar_volver_ajustes, TITULOS[s]);
    char buf[160];
    lv_obj_t *b;
    switch (s) {
    case SEC_SONIDO:
        texto_col(F16, GRIS(), "Volumen de la voz de ANDIA y de los avisos", 90);
        deslizador(scr_sub, 140, 0, 100, aj.volumen, al_mover_volumen);
        b = boton(scr_sub, LV_SYMBOL_VOLUME_MAX "  Probar", 0x1f3a5a, al_pulsar_probar);
        lv_obj_set_width(b, 220);
        lv_obj_align(b, LV_ALIGN_TOP_MID, 0, 200);
        break;
    case SEC_PANTALLA:
        texto_col(F16, GRIS(), "Brillo", 90);
        deslizador(scr_sub, 120, 5, 100, aj.brillo, al_mover_brillo);
        texto_col(F16, GRIS(), "Apagar la pantalla tras", 180);
        for (int i = 0; i < 4; i++) {
            snprintf(buf, sizeof(buf), "%d s", TIEMPOS[i]);
            sub_tiempo_btn[i] = boton(scr_sub, buf, 0x2a2a2a, al_pulsar_tiempo);
            lv_obj_remove_event_cb(sub_tiempo_btn[i], al_pulsar_tiempo);
            lv_obj_add_event_cb(sub_tiempo_btn[i], al_pulsar_tiempo, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
            lv_obj_set_style_pad_hor(sub_tiempo_btn[i], 0, 0);
            lv_obj_set_width(sub_tiempo_btn[i], 78);
            lv_obj_align(sub_tiempo_btn[i], LV_ALIGN_TOP_LEFT, COL_X + i * 86, 212);
        }
        pintar_tiempos();
        break;
    case SEC_ANDIA: {
        texto_col(F20, lv_color_white(), "\"Oye Andia\" siempre", 92);
        lv_obj_t *sw = lv_switch_create(scr_sub);
        lv_obj_set_size(sw, 64, 32);
        lv_obj_align(sw, LV_ALIGN_TOP_RIGHT, -COL_X, 88);
        if (aj.vigilia) lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, al_cambiar_vigilia, LV_EVENT_VALUE_CHANGED, NULL);
        texto_col(F16, GRIS(), "El micrófono escucha en local y solo manda la frase a la Pi cuando hay voz cerca.", 130);
        texto_col(F16, GRIS(), "Sensibilidad del micrófono", 210);
        deslizador(scr_sub, 240, 0, 100, aj.sensibilidad, al_mover_sensibilidad);
        texto_col(F16, GRIS(), "Más sensibilidad: te oye desde más lejos, pero se dispara más con ruido.", 272);
        break;
    }
    case SEC_BLUETOOTH:
        snprintf(buf, sizeof(buf), "Nombre: %s", nombre);
        texto_col(F16, GRIS(), buf, 90);
        sub_ble_estado = texto_col(F16, lv_color_white(), "", 118);
        sub_ble_dir = texto_col(F16, GRIS(), "", 146);
        sub_ble_bond = texto_col(F16, GRIS(), "", 174);
        pintar_ble();
        b = boton(scr_sub, "Olvidar iPhone", 0x7a1c1c, al_pulsar_olvidar);
        lv_obj_set_width(b, 300);
        lv_obj_align(b, LV_ALIGN_TOP_MID, 0, 220);
        btn_olvidar_lbl = lv_obj_get_child(b, 0);
        texto_col(F16, GRIS(), "Después, en el iPhone: Ajustes > Bluetooth > Omitir dispositivo, y volver a "
                               "emparejar desde nRF Connect.", 290);
        break;
    case SEC_WIFI:
        for (int i = 0; i < 4; i++) {
            sub_wifi[i] = texto_col(i == 0 ? F20 : F16, i == 0 ? lv_color_white() : GRIS(), "", 90 + i * 34);
        }
        pintar_wifi();
        texto_col(F16, GRIS(), "En casa el reloj habla con la Pi por la LAN; fuera, por el túnel.", 240);
        break;
    case SEC_ACERCA:
        texto_acerca(buf, sizeof(buf));
        sub_acerca = texto_col(F20, lv_color_white(), buf, 96);
        lv_obj_set_style_text_line_space(sub_acerca, 8, 0);
        break;
    case SEC_APAGAR:
        texto_col(F16, GRIS(), "El reloj se apaga por completo (Bluetooth, WiFi y pantalla). Para encenderlo, "
                               "mantén el botón lateral.", 96);
        b = boton(scr_sub, LV_SYMBOL_POWER "  Apagar el reloj", 0x7a1c1c, al_pulsar_apagar);
        lv_obj_set_width(b, BOTON_ANCHO);
        lv_obj_align(b, LV_ALIGN_TOP_MID, 0, 210);
        btn_apagar_lbl = lv_obj_get_child(b, 0);
        break;
    default:
        break;
    }
    lv_screen_load_anim(scr_sub, LV_SCR_LOAD_ANIM_MOVE_LEFT, 150, 0, false);
}

// ---------------------------------------------------------------- app Sensores (efímera)

static lv_obj_t *sen_acc, *sen_gir, *sen_punto;
#define BURBUJA_R 90
#define BURBUJA_CX 205
#define BURBUJA_CY 340

static void abrir_sensores(void)
{
    scr_sensores = lv_obj_create(NULL);
    pantalla_negra(scr_sensores);
    lv_obj_add_event_cb(scr_sensores, gesto_app, LV_EVENT_GESTURE, NULL);
    cabecera(scr_sensores, LV_SYMBOL_LEFT " Apps", al_pulsar_salir_app, "Sensores");
    lv_obj_t *l = etiqueta(scr_sensores, F16, GRIS(), "Acelerómetro (m/s²)      x · y · z");
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, COL_X, 84);
    sen_acc = etiqueta(scr_sensores, F24, lv_color_white(), "--");
    lv_obj_align(sen_acc, LV_ALIGN_TOP_LEFT, COL_X, 108);
    l = etiqueta(scr_sensores, F16, GRIS(), "Giroscopio (°/s)");
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, COL_X, 150);
    sen_gir = etiqueta(scr_sensores, F24, lv_color_white(), "--");
    lv_obj_align(sen_gir, LV_ALIGN_TOP_LEFT, COL_X, 174);
    // Nivel de burbuja: el punto se mueve con la inclinación.
    lv_obj_t *aro = lv_obj_create(scr_sensores);
    lv_obj_set_size(aro, 2 * BURBUJA_R, 2 * BURBUJA_R);
    lv_obj_set_pos(aro, BURBUJA_CX - BURBUJA_R, BURBUJA_CY - BURBUJA_R);
    lv_obj_set_style_radius(aro, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(aro, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(aro, GRIS(), 0);
    lv_obj_set_style_border_width(aro, 2, 0);
    lv_obj_remove_flag(aro, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *cruz = etiqueta(scr_sensores, FSIMB, GRIS(), LV_SYMBOL_PLUS);
    lv_obj_align(cruz, LV_ALIGN_TOP_LEFT, BURBUJA_CX - 8, BURBUJA_CY - 8);
    sen_punto = lv_obj_create(scr_sensores);
    lv_obj_set_size(sen_punto, 26, 26);
    lv_obj_set_style_radius(sen_punto, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(sen_punto, lv_palette_main(LV_PALETTE_LIGHT_GREEN), 0);
    lv_obj_set_style_border_width(sen_punto, 0, 0);
    lv_obj_remove_flag(sen_punto, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(sen_punto, BURBUJA_CX - 13, BURBUJA_CY - 13);
    lv_screen_load_anim(scr_sensores, LV_SCR_LOAD_ANIM_MOVE_LEFT, 150, 0, false);
}

bool ui_en_sensores(void) { return scr_sensores && lv_screen_active() == scr_sensores; }

void ui_sensores(float ax, float ay, float az, float gx, float gy, float gz)
{
    if (!ui_en_sensores()) return;
    char b[64];
    snprintf(b, sizeof(b), "%.1f · %.1f · %.1f", ax, ay, az);
    lv_label_set_text(sen_acc, b);
    snprintf(b, sizeof(b), "%.0f · %.0f · %.0f", gx, gy, gz);
    lv_label_set_text(sen_gir, b);
    // 1 g de inclinación lleva el punto al borde del aro.
    float dx = ax / 9.81f * (BURBUJA_R - 13), dy = ay / 9.81f * (BURBUJA_R - 13);
    float lim = BURBUJA_R - 13;
    if (dx > lim) dx = lim;
    if (dx < -lim) dx = -lim;
    if (dy > lim) dy = lim;
    if (dy < -lim) dy = -lim;
    lv_obj_set_pos(sen_punto, BURBUJA_CX - 13 + (int)dx, BURBUJA_CY - 13 - (int)dy);
}

// ---------------------------------------------------------------- app Flappy (efímera)

#define FL_SUELO 470
#define FL_PAJARO_X 110
#define FL_PAJARO 30
#define FL_TUBO_ANCHO 64
#define FL_HUECO 170
#define FL_TUBOS 2
#define FL_SEP ((ANCHO + FL_TUBO_ANCHO) / FL_TUBOS)   // separación horizontal entre tubos

static lv_timer_t *fl_timer;
static lv_obj_t *fl_pajaro, *fl_tubo[FL_TUBOS][2], *fl_puntos, *fl_msg;
static float fl_y, fl_vy;
static int fl_x[FL_TUBOS], fl_hueco_y[FL_TUBOS], fl_score;
static bool fl_pasado[FL_TUBOS];
static enum { FL_ESPERA, FL_JUGANDO, FL_FIN } fl_estado;

static void fl_colocar_tubo(int i, int x)
{
    fl_x[i] = x;
    fl_hueco_y[i] = 70 + (int)lv_rand(0, FL_SUELO - FL_HUECO - 140);
    fl_pasado[i] = false;
}

static void fl_pintar(void)
{
    lv_obj_set_pos(fl_pajaro, FL_PAJARO_X, (int)fl_y);
    for (int i = 0; i < FL_TUBOS; i++) {
        lv_obj_set_pos(fl_tubo[i][0], fl_x[i], 0);
        lv_obj_set_height(fl_tubo[i][0], fl_hueco_y[i]);
        lv_obj_set_pos(fl_tubo[i][1], fl_x[i], fl_hueco_y[i] + FL_HUECO);
        lv_obj_set_height(fl_tubo[i][1], FL_SUELO - fl_hueco_y[i] - FL_HUECO);
    }
    char b[16];
    snprintf(b, sizeof(b), "%d", fl_score);
    lv_label_set_text(fl_puntos, b);
}

static void fl_reiniciar(void)
{
    fl_y = 220;
    fl_vy = 0;
    fl_score = 0;
    for (int i = 0; i < FL_TUBOS; i++) {
        fl_colocar_tubo(i, ANCHO + 40 + i * FL_SEP);
    }
    fl_estado = FL_ESPERA;
    lv_label_set_text(fl_msg, "Toca para jugar");
    lv_obj_remove_flag(fl_msg, LV_OBJ_FLAG_HIDDEN);
    fl_pintar();
}

static bool fl_choca(void)
{
    int by = (int)fl_y;
    if (by < 0 || by + FL_PAJARO > FL_SUELO) return true;
    for (int i = 0; i < FL_TUBOS; i++) {
        if (FL_PAJARO_X + FL_PAJARO - 4 > fl_x[i] && FL_PAJARO_X + 4 < fl_x[i] + FL_TUBO_ANCHO) {
            if (by + 4 < fl_hueco_y[i] || by + FL_PAJARO - 4 > fl_hueco_y[i] + FL_HUECO) return true;
        }
    }
    return false;
}

static void fl_paso(lv_timer_t *t)
{
    if (fl_estado != FL_JUGANDO) return;
    fl_vy += 0.65f;
    fl_y += fl_vy;
    for (int i = 0; i < FL_TUBOS; i++) {
        fl_x[i] -= 5;
        if (!fl_pasado[i] && fl_x[i] + FL_TUBO_ANCHO < FL_PAJARO_X) {
            fl_pasado[i] = true;
            fl_score++;
        }
        if (fl_x[i] < -FL_TUBO_ANCHO) {
            fl_colocar_tubo(i, fl_x[i] + FL_TUBOS * FL_SEP);
        }
    }
    if (fl_choca()) {
        fl_estado = FL_FIN;
        char b[48];
        snprintf(b, sizeof(b), "Fin · %d punto%s\nToca para repetir", fl_score, fl_score == 1 ? "" : "s");
        lv_label_set_text(fl_msg, b);
        lv_obj_remove_flag(fl_msg, LV_OBJ_FLAG_HIDDEN);
    }
    fl_pintar();
}

static void fl_tocar(lv_event_t *e)
{
    if (fl_estado == FL_ESPERA) {
        fl_estado = FL_JUGANDO;
        lv_obj_add_flag(fl_msg, LV_OBJ_FLAG_HIDDEN);
        fl_vy = -8.5f;
    } else if (fl_estado == FL_JUGANDO) {
        fl_vy = -8.5f;
    } else {
        fl_reiniciar();
    }
}

static void flappy_parar(void)
{
    if (fl_timer) {
        lv_timer_delete(fl_timer);
        fl_timer = NULL;
    }
}

static lv_obj_t *fl_rect(uint32_t color)
{
    lv_obj_t *o = lv_obj_create(scr_flappy);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 6, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static void abrir_flappy(void)
{
    scr_flappy = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_flappy, lv_color_hex(0x102030), 0);
    lv_obj_set_style_bg_opa(scr_flappy, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr_flappy, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(scr_flappy, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr_flappy, gesto_app, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(scr_flappy, fl_tocar, LV_EVENT_PRESSED, NULL);
    for (int i = 0; i < FL_TUBOS; i++) {
        fl_tubo[i][0] = fl_rect(0x2e8b57);
        fl_tubo[i][1] = fl_rect(0x2e8b57);
        lv_obj_set_width(fl_tubo[i][0], FL_TUBO_ANCHO);
        lv_obj_set_width(fl_tubo[i][1], FL_TUBO_ANCHO);
    }
    lv_obj_t *suelo = fl_rect(0x8b6b3e);
    lv_obj_set_size(suelo, ANCHO, ALTO - FL_SUELO);
    lv_obj_set_pos(suelo, 0, FL_SUELO);
    lv_obj_set_style_radius(suelo, 0, 0);
    fl_pajaro = fl_rect(0xf5c518);
    lv_obj_set_size(fl_pajaro, FL_PAJARO, FL_PAJARO);
    lv_obj_set_style_radius(fl_pajaro, LV_RADIUS_CIRCLE, 0);
    fl_puntos = etiqueta(scr_flappy, &fuente_72, lv_color_white(), "0");
    lv_obj_align(fl_puntos, LV_ALIGN_TOP_MID, 0, 40);
    fl_msg = etiqueta(scr_flappy, F24, lv_color_white(), "");
    lv_obj_set_style_text_align(fl_msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(fl_msg, LV_ALIGN_CENTER, 0, 40);
    lv_obj_t *pista = etiqueta(scr_flappy, F16, GRIS(), "Desliza a la derecha para salir");
    lv_obj_align(pista, LV_ALIGN_BOTTOM_MID, 0, -6);
    fl_reiniciar();
    fl_timer = lv_timer_create(fl_paso, 40, NULL);
    lv_screen_load_anim(scr_flappy, LV_SCR_LOAD_ANIM_MOVE_LEFT, 150, 0, false);
}

// ---------------------------------------------------------------- notificaciones: lista, detalle, llamada, popup

static void crear_lista(void)
{
    scr_lista = lv_obj_create(NULL);
    pantalla_negra(scr_lista);
    lv_obj_add_event_cb(scr_lista, gesto_lista, LV_EVENT_GESTURE, NULL);
    cabecera(scr_lista, LV_SYMBOL_LEFT " Reloj", al_pulsar_volver_reloj, "Notificaciones");

    lista_cont = lv_obj_create(scr_lista);
    lv_obj_set_size(lista_cont, ANCHO, ALTO - 72);
    lv_obj_align(lista_cont, LV_ALIGN_TOP_MID, 0, 72);
    lv_obj_set_style_bg_opa(lista_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lista_cont, 0, 0);
    lv_obj_set_style_pad_hor(lista_cont, 12, 0);
    lv_obj_set_style_pad_top(lista_cont, 8, 0);
    lv_obj_set_style_pad_bottom(lista_cont, 56, 0);   // la última tarjeta puede subir por encima del arco inferior
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
    cabecera(scr_detalle, LV_SYMBOL_LEFT "  Lista", al_pulsar_volver_lista, "Notificación");
    lv_obj_t *descartar = boton(scr_detalle, LV_SYMBOL_TRASH "  Descartar", 0x7a1c1c, al_pulsar_descartar);
    lv_obj_set_width(descartar, BOTON_ANCHO);
    lv_obj_align(descartar, LV_ALIGN_BOTTOM_MID, 0, -PIE_MARGEN);

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
    lv_obj_set_size(caja, 390, ALTO - 200 - 52 - PIE_MARGEN - 8);
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
    lv_obj_set_size(popup, POPUP_ANCHO, 150);
    lv_obj_align(popup, LV_ALIGN_TOP_MID, 0, POPUP_Y);
    lv_obj_set_style_bg_color(popup, lv_color_hex(0x1f2a3a), 0);
    lv_obj_set_style_border_color(popup, AZUL(), 0);
    lv_obj_set_style_border_width(popup, 2, 0);
    lv_obj_set_style_radius(popup, 24, 0);
    lv_obj_set_style_pad_all(popup, 12, 0);
    lv_obj_remove_flag(popup, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(popup, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(popup, al_tocar_popup, LV_EVENT_CLICKED, NULL);

    pop_app = etiqueta(popup, F16, AZUL(), "");
    lv_obj_align(pop_app, LV_ALIGN_TOP_LEFT, 0, 0);
    pop_titulo = etiqueta(popup, F20, lv_color_white(), "");
    lv_obj_set_width(pop_titulo, POPUP_ANCHO - 28);
    lv_label_set_long_mode(pop_titulo, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(pop_titulo, LV_ALIGN_TOP_LEFT, 0, 26);
    pop_msg = etiqueta(popup, F16, GRIS(), "");
    lv_obj_set_width(pop_msg, POPUP_ANCHO - 28);
    lv_obj_set_height(pop_msg, 70);
    lv_label_set_long_mode(pop_msg, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(pop_msg, LV_ALIGN_TOP_LEFT, 0, 54);
}

// ---------------------------------------------------------------- init

void ui_init(const char *nombre_ble, int brillo_pct)
{
    // Cadena de respaldo: texto → emojis → símbolos de LVGL (flechas, papelera, etc., que viven en Montserrat).
    femoji = emoji_20; femoji.fallback = &lv_font_montserrat_16;
    f16 = fuente_16; f16.fallback = &femoji;
    f20 = fuente_20; f20.fallback = &femoji;
    f24 = fuente_24; f24.fallback = &femoji;
    strncpy(nombre, nombre_ble, sizeof(nombre) - 1);
    aj.brillo = brillo_pct;
    crear_reloj();
    crear_menu();
    crear_ajustes();
    crear_lista();
    crear_detalle();
    crear_llamada();
    crear_andia();
    crear_popup();
    ui_ram(0);
    lv_screen_load(scr_reloj);
}

void ui_ajustes(int volumen_pct, int tiempo_pantalla_s, int sensibilidad_pct)
{
    aj.volumen = volumen_pct;
    aj.tiempo_s = tiempo_pantalla_s;
    aj.sensibilidad = sensibilidad_pct;
}

// ---------------------------------------------------------------- carátula y estado

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
    char buf[64];
    snprintf(buf, sizeof(buf), "%s%s  %s%s  %s %s%d%%", LV_SYMBOL_BLUETOOTH, ble_actual == BLE_VINCULADO ? "" : "?",
             LV_SYMBOL_WIFI, aj.wifi_ok ? "" : "?",
             icono_bateria(bat_pct), bat_cargando ? LV_SYMBOL_CHARGE " " : (bat_usb ? LV_SYMBOL_USB " " : ""),
             bat_pct < 0 ? 0 : bat_pct);
    lv_label_set_text(lbl_iconos, buf);
    lv_obj_set_style_text_color(lbl_iconos, (ble_actual == BLE_VINCULADO || aj.wifi_ok) ? AZUL() : GRIS(), 0);
}

static void refrescar_pie(void)
{
    lv_label_set_text(lbl_pie, aj.vigilia ? "Di \"Oye Andia\" · arriba: apps · abajo: avisos"
                               : ble_actual == BLE_VINCULADO ? "Arriba: apps · abajo: notificaciones"
                               : "Sin iPhone · arriba: apps");
}

static void refrescar_acerca(void)
{
    if (sub_acerca) {
        char b[160];
        texto_acerca(b, sizeof(b));
        lv_label_set_text(sub_acerca, b);
    }
}

void ui_red(bool conectada, const char *ip)
{
    aj.wifi_ok = conectada;
    snprintf(aj.wifi_ip, sizeof(aj.wifi_ip), "%s", ip ? ip : "--");
    refrescar_iconos();
    refrescar_acerca();
    pintar_wifi();
}

void ui_wifi(const char *ssid, const char *ip, bool en_casa, bool andia_conectada)
{
    snprintf(aj.wifi_ssid, sizeof(aj.wifi_ssid), "%s", ssid ? ssid : "--");
    snprintf(aj.wifi_ip, sizeof(aj.wifi_ip), "%s", ip ? ip : "--");
    aj.en_casa = en_casa;
    aj.andia_ok = andia_conectada;
    pintar_wifi();
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
    snprintf(aj.ble_estado, sizeof(aj.ble_estado), "%s",
             estado == BLE_VINCULADO ? "Vinculado con el iPhone"
             : estado == BLE_CONECTADO ? "Conectado, cifrando…"
             : (tiene_bond ? "Esperando al iPhone…" : "Visible: conéctate desde nRF Connect"));
    aj.ble_vinculado = estado == BLE_VINCULADO;
    snprintf(aj.ble_dir, sizeof(aj.ble_dir), "Dirección: %s", direccion);
    snprintf(aj.ble_bond, sizeof(aj.ble_bond), "Emparejamiento guardado: %s", tiene_bond ? "sí" : "no");
    pintar_ble();
    refrescar_pie();
}

void ui_ram(unsigned kb)
{
    aj.ram_kb = kb;
    refrescar_acerca();
}

void ui_vigilia(bool activa)
{
    aj.vigilia = activa;
    refrescar_pie();
}

void ui_al_vigilia(ui_cb_bool_t cb) { cb_vigilia = cb; }
bool ui_en_andia(void) { return lv_screen_active() == scr_andia; }

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
            // Si estaba en una pantalla efímera, se vuelve a la carátula al colgar (esa pantalla se pierde).
            if (scr_antes_llamada == scr_sub || scr_antes_llamada == scr_sensores || scr_antes_llamada == scr_flappy) {
                scr_antes_llamada = scr_reloj;
            }
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

// ---------------------------------------------------------------- ANDIA

void ui_andia_estado(andia_estado_t e)
{
    and_est = e;
    static const char *TXT[] = { "Lista", "Conectando…", "Escuchando…", "Te escucho", "Pensando…", "Hablando" };
    lv_label_set_text(and_estado, TXT[e < 6 ? e : 0]);
    lv_obj_set_style_text_color(and_estado, e == ANDIA_INACTIVA ? GRIS() : AZUL(), 0);
    if (e == ANDIA_INACTIVA) {
        lv_label_set_text(and_boton_lbl, LV_SYMBOL_AUDIO "  Hablar");
        lv_obj_set_style_bg_color(and_boton, lv_color_hex(0x1f3a5a), 0);
    } else {
        lv_label_set_text(and_boton_lbl, LV_SYMBOL_CLOSE "  Cancelar");
        lv_obj_set_style_bg_color(and_boton, lv_color_hex(0x7a1c1c), 0);
    }
    if (e == ANDIA_ESCUCHANDO || e == ANDIA_ACTIVADA) {
        lv_label_set_text(and_pregunta, "");
        lv_label_set_text(and_respuesta, "");
    }
}

void ui_andia_pregunta(const char *t) { lv_label_set_text(and_pregunta, t); }
void ui_andia_respuesta(const char *t) { lv_label_set_text(and_respuesta, t); lv_obj_set_style_text_color(and_respuesta, lv_color_white(), 0); }
void ui_andia_error(const char *t) { lv_label_set_text(and_respuesta, t); lv_obj_set_style_text_color(and_respuesta, lv_palette_main(LV_PALETTE_ORANGE), 0); }
void ui_al_microfono(ui_cb_t cb) { cb_microfono = cb; }
void ui_al_cancelar_andia(ui_cb_t cb) { cb_cancelar_andia = cb; }

// ---------------------------------------------------------------- navegación y callbacks

// Cambia a una pantalla permanente; si la actual era efímera, se borra por el camino.
static void ir(lv_obj_t *scr, lv_screen_load_anim_t anim)
{
    if (lv_screen_active() == scr) {
        return;
    }
    salir_efimera(scr, anim);
}

void ui_ir_reloj(void) { ir(scr_reloj, LV_SCR_LOAD_ANIM_FADE_IN); }
void ui_ir_menu(void) { ir(scr_menu, LV_SCR_LOAD_ANIM_MOVE_TOP); }
void ui_ir_ajustes(void) { ir(scr_ajustes, LV_SCR_LOAD_ANIM_MOVE_LEFT); }
void ui_ir_andia(void) { ir(scr_andia, LV_SCR_LOAD_ANIM_MOVE_LEFT); }

void ui_ir_lista(void)
{
    reconstruir_lista();
    lv_obj_scroll_to_y(lista_cont, 0, LV_ANIM_OFF);
    ir(scr_lista, LV_SCR_LOAD_ANIM_MOVE_BOTTOM);
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
    ir(scr_detalle, LV_SCR_LOAD_ANIM_MOVE_LEFT);
}

bool ui_en_reloj(void) { return lv_screen_active() == scr_reloj; }
void ui_al_olvidar(ui_cb_t cb) { cb_olvidar = cb; }
void ui_al_cambiar_brillo(ui_cb_int_t cb) { cb_brillo = cb; }
void ui_al_descartar(ui_cb_uid_t cb) { cb_descartar = cb; }
void ui_al_llamada(ui_cb_llamada_t cb) { cb_llamada = cb; }
void ui_al_cambiar_volumen(ui_cb_int_t cb) { cb_volumen = cb; }
void ui_al_probar_sonido(ui_cb_t cb) { cb_probar = cb; }
void ui_al_cambiar_tiempo_pantalla(ui_cb_int_t cb) { cb_tiempo = cb; }
void ui_al_cambiar_sensibilidad(ui_cb_int_t cb) { cb_sensibilidad = cb; }
void ui_al_apagar(ui_cb_t cb) { cb_apagar = cb; }

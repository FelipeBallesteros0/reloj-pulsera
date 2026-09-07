#include "pantalla.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_lvgl_port.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "bsp/touch.h"
#include "sdkconfig.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "pantalla";

// Los bool de Kconfig solo existen como macro cuando están en "y".
#ifdef CONFIG_RELOJ_TACTIL_ESPEJO_Y
#define ESPEJO_Y true
#else
#define ESPEJO_Y false
#endif
#ifdef CONFIG_RELOJ_TACTIL_ESPEJO_X
#define ESPEJO_X true
#else
#define ESPEJO_X false
#endif
static esp_lcd_panel_handle_t panel;
static esp_lcd_panel_io_handle_t io;
static esp_lcd_touch_handle_t tp;
static lv_display_t *disp;
static lv_indev_t *indev;
static int brillo_pct = 80;
static bool encendida;
static volatile bool toque_pendiente;
static SemaphoreHandle_t sem_cuadro;   // lo da la tarea LVGL cada vez que termina de dibujar

// El CO5300 exige áreas de dibujo con inicio par y fin impar (mismo truco que usa el BSP).
static void redondear_area(lv_event_t *e)
{
    lv_area_t *a = lv_event_get_param(e);
    a->x1 &= ~1;
    a->y1 &= ~1;
    a->x2 |= 1;
    a->y2 |= 1;
}

static void brillo_hw(int pct)
{
    uint8_t v = (uint8_t)(pct * 255 / 100);
    uint32_t cmd = (0x02u << 24) | (0x51u << 8);   // opcode de escritura QSPI + registro de brillo del panel
    esp_lcd_panel_io_tx_param(io, cmd, &v, 1);
}

// Corre en la tarea LVGL. Con la pantalla apagada y LVGL parado sigue llegando: la INT del táctil (GPIO15) despierta
// la tarea del port, que lee el indev igualmente.
static void al_presionar(lv_event_t *e)
{
    (void)e;
    toque_pendiente = true;
}

// Corre en la tarea LVGL al terminar de dibujar un cuadro: así el encendido espera a que la carátula esté pintada
// sin dibujar desde la tarea principal (su pila de 3,5 KB no aguanta un render completo).
static void al_cuadro_listo(lv_event_t *e)
{
    (void)e;
    xSemaphoreGive(sem_cuadro);
}

// Comando del panel sin parámetros (mismo formato QSPI que el brillo).
static void comando(uint8_t c)
{
    esp_lcd_panel_io_tx_param(io, (0x02u << 24) | ((uint32_t)c << 8), NULL, 0);
}

esp_err_t pantalla_init(void)
{
    const lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_cfg), TAG, "lvgl_port_init");

    const bsp_display_config_t cfg = {
        .max_transfer_sz = BSP_LCD_H_RES * (CONFIG_BSP_DISPLAY_LVGL_BUF_HEIGHT + 2) * 2,
    };
    ESP_RETURN_ON_ERROR(bsp_display_new(&cfg, &panel, &io), TAG, "panel");

    const lvgl_port_display_cfg_t dcfg = {
        .io_handle = io,
        .panel_handle = panel,
        .buffer_size = BSP_LCD_H_RES * CONFIG_BSP_DISPLAY_LVGL_BUF_HEIGHT,
        .double_buffer = false,
        .hres = BSP_LCD_H_RES,
        .vres = BSP_LCD_V_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = { .swap_xy = false, .mirror_x = false, .mirror_y = false },
        .flags = { .buff_dma = false, .swap_bytes = true, .sw_rotate = false },   // sw_rotate=true cuesta otro buffer de 33 KB; el "swap_xy not supported" del log es inofensivo
    };
    disp = lvgl_port_add_disp(&dcfg);
    ESP_RETURN_ON_FALSE(disp, ESP_FAIL, TAG, "lvgl_port_add_disp");
    lv_display_add_event_cb(disp, redondear_area, LV_EVENT_INVALIDATE_AREA, NULL);
    sem_cuadro = xSemaphoreCreateBinary();
    lv_display_add_event_cb(disp, al_cuadro_listo, LV_EVENT_RENDER_READY, NULL);

    ESP_RETURN_ON_ERROR(bsp_touch_new(NULL, &tp), TAG, "táctil");
    // El FT3168 entrega Y espejado respecto del panel en esta placa (el BSP lo deja sin espejo).
    esp_lcd_touch_set_mirror_y(tp, ESPEJO_Y);
    esp_lcd_touch_set_mirror_x(tp, ESPEJO_X);
    const lvgl_port_touch_cfg_t tcfg = { .disp = disp, .handle = tp };
    indev = lvgl_port_add_touch(&tcfg);
    ESP_RETURN_ON_FALSE(indev, ESP_FAIL, TAG, "lvgl_port_add_touch");
    lv_indev_add_event_cb(indev, al_presionar, LV_EVENT_PRESSED, NULL);

    encendida = true;
    brillo_hw(brillo_pct);
    return ESP_OK;
}

lv_display_t *pantalla_disp(void) { return disp; }
bool pantalla_lock(uint32_t ms) { return lvgl_port_lock(ms); }
void pantalla_unlock(void) { lvgl_port_unlock(); }

void pantalla_brillo(int pct)
{
    if (pct < 1) pct = 1;
    if (pct > 100) pct = 100;
    brillo_pct = pct;
    if (encendida) {
        brillo_hw(pct);
    }
}

int pantalla_brillo_actual(void) { return brillo_pct; }

void pantalla_apagar(void)
{
    if (!encendida) {
        return;
    }
    brillo_hw(0);
    esp_lcd_panel_disp_on_off(panel, false);
    comando(0x10);            // SLPIN: el controlador del panel a dormir
    lvgl_port_stop();         // para el tick de 5 ms (y desactiva los temporizadores)
    // Con los temporizadores desactivados lv_timer_handler devuelve "1 ms" y la tarea del port gira sin parar,
    // impidiendo el light sleep. Se reactivan con el de refresco a 1 s: con el tick congelado ninguno vence y la
    // tarea espera lo que falte (hasta 500 ms) por vuelta. El toque sigue llegando por la INT.
    lvgl_port_lock(0);
    lv_timer_set_period(lv_display_get_refr_timer(disp), 1000);
    lv_timer_enable(true);
    lvgl_port_unlock();
    encendida = false;
    ESP_LOGI(TAG, "apagada (panel en sleep, LVGL parado)");
}

void pantalla_encender(void)
{
    if (encendida) {
        return;
    }
    lvgl_port_lock(0);
    lv_timer_set_period(lv_display_get_refr_timer(disp), LV_DEF_REFR_PERIOD);
    lvgl_port_unlock();
    lvgl_port_resume();
    comando(0x11);            // SLPOUT: el panel pide ~120 ms antes de DISPON; mientras, la tarea LVGL repinta
    int64_t t0 = esp_timer_get_time();
    lvgl_port_lock(0);
    lv_obj_invalidate(lv_screen_active());
    xSemaphoreTake(sem_cuadro, 0);        // descarta un cuadro anterior
    lvgl_port_unlock();
    if (xSemaphoreTake(sem_cuadro, pdMS_TO_TICKS(400)) != pdTRUE) {
        ESP_LOGW(TAG, "LVGL no dibujó en 400 ms; enciendo igual");
    }
    int64_t falta_us = 130000 - (esp_timer_get_time() - t0);   // 120 ms del panel + margen para el último flush
    if (falta_us > 0) {
        vTaskDelay(pdMS_TO_TICKS(falta_us / 1000 + 1));
    }
    esp_lcd_panel_disp_on_off(panel, true);
    brillo_hw(brillo_pct);
    encendida = true;
    ESP_LOGI(TAG, "encendida en %d ms", (int)((esp_timer_get_time() - t0) / 1000));
}

bool pantalla_toque_pendiente(void)
{
    bool t = toque_pendiente;
    toque_pendiente = false;
    return t;
}

bool pantalla_encendida(void) { return encendida; }
uint32_t pantalla_ms_sin_tocar(void) { return disp ? lv_display_get_inactive_time(disp) : 0; }

bool pantalla_punto_tocado(int *x, int *y)
{
    if (!indev || lv_indev_get_state(indev) != LV_INDEV_STATE_PRESSED) {
        return false;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    *x = p.x;
    *y = p.y;
    return true;
}

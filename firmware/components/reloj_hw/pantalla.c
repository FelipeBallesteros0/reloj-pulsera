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

static const char *TAG = "pantalla";
static esp_lcd_panel_handle_t panel;
static esp_lcd_panel_io_handle_t io;
static esp_lcd_touch_handle_t tp;
static lv_display_t *disp;
static lv_indev_t *indev;
static int brillo_pct = 80;
static bool encendida;

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
        .flags = { .buff_dma = false, .swap_bytes = true, .sw_rotate = false },
    };
    disp = lvgl_port_add_disp(&dcfg);
    ESP_RETURN_ON_FALSE(disp, ESP_FAIL, TAG, "lvgl_port_add_disp");
    lv_display_add_event_cb(disp, redondear_area, LV_EVENT_INVALIDATE_AREA, NULL);

    ESP_RETURN_ON_ERROR(bsp_touch_new(NULL, &tp), TAG, "táctil");
    const lvgl_port_touch_cfg_t tcfg = { .disp = disp, .handle = tp };
    indev = lvgl_port_add_touch(&tcfg);
    ESP_RETURN_ON_FALSE(indev, ESP_FAIL, TAG, "lvgl_port_add_touch");

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
    encendida = false;
}

void pantalla_encender(void)
{
    if (encendida) {
        return;
    }
    esp_lcd_panel_disp_on_off(panel, true);
    brillo_hw(brillo_pct);
    encendida = true;
}

bool pantalla_encendida(void) { return encendida; }
uint32_t pantalla_ms_sin_tocar(void) { return disp ? lv_display_get_inactive_time(disp) : 0; }

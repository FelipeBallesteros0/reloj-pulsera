// Fase F0: arranque mínimo — BSP + LVGL, "Hola", hora del RTC y medición de RAM libre.
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "rtc_pcf85063.h"
#include "sdkconfig.h"

static const char *TAG = "reloj";

static void log_ram(const char *momento)
{
    ESP_LOGI(TAG, "RAM libre %s: %u B (bloque mayor %u B, mínimo histórico %u B)", momento,
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
             (unsigned)esp_get_minimum_free_heap_size());
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

    lv_display_t *disp = bsp_display_start();
    ESP_ERROR_CHECK(disp ? ESP_OK : ESP_FAIL);
    bsp_display_backlight_on();
    log_ram("tras BSP + LVGL");

    // Hora: si el RTC la conserva, la copiamos al reloj del sistema.
    bool rtc_ok = (rtc_pcf85063_init(bsp_i2c_get_handle()) == ESP_OK);
    struct tm t;
    if (rtc_ok && rtc_pcf85063_get(&t) == ESP_OK) {
        struct timeval tv = { .tv_sec = mktime(&t) };
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "hora del RTC: %04d-%02d-%02d %02d:%02d:%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                 t.tm_hour, t.tm_min, t.tm_sec);
    } else {
        ESP_LOGW(TAG, "RTC sin hora fiable; se mostrará desde 2000 hasta que el iPhone la entregue (F1)");
    }

    bsp_display_lock(0);
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    lv_obj_t *titulo = lv_label_create(scr);
    lv_label_set_text(titulo, "Hola Felipe");
    lv_obj_set_style_text_font(titulo, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(titulo, lv_color_white(), 0);
    lv_obj_align(titulo, LV_ALIGN_TOP_MID, 0, 60);

    lv_obj_t *hora = lv_label_create(scr);
    lv_obj_set_style_text_font(hora, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(hora, lv_color_white(), 0);
    lv_obj_align(hora, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *pie = lv_label_create(scr);
    lv_obj_set_style_text_font(pie, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(pie, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_align(pie, LV_ALIGN_BOTTOM_MID, 0, -40);
    bsp_display_unlock();
    log_ram("con la pantalla dibujada");

    char buf[64];
    while (true) {
        time_t ahora = time(NULL);
        localtime_r(&ahora, &t);
        bsp_display_lock(0);
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
        lv_label_set_text(hora, buf);
        snprintf(buf, sizeof(buf), "%02d/%02d/%04d   RAM libre %u KB", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900,
                 (unsigned)(esp_get_free_heap_size() / 1024));
        lv_label_set_text(pie, buf);
        bsp_display_unlock();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

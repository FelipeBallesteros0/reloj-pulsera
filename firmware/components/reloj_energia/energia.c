#include "energia.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "red.h"
#include "sdkconfig.h"
#if CONFIG_PM_ENABLE
#include "esp_pm.h"
#endif

static const char *TAG = "energia";
static energia_salidas_t actual = { .wifi = false, .cpu_rapida = true, .dormir = false, .lvgl = true };   // arranque
static energia_modo_t modo = ENERGIA_USB;
static int64_t desde_us;
#if CONFIG_PM_ENABLE
static esp_pm_lock_handle_t lock_cpu, lock_sueno;
#endif

esp_err_t energia_init(void)
{
#if CONFIG_PM_ENABLE
    esp_pm_config_t cfg = { .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ, .min_freq_mhz = 40, .light_sleep_enable = true };
    ESP_RETURN_ON_ERROR(esp_pm_configure(&cfg), TAG, "esp_pm_configure");
    ESP_RETURN_ON_ERROR(esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "reloj_cpu", &lock_cpu), TAG, "candado cpu");
    ESP_RETURN_ON_ERROR(esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "reloj_sueno", &lock_sueno), TAG, "candado sueño");
    esp_pm_lock_acquire(lock_cpu);      // coherente con `actual`: rápido y despierto hasta la primera decisión
    esp_pm_lock_acquire(lock_sueno);
    ESP_LOGI(TAG, "PM activo: 40-160 MHz con light sleep automático");
#endif
    desde_us = esp_timer_get_time();
    return ESP_OK;
}

void energia_actualizar(const energia_entradas_t *e)
{
    energia_salidas_t s;
    energia_modo_t m = energia_decidir(e, &s);
    if (s.wifi != actual.wifi) {
        if (s.wifi) {
            red_encender();
        } else {
            red_apagar();
        }
    }
#if CONFIG_PM_ENABLE
    if (s.cpu_rapida != actual.cpu_rapida) {
        if (s.cpu_rapida) esp_pm_lock_acquire(lock_cpu); else esp_pm_lock_release(lock_cpu);
    }
    if (s.dormir != actual.dormir) {
        if (s.dormir) esp_pm_lock_release(lock_sueno); else esp_pm_lock_acquire(lock_sueno);
    }
#endif
    if (m != modo) {
        ESP_LOGI(TAG, "modo %s → %s (usb %d, vigilia %d, pantalla %d por %s, conversación %d): wifi %d, cpu rápida %d, dormir %d",
                 energia_modo_nombre(modo), energia_modo_nombre(m), e->usb, e->vigilia, e->pantalla,
                 e->pantalla_por_usuario ? "usuario" : "aviso", e->conversacion, s.wifi, s.cpu_rapida, s.dormir);
        modo = m;
        desde_us = esp_timer_get_time();
    }
    actual = s;
}

energia_modo_t energia_modo(void) { return modo; }
uint32_t energia_segundos_en_modo(void) { return (uint32_t)((esp_timer_get_time() - desde_us) / 1000000); }

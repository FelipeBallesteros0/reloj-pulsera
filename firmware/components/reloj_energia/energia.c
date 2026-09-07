#include "energia.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "red.h"
#include "sdkconfig.h"
#if CONFIG_PM_ENABLE
#include "esp_pm.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#endif

static const char *TAG = "energia";
static energia_salidas_t actual = { .wifi = false, .cpu_rapida = true, .dormir = false, .lvgl = true };   // arranque
static energia_modo_t modo = ENERGIA_USB;
static int64_t desde_us;
static volatile uint32_t suenos;            // entradas en light sleep desde el último cambio de modo
static volatile int64_t dormido_us;         // tiempo dormido desde el último cambio de modo
static struct { uint32_t segundos, suenos; int dormido_pct; bool hay; } ultimo_reposo;
#if CONFIG_PM_ENABLE
static esp_pm_lock_handle_t lock_cpu, lock_sueno;
#define GPIO_TACTIL_INT 15

// El driver del táctil usa flanco de bajada; el light sleep solo despierta por nivel. Se cambia al entrar y se
// restaura al salir, así el toque saca al chip del sueño y la ISR del port de LVGL sigue funcionando después.
static esp_err_t al_entrar_sueno(int64_t us, void *arg)
{
    (void)us; (void)arg;
    return gpio_wakeup_enable(GPIO_TACTIL_INT, GPIO_INTR_LOW_LEVEL);
}

static esp_err_t al_salir_sueno(int64_t us, void *arg)
{
    (void)arg;
    dormido_us += us;
    suenos++;
    gpio_wakeup_disable(GPIO_TACTIL_INT);
    return gpio_set_intr_type(GPIO_TACTIL_INT, GPIO_INTR_NEGEDGE);
}
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
    esp_pm_sleep_cbs_register_config_t cbs = { .enter_cb = al_entrar_sueno, .exit_cb = al_salir_sueno };
    ESP_RETURN_ON_ERROR(esp_pm_light_sleep_register_cbs(&cbs), TAG, "callbacks de sueño");
    ESP_RETURN_ON_ERROR(esp_sleep_enable_gpio_wakeup(), TAG, "despertar por GPIO");
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
        int64_t ahora = esp_timer_get_time();
        if (modo == ENERGIA_REPOSO) {   // al salir del reposo, guardar cuánto durmió para "Acerca de"
            ultimo_reposo.segundos = (uint32_t)((ahora - desde_us) / 1000000);
            ultimo_reposo.suenos = suenos;
            ultimo_reposo.dormido_pct = ahora > desde_us ? (int)(dormido_us * 100 / (ahora - desde_us)) : 0;
            ultimo_reposo.hay = true;
            ESP_LOGI(TAG, "reposo de %lu s: dormido %d %% en %lu sueños", (unsigned long)ultimo_reposo.segundos,
                     ultimo_reposo.dormido_pct, (unsigned long)ultimo_reposo.suenos);
        }
        modo = m;
        desde_us = ahora;
        suenos = 0;
        dormido_us = 0;
    }
    actual = s;
}

bool energia_ultimo_reposo(uint32_t *segundos, int *dormido_pct, uint32_t *n_suenos)
{
    if (!ultimo_reposo.hay) {
        return false;
    }
    *segundos = ultimo_reposo.segundos;
    *dormido_pct = ultimo_reposo.dormido_pct;
    *n_suenos = ultimo_reposo.suenos;
    return true;
}

energia_modo_t energia_modo(void) { return modo; }
uint32_t energia_segundos_en_modo(void) { return (uint32_t)((esp_timer_get_time() - desde_us) / 1000000); }

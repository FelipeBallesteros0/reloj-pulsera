#include "boton_boot.h"
#include "driver/gpio.h"

#define PIN_BOOT GPIO_NUM_9

esp_err_t boton_boot_init(void)
{
    gpio_config_t c = {
        .pin_bit_mask = 1ULL << PIN_BOOT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&c);
}

bool boton_boot_presionado(void) { return gpio_get_level(PIN_BOOT) == 0; }

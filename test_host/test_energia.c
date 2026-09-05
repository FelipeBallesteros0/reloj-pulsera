#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "energia_politica.h"

static void caso(const char *nombre, energia_entradas_t e, bool wifi, bool cpu, bool dormir, energia_modo_t modo)
{
    energia_salidas_t s;
    energia_modo_t m = energia_decidir(&e, &s);
    if (s.wifi != wifi || s.cpu_rapida != cpu || s.dormir != dormir || s.lvgl != e.pantalla || m != modo) {
        printf("FALLA %s: wifi %d cpu %d dormir %d lvgl %d modo %s\n", nombre, s.wifi, s.cpu_rapida, s.dormir, s.lvgl,
               energia_modo_nombre(m));
        assert(0);
    }
}

int main(void)
{
    //                                                    usb vig pant usr conv    wifi cpu dormir modo
    caso("reposo",                 (energia_entradas_t){ 0, 0, 0, 0, 0 }, 0, 0, 1, ENERGIA_REPOSO);
    caso("pantalla por usuario",   (energia_entradas_t){ 0, 0, 1, 1, 0 }, 1, 1, 0, ENERGIA_ACTIVO);
    caso("pantalla por aviso",     (energia_entradas_t){ 0, 0, 1, 0, 0 }, 0, 1, 0, ENERGIA_ACTIVO);
    caso("usuario sin pantalla",   (energia_entradas_t){ 0, 0, 0, 1, 0 }, 0, 0, 1, ENERGIA_REPOSO);
    caso("conversación a oscuras", (energia_entradas_t){ 0, 0, 0, 0, 1 }, 1, 1, 0, ENERGIA_CONVERSACION);
    caso("vigilia en reposo",      (energia_entradas_t){ 0, 1, 0, 0, 0 }, 1, 0, 0, ENERGIA_VIGILIA);
    caso("vigilia con pantalla",   (energia_entradas_t){ 0, 1, 1, 1, 0 }, 1, 1, 0, ENERGIA_VIGILIA);
    caso("usb en reposo",          (energia_entradas_t){ 1, 0, 0, 0, 0 }, 1, 1, 0, ENERGIA_USB);
    caso("usb y conversación",     (energia_entradas_t){ 1, 0, 1, 1, 1 }, 1, 1, 0, ENERGIA_USB);
    caso("vigilia y conversación", (energia_entradas_t){ 0, 1, 0, 0, 1 }, 1, 1, 0, ENERGIA_CONVERSACION);
    assert(strcmp(energia_modo_nombre(ENERGIA_REPOSO), "reposo") == 0);
    puts("test_energia: OK");
    return 0;
}

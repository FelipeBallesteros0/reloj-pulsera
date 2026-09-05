#include "energia_politica.h"

energia_modo_t energia_decidir(const energia_entradas_t *e, energia_salidas_t *s)
{
    bool activo = e->pantalla && e->pantalla_por_usuario;
    s->wifi = e->usb || e->vigilia || e->conversacion || activo;
    s->cpu_rapida = e->usb || e->pantalla || e->conversacion;
    s->dormir = !e->usb && !e->vigilia && !e->pantalla && !e->conversacion;
    s->lvgl = e->pantalla;
    if (e->usb) return ENERGIA_USB;
    if (e->conversacion) return ENERGIA_CONVERSACION;
    if (e->vigilia) return ENERGIA_VIGILIA;
    if (e->pantalla) return ENERGIA_ACTIVO;
    return ENERGIA_REPOSO;
}

const char *energia_modo_nombre(energia_modo_t m)
{
    static const char *N[] = { "USB", "conversación", "vigilia", "activo", "reposo" };
    return ((unsigned)m <= ENERGIA_REPOSO) ? N[m] : "?";
}

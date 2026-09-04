// Tests del protocolo ANCS, del anillo de notificaciones y de los nombres de app. Ejecutar con: make test
#include <stdio.h>
#include <string.h>
#include "ancs_proto.h"
#include "notif_store.h"
#include "apps.h"

static int fallos = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FALLO %s:%d: %s\n", __FILE__, __LINE__, #cond); fallos++; } } while (0)

// Arma una respuesta GetNotificationAttributes como la manda el iPhone.
static size_t armar_respuesta(uint8_t *out, uint32_t uid, const char *app, const char *tit, const char *sub,
                              const char *msg, const char *fecha)
{
    size_t p = 0;
    out[p++] = ANCS_CMD_GET_NOTIF_ATTRS;
    out[p++] = uid; out[p++] = uid >> 8; out[p++] = uid >> 16; out[p++] = uid >> 24;
    const char *vals[5] = { app, tit, sub, msg, fecha };
    const uint8_t ids[5] = { ANCS_ATTR_APP_ID, ANCS_ATTR_TITULO, ANCS_ATTR_SUBTITULO, ANCS_ATTR_MENSAJE, ANCS_ATTR_FECHA };
    for (int i = 0; i < 5; i++) {
        size_t l = strlen(vals[i]);
        out[p++] = ids[i]; out[p++] = l & 0xFF; out[p++] = l >> 8;
        memcpy(&out[p], vals[i], l); p += l;
    }
    return p;
}

static void test_ns(void)
{
    const uint8_t d[8] = { 0, 0x18, 4, 2, 0x39, 0x30, 0, 0 };   // agregada, acciones +/-, social, uid 12345
    ancs_ns_t ns;
    CHECK(ancs_parse_ns(d, 8, &ns));
    CHECK(ns.evento == ANCS_EV_AGREGADA && ns.flags == 0x18 && ns.categoria == ANCS_CAT_SOCIAL && ns.cantidad == 2 && ns.uid == 12345);
    CHECK(!ancs_parse_ns(d, 7, &ns));
    uint8_t mal[8] = { 5, 0, 0, 0, 1, 0, 0, 0 };
    CHECK(!ancs_parse_ns(mal, 8, &ns));
}

static void test_comandos(void)
{
    uint8_t c[32];
    size_t n = ancs_cmd_get_attrs(c, sizeof(c), 0x04030201);
    const uint8_t esperado[] = { 0, 1, 2, 3, 4, 0, 1, 64, 0, 2, 48, 0, 3, 192, 0, 5 };
    CHECK(n == sizeof(esperado) && memcmp(c, esperado, n) == 0);
    CHECK(ancs_cmd_get_attrs(c, 10, 1) == 0);
    n = ancs_cmd_accion(c, sizeof(c), 7, ANCS_ACCION_NEGATIVA);
    CHECK(n == 6 && c[0] == 2 && c[1] == 7 && c[5] == 1);
    n = ancs_cmd_get_app_attrs(c, sizeof(c), "com.foo.Bar");
    CHECK(n == 1 + 11 + 1 + 1 && c[0] == 1 && memcmp(&c[1], "com.foo.Bar", 12) == 0 && c[13] == 0);
}

static void test_fragmentos(void)
{
    uint8_t resp[400];
    size_t total = armar_respuesta(resp, 4242, "net.whatsapp.WhatsApp", "Mamá", "", "¿Vienes a cenar? Sí, ñam…", "20260903T172805");
    const size_t tamanos[] = { 1, 7, 20, 100, 400 };
    for (size_t t = 0; t < sizeof(tamanos) / sizeof(tamanos[0]); t++) {
        ancs_ds_t a;
        ancs_ds_reset(&a);
        int r = 0;
        size_t pos = 0;
        int veces = 0;
        while (pos < total) {
            size_t n = total - pos < tamanos[t] ? total - pos : tamanos[t];
            r = ancs_ds_alimentar(&a, &resp[pos], n);
            pos += n;
            if (pos < total) {
                CHECK(r == 0);
            }
            veces++;
        }
        CHECK(r == 1);
        ancs_attrs_t at;
        CHECK(ancs_parse_attrs(a.buf, a.len, &at));
        CHECK(at.uid == 4242);
        CHECK(strcmp(at.app_id, "net.whatsapp.WhatsApp") == 0);
        CHECK(strcmp(at.titulo, "Mamá") == 0);
        CHECK(at.subtitulo[0] == '\0');
        CHECK(strcmp(at.mensaje, "¿Vienes a cenar? Sí, ñam…") == 0);
        CHECK(strcmp(at.fecha, "20260903T172805") == 0);
    }
    // Desborde
    ancs_ds_t a;
    ancs_ds_reset(&a);
    uint8_t basura[ANCS_DS_CAP + 1] = { 0 };
    CHECK(ancs_ds_alimentar(&a, basura, sizeof(basura)) == -1);
    // Comando desconocido
    ancs_ds_reset(&a);
    uint8_t raro[1] = { 9 };
    CHECK(ancs_ds_alimentar(&a, raro, 1) == -1);
}

static void test_truncado_utf8(void)
{
    // 300 × "á" (2 bytes) = 600 bytes de mensaje: el iPhone respeta nuestro máximo (192), pero probamos el recorte igual.
    char largo[601];
    for (int i = 0; i < 300; i++) { largo[2 * i] = (char)0xC3; largo[2 * i + 1] = (char)0xA1; }
    largo[600] = '\0';
    uint8_t resp[700];
    size_t total = armar_respuesta(resp, 1, "a", "t", "", largo, "");
    ancs_attrs_t at;
    CHECK(ancs_parse_attrs(resp, total, &at));
    size_t l = strlen(at.mensaje);
    CHECK(l <= ANCS_LARGO_MENSAJE && l % 2 == 0 && l >= ANCS_LARGO_MENSAJE - 1);
    CHECK((unsigned char)at.mensaje[l - 2] == 0xC3 && (unsigned char)at.mensaje[l - 1] == 0xA1);
}

static void test_app_attrs(void)
{
    uint8_t resp[64];
    size_t p = 0;
    resp[p++] = ANCS_CMD_GET_APP_ATTRS;
    memcpy(&resp[p], "com.foo.Bar", 12); p += 12;
    resp[p++] = 0; resp[p++] = 3; resp[p++] = 0; memcpy(&resp[p], "Bar", 3); p += 3;
    ancs_ds_t a;
    ancs_ds_reset(&a);
    CHECK(ancs_ds_alimentar(&a, resp, 5) == 0);
    CHECK(ancs_ds_alimentar(&a, resp + 5, p - 5) == 1);
    char id[48], nombre[32];
    CHECK(ancs_parse_app_attrs(a.buf, a.len, id, sizeof(id), nombre, sizeof(nombre)));
    CHECK(strcmp(id, "com.foo.Bar") == 0 && strcmp(nombre, "Bar") == 0);
}

static void test_anillo(void)
{
    notif_store_init();
    for (uint32_t u = 1; u <= 30; u++) {
        CHECK(notif_store_agregar(u, ANCS_CAT_OTRA, 0, u * 10));
    }
    CHECK(notif_store_cantidad() == NOTIF_MAX);
    notif_t n;
    CHECK(notif_store_obtener(0, &n) && n.uid == 30);
    CHECK(notif_store_obtener(NOTIF_MAX - 1, &n) && n.uid == 30 - NOTIF_MAX + 1);
    CHECK(!notif_store_obtener(NOTIF_MAX, &n));
    CHECK(!notif_store_buscar(3, &n));                 // cayó del anillo
    CHECK(!notif_store_quitar(999));
    CHECK(notif_store_quitar(30) && notif_store_cantidad() == NOTIF_MAX - 1);
    CHECK(notif_store_agregar(0, 0, 0, 0) && notif_store_cantidad() == NOTIF_MAX);   // el uid 0 es válido
    CHECK(notif_store_quitar(0) && notif_store_cantidad() == NOTIF_MAX - 1);
    ancs_attrs_t at = { .uid = 20 };
    strcpy(at.titulo, "Hola");
    strcpy(at.mensaje, "qué tal");
    CHECK(notif_store_poner_detalle(20, &at));
    CHECK(!notif_store_poner_detalle(999, &at));
    CHECK(notif_store_buscar(20, &n) && n.con_detalle && strcmp(n.titulo, "Hola") == 0);
    // Modificada: vuelve al frente y conserva el detalle
    CHECK(notif_store_agregar(20, ANCS_CAT_CORREO, ANCS_FLAG_SILENCIOSA, 999));
    CHECK(notif_store_cantidad() == NOTIF_MAX - 1);
    CHECK(notif_store_obtener(0, &n) && n.uid == 20 && n.con_detalle && n.categoria == ANCS_CAT_CORREO && strcmp(n.mensaje, "qué tal") == 0);
    CHECK(notif_store_no_leidas() == NOTIF_MAX - 1);
    CHECK(notif_store_marcar_leida(20) && notif_store_no_leidas() == NOTIF_MAX - 2);
    notif_store_marcar_todas_leidas();
    CHECK(notif_store_no_leidas() == 0);
    notif_store_vaciar();
    CHECK(notif_store_cantidad() == 0);
}

static void test_apps(void)
{
    char buf[32];
    CHECK(strcmp(apps_nombre("net.whatsapp.WhatsApp"), "WhatsApp") == 0);
    CHECK(apps_nombre("com.desconocida.App") == NULL);
    CHECK(strcmp(apps_nombre_o_bundle("com.desconocida.App", buf, sizeof(buf)), "App") == 0);
    CHECK(strcmp(apps_nombre_o_bundle("", buf, sizeof(buf)), "App") == 0);
    CHECK(!apps_en_cache("com.desconocida.App"));
    apps_cachear("com.desconocida.App", "Desconocida");
    CHECK(apps_en_cache("com.desconocida.App"));
    CHECK(strcmp(apps_nombre("com.desconocida.App"), "Desconocida") == 0);
    for (int i = 0; i < 20; i++) {   // la caché rota sin romperse
        char id[32]; snprintf(id, sizeof(id), "com.x.app%d", i);
        apps_cachear(id, "X");
    }
    CHECK(strcmp(apps_nombre("com.x.app19"), "X") == 0);
}

int main(void)
{
    test_ns();
    test_comandos();
    test_fragmentos();
    test_truncado_utf8();
    test_app_attrs();
    test_anillo();
    test_apps();
    printf(fallos ? "test_ancs: %d fallos\n" : "test_ancs: OK\n", fallos);
    return fallos ? 1 : 0;
}

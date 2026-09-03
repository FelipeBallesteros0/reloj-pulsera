#include "ancs_proto.h"
#include <string.h>

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

void ancs_copiar_utf8(char *dst, size_t cap, const uint8_t *src, size_t n)
{
    if (cap == 0) {
        return;
    }
    size_t m = n < cap - 1 ? n : cap - 1;
    // Si cortamos en medio de un carácter multibyte, retrocedemos hasta un inicio de carácter.
    if (m < n) {
        while (m > 0 && (src[m] & 0xC0) == 0x80) {
            m--;
        }
    }
    memcpy(dst, src, m);
    dst[m] = '\0';
}

bool ancs_parse_ns(const uint8_t *d, size_t n, ancs_ns_t *out)
{
    if (!d || !out || n < 8 || d[0] > ANCS_EV_QUITADA) {
        return false;
    }
    out->evento = d[0];
    out->flags = d[1];
    out->categoria = d[2];
    out->cantidad = d[3];
    out->uid = le32(&d[4]);
    return true;
}

void ancs_ds_reset(ancs_ds_t *a) { a->len = 0; }

// Recorre la lista de atributos [id(1) len(2) datos(len)]* a partir de d[pos]. Devuelve 1 si la lista está completa
// y termina exactamente en n, 0 si faltan bytes, -1 si está mal formada.
static int recorrer_attrs(const uint8_t *d, size_t n, size_t pos, int esperados)
{
    int vistos = 0;
    while (vistos < esperados) {
        if (pos + 3 > n) {
            return 0;
        }
        uint16_t len = le16(&d[pos + 1]);
        pos += 3;
        if (pos + len > n) {
            return 0;
        }
        pos += len;
        vistos++;
    }
    return pos == n ? 1 : -1;
}

int ancs_ds_alimentar(ancs_ds_t *a, const uint8_t *frag, size_t n)
{
    if (!frag || a->len + n > ANCS_DS_CAP) {
        return -1;
    }
    memcpy(&a->buf[a->len], frag, n);
    a->len += n;
    if (a->len < 1) {
        return 0;
    }
    switch (a->buf[0]) {
    case ANCS_CMD_GET_NOTIF_ATTRS:
        if (a->len < 5) {
            return 0;
        }
        return recorrer_attrs(a->buf, a->len, 5, 5);   // pedimos siempre 5 atributos
    case ANCS_CMD_GET_APP_ATTRS: {
        // CommandID + AppIdentifier (cadena NUL) + 1 atributo
        size_t pos = 1;
        while (pos < a->len && a->buf[pos] != '\0') {
            pos++;
        }
        if (pos >= a->len) {
            return 0;
        }
        return recorrer_attrs(a->buf, a->len, pos + 1, 1);
    }
    default:
        return -1;
    }
}

bool ancs_parse_attrs(const uint8_t *d, size_t n, ancs_attrs_t *out)
{
    if (!d || !out || n < 5 || d[0] != ANCS_CMD_GET_NOTIF_ATTRS) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->uid = le32(&d[1]);
    size_t pos = 5;
    while (pos + 3 <= n) {
        uint8_t id = d[pos];
        uint16_t len = le16(&d[pos + 1]);
        pos += 3;
        if (pos + len > n) {
            return false;
        }
        const uint8_t *v = &d[pos];
        switch (id) {
        case ANCS_ATTR_APP_ID:    ancs_copiar_utf8(out->app_id, sizeof(out->app_id), v, len); break;
        case ANCS_ATTR_TITULO:    ancs_copiar_utf8(out->titulo, sizeof(out->titulo), v, len); break;
        case ANCS_ATTR_SUBTITULO: ancs_copiar_utf8(out->subtitulo, sizeof(out->subtitulo), v, len); break;
        case ANCS_ATTR_MENSAJE:   ancs_copiar_utf8(out->mensaje, sizeof(out->mensaje), v, len); break;
        case ANCS_ATTR_FECHA:     ancs_copiar_utf8(out->fecha, sizeof(out->fecha), v, len); break;
        default: break;
        }
        pos += len;
    }
    return pos == n;
}

bool ancs_parse_app_attrs(const uint8_t *d, size_t n, char *app_id, size_t cap_id, char *nombre, size_t cap_nombre)
{
    if (!d || n < 2 || d[0] != ANCS_CMD_GET_APP_ATTRS) {
        return false;
    }
    size_t pos = 1;
    while (pos < n && d[pos] != '\0') {
        pos++;
    }
    if (pos >= n) {
        return false;
    }
    ancs_copiar_utf8(app_id, cap_id, &d[1], pos - 1);
    pos++;
    nombre[0] = '\0';
    while (pos + 3 <= n) {
        uint8_t id = d[pos];
        uint16_t len = le16(&d[pos + 1]);
        pos += 3;
        if (pos + len > n) {
            return false;
        }
        if (id == 0) {   // AppAttributeIDDisplayName
            ancs_copiar_utf8(nombre, cap_nombre, &d[pos], len);
        }
        pos += len;
    }
    return pos == n;
}

size_t ancs_cmd_get_attrs(uint8_t *out, size_t cap, uint32_t uid)
{
    // CommandID, UID, AppIdentifier, Title(max), Subtitle(max), Message(max), Date
    const uint8_t cmd[] = {
        ANCS_CMD_GET_NOTIF_ATTRS,
        (uint8_t)uid, (uint8_t)(uid >> 8), (uint8_t)(uid >> 16), (uint8_t)(uid >> 24),
        ANCS_ATTR_APP_ID,
        ANCS_ATTR_TITULO, ANCS_LARGO_TITULO & 0xFF, ANCS_LARGO_TITULO >> 8,
        ANCS_ATTR_SUBTITULO, ANCS_LARGO_SUBTITULO & 0xFF, ANCS_LARGO_SUBTITULO >> 8,
        ANCS_ATTR_MENSAJE, ANCS_LARGO_MENSAJE & 0xFF, ANCS_LARGO_MENSAJE >> 8,
        ANCS_ATTR_FECHA,
    };
    if (cap < sizeof(cmd)) {
        return 0;
    }
    memcpy(out, cmd, sizeof(cmd));
    return sizeof(cmd);
}

size_t ancs_cmd_get_app_attrs(uint8_t *out, size_t cap, const char *app_id)
{
    size_t l = strlen(app_id);
    if (cap < 1 + l + 1 + 1) {
        return 0;
    }
    out[0] = ANCS_CMD_GET_APP_ATTRS;
    memcpy(&out[1], app_id, l + 1);   // con el NUL
    out[1 + l + 1] = 0;               // AppAttributeIDDisplayName
    return 1 + l + 1 + 1;
}

size_t ancs_cmd_accion(uint8_t *out, size_t cap, uint32_t uid, uint8_t accion)
{
    if (cap < 6) {
        return 0;
    }
    out[0] = ANCS_CMD_ACCION;
    out[1] = (uint8_t)uid; out[2] = (uint8_t)(uid >> 8); out[3] = (uint8_t)(uid >> 16); out[4] = (uint8_t)(uid >> 24);
    out[5] = accion;
    return 6;
}

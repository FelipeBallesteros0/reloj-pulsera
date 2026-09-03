#include "notif_store.h"
#include <string.h>

static notif_t anillo[NOTIF_MAX];   // ordenado: [0] = más reciente
static int cantidad;
static void (*f_lock)(void);
static void (*f_unlock)(void);

#define LOCK()   do { if (f_lock) f_lock(); } while (0)
#define UNLOCK() do { if (f_unlock) f_unlock(); } while (0)

void notif_store_set_lock(void (*lock)(void), void (*unlock)(void)) { f_lock = lock; f_unlock = unlock; }
void notif_store_init(void) { LOCK(); cantidad = 0; UNLOCK(); }
void notif_store_vaciar(void) { notif_store_init(); }

static int indice(uint32_t uid)
{
    for (int i = 0; i < cantidad; i++) {
        if (anillo[i].uid == uid) {
            return i;
        }
    }
    return -1;
}

static void quitar_indice(int i)
{
    memmove(&anillo[i], &anillo[i + 1], (size_t)(cantidad - i - 1) * sizeof(notif_t));
    cantidad--;
}

bool notif_store_agregar(uint32_t uid, uint8_t categoria, uint8_t flags, uint32_t ahora_ms)
{
    // Ojo: el UID 0 es válido (iOS numera desde 0 en cada conexión).
    LOCK();
    notif_t nueva;
    int i = indice(uid);
    if (i >= 0) {
        nueva = anillo[i];          // conserva título/mensaje si ya los teníamos
        quitar_indice(i);
    } else {
        memset(&nueva, 0, sizeof(nueva));
        nueva.uid = uid;
    }
    nueva.categoria = categoria;
    nueva.flags = flags;
    nueva.recibida_ms = ahora_ms;
    if (cantidad == NOTIF_MAX) {
        cantidad--;                 // cae la más antigua
    }
    memmove(&anillo[1], &anillo[0], (size_t)cantidad * sizeof(notif_t));
    anillo[0] = nueva;
    cantidad++;
    UNLOCK();
    return true;
}

bool notif_store_poner_detalle(uint32_t uid, const ancs_attrs_t *a)
{
    LOCK();
    int i = indice(uid);
    if (i < 0) {
        UNLOCK();
        return false;
    }
    notif_t *n = &anillo[i];
    memcpy(n->app_id, a->app_id, sizeof(n->app_id));
    memcpy(n->titulo, a->titulo, sizeof(n->titulo));
    memcpy(n->subtitulo, a->subtitulo, sizeof(n->subtitulo));
    memcpy(n->mensaje, a->mensaje, sizeof(n->mensaje));
    memcpy(n->fecha, a->fecha, sizeof(n->fecha));
    n->con_detalle = true;
    UNLOCK();
    return true;
}

bool notif_store_quitar(uint32_t uid)
{
    LOCK();
    int i = indice(uid);
    if (i >= 0) {
        quitar_indice(i);
    }
    UNLOCK();
    return i >= 0;
}

bool notif_store_marcar_leida(uint32_t uid)
{
    LOCK();
    int i = indice(uid);
    if (i >= 0) {
        anillo[i].leida = true;
    }
    UNLOCK();
    return i >= 0;
}

void notif_store_marcar_todas_leidas(void)
{
    LOCK();
    for (int i = 0; i < cantidad; i++) {
        anillo[i].leida = true;
    }
    UNLOCK();
}

int notif_store_cantidad(void) { return cantidad; }

int notif_store_no_leidas(void)
{
    LOCK();
    int n = 0;
    for (int i = 0; i < cantidad; i++) {
        if (!anillo[i].leida) {
            n++;
        }
    }
    UNLOCK();
    return n;
}

bool notif_store_obtener(int i, notif_t *out)
{
    LOCK();
    bool ok = i >= 0 && i < cantidad;
    if (ok) {
        *out = anillo[i];
    }
    UNLOCK();
    return ok;
}

bool notif_store_buscar(uint32_t uid, notif_t *out)
{
    LOCK();
    int i = indice(uid);
    if (i >= 0) {
        *out = anillo[i];
    }
    UNLOCK();
    return i >= 0;
}

#include "apps.h"
#include <string.h>
#include <stdio.h>

static const struct { const char *id; const char *nombre; } TABLA[] = {
    { "cl.reloj.andia", "ANDIA" },
    { "net.whatsapp.WhatsApp", "WhatsApp" },
    { "com.apple.MobileSMS", "Mensajes" },
    { "com.apple.mobilephone", "Teléfono" },
    { "com.apple.facetime", "FaceTime" },
    { "com.apple.mobilemail", "Mail" },
    { "com.apple.mobilecal", "Calendario" },
    { "com.apple.reminders", "Recordatorios" },
    { "com.apple.Preferences", "Ajustes" },
    { "com.apple.AppStore", "App Store" },
    { "com.apple.Health", "Salud" },
    { "com.apple.Maps", "Mapas" },
    { "com.apple.findmy", "Buscar" },
    { "com.apple.Passbook", "Wallet" },
    { "com.burbn.instagram", "Instagram" },
    { "com.facebook.Facebook", "Facebook" },
    { "com.facebook.Messenger", "Messenger" },
    { "ph.telegra.Telegraph", "Telegram" },
    { "com.hammerandchisel.discord", "Discord" },
    { "com.google.Gmail", "Gmail" },
    { "com.google.ios.youtube", "YouTube" },
    { "com.atebits.Tweetie2", "X" },
    { "com.zhiliaoapp.musically", "TikTok" },
    { "com.spotify.client", "Spotify" },
    { "com.ubercab.UberClient", "Uber" },
    { "com.toyopagroup.picaboo", "Snapchat" },
    { "com.linkedin.LinkedIn", "LinkedIn" },
    { "com.tinyspeck.chatlyio", "Slack" },
    { "com.microsoft.Office.Outlook", "Outlook" },
    { "com.apple.shortcuts", "Atajos" },
};

#define CACHE_MAX 16
static struct { char id[48]; char nombre[32]; } cache[CACHE_MAX];
static int cache_n, cache_sig;

const char *apps_nombre(const char *bundle_id)
{
    if (!bundle_id || !bundle_id[0]) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(TABLA) / sizeof(TABLA[0]); i++) {
        if (strcmp(TABLA[i].id, bundle_id) == 0) {
            return TABLA[i].nombre;
        }
    }
    for (int i = 0; i < cache_n; i++) {
        if (strcmp(cache[i].id, bundle_id) == 0) {
            return cache[i].nombre;
        }
    }
    return NULL;
}

const char *apps_nombre_o_bundle(const char *bundle_id, char *buf, int cap)
{
    const char *n = apps_nombre(bundle_id);
    if (n) {
        return n;
    }
    if (!bundle_id || !bundle_id[0]) {
        return "App";
    }
    const char *p = strrchr(bundle_id, '.');
    snprintf(buf, (size_t)cap, "%s", p ? p + 1 : bundle_id);
    return buf;
}

bool apps_en_cache(const char *bundle_id)
{
    for (int i = 0; i < cache_n; i++) {
        if (strcmp(cache[i].id, bundle_id) == 0) {
            return true;
        }
    }
    return false;
}

void apps_cachear(const char *bundle_id, const char *nombre)
{
    if (!bundle_id || !nombre || !nombre[0]) {
        return;
    }
    for (int i = 0; i < cache_n; i++) {
        if (strcmp(cache[i].id, bundle_id) == 0) {
            snprintf(cache[i].nombre, sizeof(cache[i].nombre), "%s", nombre);
            return;
        }
    }
    int i = cache_n < CACHE_MAX ? cache_n++ : cache_sig;
    cache_sig = (i + 1) % CACHE_MAX;
    snprintf(cache[i].id, sizeof(cache[i].id), "%s", bundle_id);
    snprintf(cache[i].nombre, sizeof(cache[i].nombre), "%s", nombre);
}

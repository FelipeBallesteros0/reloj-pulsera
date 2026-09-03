#pragma once
// Nombre legible de las apps del iPhone a partir de su bundle id (tabla local + caché de las que responde ANCS).
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
// Devuelve el nombre conocido (tabla o caché) o NULL si no lo conocemos.
const char *apps_nombre(const char *bundle_id);
// Nombre para mostrar: el conocido, o el último tramo del bundle id ("com.foo.Bar" → "Bar").
const char *apps_nombre_o_bundle(const char *bundle_id, char *buf, int cap);
void apps_cachear(const char *bundle_id, const char *nombre);
bool apps_en_cache(const char *bundle_id);
#ifdef __cplusplus
}
#endif

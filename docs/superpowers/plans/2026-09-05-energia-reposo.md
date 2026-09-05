# Energía en reposo (F4, primera parte): plan de implementación

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Que el reloj, a batería, con la pantalla apagada, sin conversación y con la vigilia "Oye Andia" desactivada,
apague WiFi, códec, panel, LVGL y giroscopio y duerma la CPU entre eventos manteniendo solo el BLE con el iPhone;
con medición de la descarga en %/h antes y después de cada etapa.

**Architecture:** Un componente nuevo `reloj_energia` con una regla pura (`energia_politica.c`, sin ESP-IDF, testeada
en host) que decide WiFi / CPU rápida / dormir / LVGL a partir de USB, vigilia, pantalla (y su motivo) y conversación,
y un pegamento (`energia.c`) que aplica la decisión con candados de PM y `red_encender/apagar`. Cada driver gana su
par dormir/despertar: `red` arranca y para el WiFi, `voz` abre y cierra el códec por demanda, `pantalla` pone el panel
en sleep y para LVGL, `imu` enciende los sensores solo en la app Sensores. ANDIA deja de bloquear al conectar. Tres
etapas medibles: 0 (medir), 1 (apagar periféricos), 2 (PM de ESP-IDF con light sleep y sueño del BLE).

**Tech Stack:** ESP-IDF v5.5.5 (esp32c6), LVGL 9.5 + `esp_lvgl_port` (stop/resume, indev en modo evento), NimBLE,
`esp_codec_dev` (ES8311/ES7210), `esp_pm` (etapa 2), gcc en WSL para los tests en host (`test_host/`).

**Spec:** `docs/superpowers/specs/2026-09-05-energia-reposo-diseno.md` (secciones 4 a 10). Este plan la implementa
entera; la sección 11 (fuera de alcance) no entra.

## Global Constraints

- Compilar en WSL, flashear y leer la consola desde Windows: `. ~/esp/esp-idf/export.sh && cd ~/reloj_pulsera/firmware && idf.py build`,
  `../herramientas/flash_win.sh`, `../herramientas/monitor_win.sh 20 COM3 --reset` (la consola solo sale si el
  puerto está abierto desde el arranque: siempre `--reset`). Capturas largas: `SALIDA=C:/ruta/consola.log ../herramientas/monitor_win.sh 1200 COM3 --reset`.
- **`firmware/sdkconfig` no está versionado y guarda las claves WiFi (`CONFIG_RELOJ_WIFI1_*`): no borrarlo nunca.**
  Los cambios de configuración van a `sdkconfig.defaults` (repo) y se aplican al `sdkconfig` local editándolo (tarea 12).
- Solo la tarea principal toca LVGL, siempre bajo `pantalla_lock(0)` / `pantalla_unlock()`. Los callbacks de la UI
  (`cb_*` en `main.c`) corren en la tarea de LVGL.
- RAM ajustada (TLS del túnel): sin tareas nuevas ni búferes grandes; el registro de batería ocupa ~1 KB.
- Código, comentarios, logs y commits en español, como el resto del repo. Mensajes de commit terminan con
  `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>` y `Claude-Session: https://claude.ai/code/session_01X7K61kpop9m6NkG19dUB1W`.
- Todo cambio de firmware se compila, se flashea y se verifica en el reloj antes del commit. Los tests en host se
  corren con `cd ~/reloj_pulsera/test_host && make test`.
- Vigilia por defecto desactivada; el ajuste guardado en NVS manda. **El reloj de Felipe la tiene guardada activa**:
  para probar el reposo hay que desactivarla una vez en Configuración → ANDIA.
- Medición: el AXP2101 no mide corriente; se usa el % del medidor de carga. 1 %/h ≈ 4 mA (400 mAh).

---

## Etapa 0 — Medir

### Task 1: Registro de batería (C puro) con test en host

**Files:**
- Create: `firmware/components/reloj_energia/CMakeLists.txt`
- Create: `firmware/components/reloj_energia/include/bateria_registro.h`
- Create: `firmware/components/reloj_energia/bateria_registro.c`
- Create: `test_host/test_bateria.c`
- Modify: `test_host/Makefile`

**Interfaces:**
- Produces: `bateria_registro_t`, `bateria_registro_init(r)`, `bateria_registro_agregar(r, mv, pct)` (una muestra por
  minuto), `bool bateria_registro_descarga(r, minutos, int *decimas_pct_h)` (false si no hay al menos media ventana).

- [ ] **Step 1: Escribir la cabecera**

```c
// firmware/components/reloj_energia/include/bateria_registro.h
#pragma once
// Registro en RAM de la batería (una muestra por minuto) para medir la descarga en %/h sin consola ni USB.
// C puro: se testea en host.
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define BATERIA_REGISTRO_N 240          // 4 h a una muestra por minuto (~1 KB)
typedef struct {
    uint16_t mv[BATERIA_REGISTRO_N];
    uint8_t pct[BATERIA_REGISTRO_N];
    uint16_t n;                         // muestras guardadas (≤ N)
    uint16_t sig;                       // índice de la próxima escritura (anillo)
} bateria_registro_t;
void bateria_registro_init(bateria_registro_t *r);
void bateria_registro_agregar(bateria_registro_t *r, uint16_t mv, uint8_t pct);   // llamar una vez por minuto
// Descarga en décimas de %/h entre la muestra de hace `minutos` (o la más vieja, si hay al menos media ventana)
// y la última. false si no hay muestras suficientes.
bool bateria_registro_descarga(const bateria_registro_t *r, int minutos, int *decimas_pct_h);
#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: Escribir el test (falla: no existe la implementación)**

```c
// test_host/test_bateria.c
#include <assert.h>
#include <stdio.h>
#include "bateria_registro.h"

int main(void)
{
    bateria_registro_t r;
    bateria_registro_init(&r);
    int d = 999;
    assert(!bateria_registro_descarga(&r, 30, &d));               // vacío
    bateria_registro_agregar(&r, 4000, 80);
    assert(!bateria_registro_descarga(&r, 30, &d));               // una sola muestra: sin lapso
    // 30 muestras más: de 80 % a 78 % en 30 min → 4,0 %/h
    for (int i = 1; i <= 30; i++) {
        bateria_registro_agregar(&r, 4000, (uint8_t)(80 - (i * 2) / 30));
    }
    assert(bateria_registro_descarga(&r, 30, &d) && d == 40);
    assert(!bateria_registro_descarga(&r, 120, &d));              // 30 min no llegan a media ventana de 2 h
    // 300 muestras constantes: el anillo da la vuelta y la descarga es 0
    for (int i = 0; i < 300; i++) {
        bateria_registro_agregar(&r, 3900, 50);
    }
    assert(r.n == BATERIA_REGISTRO_N);
    assert(bateria_registro_descarga(&r, 120, &d) && d == 0);
    // Cargando: la descarga sale negativa
    for (int i = 0; i < 61; i++) {
        bateria_registro_agregar(&r, 4100, (uint8_t)(50 + i / 6));
    }
    assert(bateria_registro_descarga(&r, 60, &d) && d < 0);
    puts("test_bateria: OK");
    return 0;
}
```

- [ ] **Step 3: Añadir el test al Makefile**

En `test_host/Makefile`: añadir `-I../firmware/components/reloj_energia/include` al final de la línea `CFLAGS ?=`,
la variable `ENERGIA = ../firmware/components/reloj_energia` junto a `VOZ`, `test_bateria` a la lista de `test:` (y su
`./test_bateria`), la regla y el `clean`:

```make
test_bateria: test_bateria.c $(ENERGIA)/bateria_registro.c
	$(CC) $(CFLAGS) -o $@ $^
```

- [ ] **Step 4: Correr el test y ver que falla**

Run: `cd ~/reloj_pulsera/test_host && make test_bateria`
Expected: error de compilación, no existe `bateria_registro.c`.

- [ ] **Step 5: Implementar**

```c
// firmware/components/reloj_energia/bateria_registro.c
#include "bateria_registro.h"
#include <string.h>

void bateria_registro_init(bateria_registro_t *r) { memset(r, 0, sizeof(*r)); }

void bateria_registro_agregar(bateria_registro_t *r, uint16_t mv, uint8_t pct)
{
    r->mv[r->sig] = mv;
    r->pct[r->sig] = pct;
    r->sig = (uint16_t)((r->sig + 1) % BATERIA_REGISTRO_N);
    if (r->n < BATERIA_REGISTRO_N) {
        r->n++;
    }
}

// Índice de la muestra guardada hace `atras` minutos (0 = la última).
static uint16_t indice(const bateria_registro_t *r, int atras)
{
    return (uint16_t)((r->sig + BATERIA_REGISTRO_N - 1 - atras) % BATERIA_REGISTRO_N);
}

bool bateria_registro_descarga(const bateria_registro_t *r, int minutos, int *decimas_pct_h)
{
    int k = (minutos + 1 < r->n) ? minutos + 1 : r->n;   // muestras que caben en la ventana
    int lapso = k - 1;                                    // minutos entre la más vieja y la última
    if (lapso < 1 || lapso * 2 < minutos) {               // hace falta al menos media ventana
        return false;
    }
    int caida = (int)r->pct[indice(r, lapso)] - (int)r->pct[indice(r, 0)];
    *decimas_pct_h = caida * 600 / lapso;                 // % por minuto × 60 × 10
    return true;
}
```

```cmake
# firmware/components/reloj_energia/CMakeLists.txt
idf_component_register(SRCS "bateria_registro.c"
                       INCLUDE_DIRS "include")
```

- [ ] **Step 6: Correr los tests y ver que pasan**

Run: `cd ~/reloj_pulsera/test_host && make test`
Expected: las cinco líneas `...: OK`, incluida `test_bateria: OK`.

- [ ] **Step 7: Commit**

```bash
cd ~/reloj_pulsera && git add firmware/components/reloj_energia test_host/test_bateria.c test_host/Makefile
git commit -m "Energía: registro de batería en RAM (una muestra por minuto, descarga en %/h) con test en host"
```

### Task 2: Muestreo en el reloj y línea de descarga en "Acerca de"

**Files:**
- Modify: `firmware/main/main.c` (includes, estáticos, `refrescar_bateria`, bucle principal)
- Modify: `firmware/main/CMakeLists.txt` (REQUIRES + `reloj_energia`)
- Modify: `firmware/components/reloj_ui/include/ui.h`, `firmware/components/reloj_ui/ui.c` (`ui_energia`)

**Interfaces:**
- Consumes: `bateria_registro_*` (tarea 1).
- Produces: `void ui_energia(const char *texto)` (texto libre de una o dos líneas que se muestra en "Acerca de").

- [ ] **Step 1: `ui_energia` en la UI**

En `ui.h`, junto a `ui_ram`: `void ui_energia(const char *texto);   // línea(s) de energía en "Acerca de"`.

En `ui.c`: añadir `char energia[96];` como último campo del `struct` `aj` (línea ~72) y `"Descarga: --"` como último
valor del inicializador. En `texto_acerca` (línea ~596) cambiar el formato a
`"Reloj\nVersión %s\nRAM libre %u KB\nESP32-C6 · AMOLED 410×502\nWiFi %s\n%s"` añadiendo `aj.energia` como
último argumento. Junto a `ui_ram` (línea ~1364):

```c
void ui_energia(const char *texto)
{
    snprintf(aj.energia, sizeof(aj.energia), "%s", texto);
    refrescar_acerca();
}
```

- [ ] **Step 2: Muestreo en `main.c`**

Includes: `#include "bateria_registro.h"`. Estáticos (junto a `t_apagado_ms`): `static bateria_registro_t reg_bat;`.
En `app_main`, tras `ajustes_cargar();`: `bateria_registro_init(&reg_bat);`. Antes de `refrescar_bateria`:

```c
static void texto_descarga(char *b, size_t cap, int minutos)
{
    int d;
    if (bateria_registro_descarga(&reg_bat, minutos, &d)) {
        snprintf(b, cap, "%s%d,%d %%/h", d < 0 ? "-" : "", abs(d) / 10, abs(d) % 10);
    } else {
        snprintf(b, cap, "--");
    }
}

// Texto de energía para "Acerca de" (una muestra de batería por minuto; ver bateria_registro).
static void refrescar_energia_ui(void)
{
    char a[16], c[16], t[96];
    texto_descarga(a, sizeof(a), 30);
    texto_descarga(c, sizeof(c), 120);
    snprintf(t, sizeof(t), "Descarga %s (30 min) · %s (2 h)", a, c);
    pantalla_lock(0);
    ui_energia(t);
    pantalla_unlock();
}
```

En el bucle principal, junto a `ultimo_bat`: `int64_t ultimo_muestra = -60000;` y, tras el bloque de `ultimo_bat`:

```c
        if (ahora_ms() - ultimo_muestra >= 60000) {
            ultimo_muestra = ahora_ms();
            if (pmic_bateria_pct() >= 0) {
                bateria_registro_agregar(&reg_bat, (uint16_t)pmic_bateria_mv(), (uint8_t)pmic_bateria_pct());
            }
            refrescar_energia_ui();
        }
```

En `firmware/main/CMakeLists.txt` añadir `reloj_energia` a `REQUIRES`.

- [ ] **Step 3: Compilar, flashear, verificar**

Run: `. ~/esp/esp-idf/export.sh && cd ~/reloj_pulsera/firmware && idf.py build && ../herramientas/flash_win.sh && ../herramientas/monitor_win.sh 20 COM3 --reset`
Expected: arranca sin errores. En el reloj: Configuración → Acerca de muestra `Descarga -- (30 min) · -- (2 h)`;
tras 16 min sin USB muestra un número en la primera cifra.

- [ ] **Step 4: Commit**

```bash
cd ~/reloj_pulsera && git add firmware/main firmware/components/reloj_ui
git commit -m "Acerca de: descarga de la batería en %/h (30 min y 2 h) a partir de una muestra por minuto"
```

### Task 3: Medida base (la hace Felipe)

**Files:**
- Create: `docs/energia.md`

- [ ] **Step 1: Crear la tabla de medidas**

```markdown
# Medidas de energía

Protocolo: cargar a tope → desconectar USB → pantalla apagada, sin tocar, 2 h → leer "Acerca de" (descarga a 2 h).
1 %/h ≈ 4 mA (batería de 400 mAh). Vigilia desactivada salvo que se diga lo contrario.

| Fecha | Etapa | Estado del firmware | %/h (2 h) | Notas |
| --- | --- | --- | --- | --- |
| (fecha) | 0 (base) | commit de la tarea 2 | | vigilia activada (así estaba) y desactivada: dos filas |
```

- [ ] **Step 2: Medir** (2 h con vigilia activada como está hoy, y 2 h con la vigilia desactivada en Configuración) y
  rellenar las filas. Si la descarga a 2 h aún dice `--` (menos de 61 muestras), esperar.

- [ ] **Step 3: Commit**

```bash
cd ~/reloj_pulsera && git add docs/energia.md && git commit -m "docs: medida base de descarga en reposo"
```

---

## Etapa 1 — Apagar lo que sobra

### Task 4: IMU solo en la app Sensores

**Files:**
- Modify: `firmware/components/reloj_hw/include/imu.h`, `firmware/components/reloj_hw/imu.c`
- Modify: `firmware/main/main.c` (bucle principal, bloque "App Sensores")

**Interfaces:**
- Produces: `void imu_activar(bool activo);` (enciende/apaga acelerómetro y giroscopio; idempotente).

- [ ] **Step 1: Implementar**

`imu.h`: añadir `void imu_activar(bool activo);   // sensores encendidos solo mientras alguien los lee`.

`imu.c`: en `imu_init`, sustituir las dos líneas `qmi8658_enable_accel(&dev, true); qmi8658_enable_gyro(&dev, true);`
por `qmi8658_enable_accel(&dev, false); qmi8658_enable_gyro(&dev, false);` y cambiar el log a
`"QMI8658 listo (sensores apagados hasta que se usen)"`. Añadir:

```c
static bool activo_actual;

void imu_activar(bool activo)
{
    if (!listo || activo == activo_actual) {
        return;
    }
    qmi8658_enable_accel(&dev, activo);
    qmi8658_enable_gyro(&dev, activo);
    activo_actual = activo;
    ESP_LOGI(TAG, "sensores %s", activo ? "encendidos" : "apagados");
}
```

`main.c`, bloque "App Sensores" del bucle (línea ~509): antes del `if (ui_en_sensores())` añadir

```c
        static bool imu_activo;
        if (ui_en_sensores() != imu_activo) {
            imu_activo = ui_en_sensores();
            imu_activar(imu_activo);
        }
```

- [ ] **Step 2: Compilar, flashear, verificar**

Run: build + flash + monitor de 40 s (ver Global Constraints).
Expected: al arrancar `QMI8658 listo (sensores apagados...)`; al abrir Sensores `sensores encendidos` y los valores se
mueven al inclinar el reloj; al salir `sensores apagados`.

- [ ] **Step 3: Commit**

```bash
cd ~/reloj_pulsera && git add firmware/components/reloj_hw firmware/main/main.c
git commit -m "IMU: acelerómetro y giroscopio encendidos solo dentro de la app Sensores"
```

### Task 5: Códec de audio por demanda

**Files:**
- Modify: `firmware/components/reloj_voz/voz.c`

**Interfaces:**
- Sin cambios de API. Comportamiento: el códec se abre al primer uso y se cierra tras ~2 s sin captura ni
  reproducción (cerrar deshabilita los canales I²S y apaga el amplificador por GPIO6).

- [ ] **Step 1: Implementar**

En `voz.c`:

1. Estáticos nuevos (junto a `tramas_a_descartar`):
   ```c
   static bool abierto;                        // códec abierto (canales I²S habilitados, amplificador encendido)
   static int volumen_pct = 60;                // se aplican en cada apertura (el códec cerrado no acepta ajustes)
   static float ganancia_db = 36.0f;
   static int ciclos_ocioso;                   // ciclos de 100 ms sin captura ni reproducción
   #define CICLOS_PARA_CERRAR 20               // ~2 s
   ```
2. `voz_init`: quitar las dos líneas `esp_codec_dev_open(...)` con sus `ESP_RETURN_ON_FALSE`, y las dos de
   `set_in_gain` / `set_out_vol`. Log final: `"códec listo (16 kHz mono; se abre al usarse)"`.
3. Funciones nuevas antes de `tarea_voz`:
   ```c
   static esp_err_t abrir(void)
   {
       if (abierto) {
           return ESP_OK;
       }
       esp_codec_dev_sample_info_t fs = { .bits_per_sample = 16, .channel = 1, .sample_rate = VOZ_TASA };
       ESP_RETURN_ON_FALSE(esp_codec_dev_open(alt, &fs) == 0, ESP_FAIL, TAG, "abrir altavoz");
       ESP_RETURN_ON_FALSE(esp_codec_dev_open(mic, &fs) == 0, ESP_FAIL, TAG, "abrir micrófono");
       esp_codec_dev_set_in_gain(mic, ganancia_db);
       esp_codec_dev_set_out_vol(alt, volumen_pct);
       tramas_a_descartar = TRAMAS_TRAS_REPRODUCIR;   // el DMA recién arrancado trae basura
       abierto = true;
       ciclos_ocioso = 0;
       ESP_LOGI(TAG, "códec abierto");
       return ESP_OK;
   }

   static void cerrar(void)
   {
       if (!abierto) {
           return;
       }
       esp_codec_dev_close(mic);   // deshabilita los canales I²S (suelta el candado de PM del driver)
       esp_codec_dev_close(alt);   // y el ES8311 baja el GPIO6 del amplificador
       abierto = false;
       ESP_LOGI(TAG, "códec cerrado");
   }
   ```
4. En `tarea_voz`, al principio del `for (;;)`:
   ```c
           if ((reproduciendo || capturando) && !abierto && abrir() != ESP_OK) {
               vTaskDelay(pdMS_TO_TICKS(100));
               continue;
           }
   ```
   y en la rama final (`else { ulTaskNotifyTake(...); }`) sustituir por:
   ```c
           } else if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100)) == 0) {   // sin trabajo: contar hasta cerrar
               if (abierto && ++ciclos_ocioso >= CICLOS_PARA_CERRAR) {
                   cerrar();
               }
           } else {
               ciclos_ocioso = 0;
           }
   ```
5. `voz_ganancia_mic` y `voz_volumen`: guardar en `ganancia_db` / `volumen_pct` y aplicar solo `if (abierto)`.

- [ ] **Step 2: Compilar, flashear, verificar**

Expected en consola: al arrancar `códec listo (... se abre al usarse)` y ningún `códec abierto` hasta usarlo (con la
vigilia activada se abrirá enseguida: es correcto). Configuración → Sonido → Probar: `códec abierto`, suena el pitido,
unos 2 s después `códec cerrado`. Una conversación con ANDIA por botón funciona entera (captura y voz de vuelta) y al
terminar se cierra. Con la vigilia activada el códec queda abierto y no aparece `códec cerrado`.

- [ ] **Step 3: Commit**

```bash
cd ~/reloj_pulsera && git add firmware/components/reloj_voz/voz.c
git commit -m "Voz: el códec se abre al primer uso y se cierra tras 2 s sin audio (I2S y amplificador apagados en reposo)"
```

### Task 6: WiFi por demanda y ANDIA enterada de la red

**Files:**
- Modify: `firmware/components/reloj_red/include/red.h`, `firmware/components/reloj_red/red.c`
- Modify: `firmware/components/reloj_andia/include/andia.h`, `firmware/components/reloj_andia/andia.c`
- Modify: `firmware/main/main.c` (`evento_andia` caso `ANDIA_EV_RED`; `app_main` tras `red_init`)

**Interfaces:**
- Produces: `esp_err_t red_encender(void); esp_err_t red_apagar(void); bool red_encendida(void);` (`red_init` ya no
  arranca el WiFi). `void andia_red(bool conectada);`.

- [ ] **Step 1: `red.c`**

Estático nuevo: `static bool deseada;   // el WiFi debe estar encendido (lo decide el gestor de energía)`.

`red_init`: sustituir la última línea `return esp_wifi_start();` por `return ESP_OK;` y el comentario de `red.h`
`esp_err_t red_init(...)` por `// prepara el WiFi (no lo arranca): publica andia_evento_t {ANDIA_EV_RED, red_conectada}`.

Funciones nuevas al final:

```c
esp_err_t red_encender(void)
{
    if (deseada) {
        return ESP_OK;
    }
    deseada = true;
    fallos = 0;
    ESP_LOGI(TAG, "WiFi encendido");
    return esp_wifi_start();   // STA_START → conectar_a()
}

esp_err_t red_apagar(void)
{
    if (!deseada) {
        return ESP_OK;
    }
    deseada = false;
    ESP_LOGI(TAG, "WiFi apagado");
    esp_err_t r = esp_wifi_stop();
    if (conectada) {           // por si el evento de desconexión no llega (ya estaba caído)
        strcpy(ip, "--");
        publicar(false);
    }
    return r;
}

bool red_encendida(void) { return deseada; }
```

En `al_evento`, caso `WIFI_EVENT_STA_DISCONNECTED`: justo después del bloque `if (conectada) {...}` añadir
`if (!deseada) { return; }   // apagado nuestro: sin reintentos`.

`red.h`: declarar las tres funciones con comentarios (`// arranca el WiFi y se conecta a la primera red conocida`,
`// para el WiFi; publica ANDIA_EV_RED=false si estaba conectado`, `// true entre encender y apagar`).

- [ ] **Step 2: `andia_red`**

`andia.h`: `void andia_red(bool conectada);   // el WiFi cayó o volvió (lo llama main al recibir ANDIA_EV_RED)`.

`andia.c`, en la sección API:

```c
void andia_red(bool conectada)
{
    if (!conectada) {
        if (ws) {
            esp_websocket_client_stop(ws);   // sin red no hay que reintentar cada 5 s
            saludado = false;
        }
        return;
    }
    if (vigilia) {
        intentar_conectar();
    }
}
```

`intentar_conectar` se define en la tarea 7; para que esta tarea compile sola, definir ahora esta versión **justo antes
de `andia_init`** (después de `asegurar_cliente`; la tarea 7 la reemplaza en el mismo sitio):

```c
static void intentar_conectar(void)
{
    if (!red_conectada() || asegurar_cliente() != ESP_OK) {
        return;
    }
    if (!esp_websocket_client_is_connected(ws)) {
        esp_websocket_client_start(ws);
    }
}
```

y en `andia_vigilia(bool activa)` sustituir el `if (red_conectada() && asegurar_cliente() == ESP_OK && ...) { esp_websocket_client_start(ws); }`
por `intentar_conectar();`.

- [ ] **Step 3: `main.c`**

En `evento_andia`, caso `ANDIA_EV_RED`, primera línea: `andia_red(ev->red_conectada);`.
En `app_main`, justo después del bloque `if (red_init(cola_andia) != ESP_OK) {...}`: `red_encender();   // provisional: el gestor de energía (tarea 10) decidirá`.

- [ ] **Step 4: Compilar, flashear, verificar**

Expected: `WiFi encendido` en el arranque y conexión como antes (`conectado a "...", IP ...`); ANDIA responde por
botón; con la vigilia activada, `andia: vigilia activada` y el WebSocket abre solo.

- [ ] **Step 5: Commit**

```bash
cd ~/reloj_pulsera && git add firmware/components/reloj_red firmware/components/reloj_andia firmware/main/main.c
git commit -m "Red: WiFi por demanda (encender/apagar) y ANDIA para su cliente cuando cae la red"
```

### Task 7: ANDIA conecta sin bloquear ("Conectando…" visible, plazo de 15 s)

**Files:**
- Modify: `firmware/components/reloj_andia/andia.c`

**Interfaces:**
- Sin cambios de API. `andia_conversar` vuelve de inmediato; la conversación arranca al llegar el "hola" del servidor.

- [ ] **Step 1: Implementar**

1. Estáticos nuevos (junto a `timer_sigue`):
   ```c
   static volatile bool conversacion_pendiente;   // pedida por botón/gesto, esperando red + saludo
   static bool gesto_pendiente;
   static esp_timer_handle_t timer_conexion;      // plazo para conseguir el saludo
   ```
2. En `andia_init`, junto a los otros `esp_timer_create`:
   ```c
   const esp_timer_create_args_t a4 = { .callback = al_vencer_conexion, .name = "andia_conex" };
   esp_timer_create(&a4, &timer_conexion);
   ```
   (declarar `static void al_vencer_conexion(void *arg);` arriba, junto a `al_capturar`).
3. Borrar `asegurar_conexion` entera y reemplazar `intentar_conectar` (tarea 6) por este bloque, en el mismo sitio
   (antes de `andia_init`, así `andia_conversar`, `andia_red` y `andia_vigilia` lo ven definido):
   ```c
   static void fallar_conexion(const char *motivo)
   {
       conversacion_pendiente = false;
       esp_timer_stop(timer_conexion);
       poner_estado(ANDIA_INACTIVA);
       red_ahorro(true);
       publicar_texto(ANDIA_EV_ERROR, motivo, NULL);
   }

   static void intentar_conectar(void)
   {
       if (!red_conectada()) {
           return;                                  // andia_red(true) volverá a llamar cuando llegue la red
       }
       if (asegurar_cliente() != ESP_OK) {
           if (conversacion_pendiente) {
               fallar_conexion("Sin cliente ANDIA");
           }
           return;
       }
       if (!esp_websocket_client_is_connected(ws) && esp_websocket_client_start(ws) == ESP_ERR_INVALID_STATE) {
           esp_websocket_client_stop(ws);           // corría pero caído: reintento
           esp_websocket_client_start(ws);
       }
   }

   static void al_vencer_conexion(void *arg)
   {
       if (conversacion_pendiente) {
           fallar_conexion(red_conectada() ? "Sin conexión con ANDIA" : "Sin WiFi");
       }
   }

   // Lo que hoy hace la cola de andia_conversar: "inicio", captura, envío.
   static void empezar_conversacion(void)
   {
       char inicio[80];
       snprintf(inicio, sizeof(inicio), "{\"t\":\"inicio\",\"modo\":\"%s\",\"bajada\":\"%s\"}",
                gesto_pendiente ? "gesto" : "boton", bajada_para_ahora());
       enviar_json(inicio);
       memset(&envio, 0, sizeof(envio));
       en_gesto = gesto_pendiente;
       poner_estado(ANDIA_ESCUCHANDO);
       armar_vigilante();
       if (voz_capturar_iniciar(al_capturar) != ESP_OK) {
           enviar_json("{\"t\":\"cancelar\"}");
           terminar_conversacion();
           publicar_texto(ANDIA_EV_ERROR, "Micrófono no disponible", NULL);
           return;
       }
       enviando = true;                        // en vigilia la cola ya trae audio previo: no se pierde el arranque
       if (tarea_envio) {
           xTaskNotifyGive(tarea_envio);
       }
   }
   ```
   `andia_red(true)`: cambiar `if (vigilia)` por `if (vigilia || conversacion_pendiente)`.
4. `andia_conversar` completa:
   ```c
   esp_err_t andia_conversar(bool gesto)
   {
       if (estado != ANDIA_INACTIVA && estado != ANDIA_HABLANDO) {
           return ESP_ERR_INVALID_STATE;
       }
       voz_reproducir_parar();
       red_ahorro(false);
       gesto_pendiente = gesto;
       if (saludado && ws && esp_websocket_client_is_connected(ws)) {
           empezar_conversacion();
           return ESP_OK;
       }
       // Sin saludo: el WiFi puede estar arrancando (desde reposo). Se espera al "hola" con plazo; no se bloquea.
       conversacion_pendiente = true;
       poner_estado(ANDIA_CONECTANDO);
       esp_timer_stop(timer_conexion);
       esp_timer_start_once(timer_conexion, 15 * 1000000ULL);   // 3-4 s de WiFi + hasta 12 s de TLS por el hotspot
       intentar_conectar();
       return ESP_OK;
   }
   ```
5. En `procesar_mensaje`, rama `"hola"`, después de `saludado = true;` y su log:
   ```c
           if (conversacion_pendiente) {
               conversacion_pendiente = false;
               esp_timer_stop(timer_conexion);
               empezar_conversacion();
           }
   ```
6. `andia_cancelar`, al principio:
   ```c
       if (conversacion_pendiente) {           // aún no empezó nada en el servidor
           fallar_conexion("Cancelado");
           return;
       }
   ```
   (con `fallar_conexion` publicando `ANDIA_EV_ERROR` "Cancelado": la UI lo muestra en naranja; aceptable).

- [ ] **Step 2: Compilar, flashear, verificar**

Expected: (a) con WiFi conectado y saludo hecho, el botón de micrófono pasa a `Escuchando…` al instante, como hoy;
(b) reiniciar el reloj y pulsar el micrófono en los primeros 2 s (WiFi aún asociando): la pantalla muestra
`Conectando…` sin congelarse, y al conectar pasa a `Escuchando…` y la conversación funciona; (c) con el router apagado
o el SSID mal, tras 15 s aparece `Sin WiFi`; (d) Cancelar durante `Conectando…` vuelve a `Lista`.

- [ ] **Step 3: Commit**

```bash
cd ~/reloj_pulsera && git add firmware/components/reloj_andia/andia.c
git commit -m "ANDIA: la conversación arranca al llegar el saludo del servidor, sin bloquear la tarea que la pide; plazo de 15 s"
```

### Task 8: Panel en sleep, LVGL parado y despertar por toque

**Files:**
- Modify: `firmware/components/reloj_hw/include/pantalla.h`, `firmware/components/reloj_hw/pantalla.c`
- Modify: `firmware/main/main.c` (`encender_pantalla`, `apagar_pantalla`, `refrescar_bateria`, bucle principal)

**Interfaces:**
- Produces: `void pantalla_encender(void)` (reanuda LVGL + SLPOUT; la pantalla aún no se ve),
  `void pantalla_mostrar(void)` (espera lo que falte de 120 ms, DISPON, brillo), `bool pantalla_toque_pendiente(void)`
  (true una vez por cada toque, también con la pantalla apagada). `pantalla_apagar` deja el panel en sleep y LVGL parado.
- Consumes en `main.c`: `pantalla_disp()`, `lv_refr_now`, `lv_obj_invalidate`.

- [ ] **Step 1: `pantalla.c`**

Includes nuevos: `#include "esp_timer.h"`, `#include "freertos/FreeRTOS.h"`, `#include "freertos/task.h"`.
Estáticos: `static volatile bool toque_pendiente; static int64_t t_slpout_us;`.

```c
// Corre en la tarea LVGL. Con la pantalla apagada y LVGL parado sigue llegando: la INT del táctil (GPIO15) despierta
// la tarea del port, que lee el indev igualmente.
static void al_presionar(lv_event_t *e)
{
    (void)e;
    toque_pendiente = true;
}

// Comando del panel sin parámetros (mismo formato QSPI que el brillo).
static void comando(uint8_t c)
{
    esp_lcd_panel_io_tx_param(io, (0x02u << 24) | ((uint32_t)c << 8), NULL, 0);
}
```

En `pantalla_init`, después de `indev = lvgl_port_add_touch(&tcfg);` y su comprobación:
`lv_indev_add_event_cb(indev, al_presionar, LV_EVENT_PRESSED, NULL);`.

Reemplazar `pantalla_apagar` y `pantalla_encender`, y añadir las dos nuevas:

```c
void pantalla_apagar(void)
{
    if (!encendida) {
        return;
    }
    brillo_hw(0);
    esp_lcd_panel_disp_on_off(panel, false);
    comando(0x10);            // SLPIN: el controlador del panel a dormir
    lvgl_port_stop();         // sin tick ni temporizadores ni redibujos; el toque sigue llegando por la INT
    encendida = false;
    ESP_LOGI(TAG, "apagada (panel en sleep, LVGL parado)");
}

void pantalla_encender(void)
{
    if (encendida) {
        return;
    }
    lvgl_port_resume();
    comando(0x11);            // SLPOUT: el panel pide ~120 ms antes de DISPON; mientras, se repinta la UI
    t_slpout_us = esp_timer_get_time();
    encendida = true;
}

void pantalla_mostrar(void)
{
    int64_t falta_us = 120000 - (esp_timer_get_time() - t_slpout_us);
    if (falta_us > 0) {
        vTaskDelay(pdMS_TO_TICKS(falta_us / 1000 + 1));
    }
    esp_lcd_panel_disp_on_off(panel, true);
    brillo_hw(brillo_pct);
    ESP_LOGI(TAG, "encendida en %d ms", (int)((esp_timer_get_time() - t_slpout_us) / 1000));
}

bool pantalla_toque_pendiente(void)
{
    bool t = toque_pendiente;
    toque_pendiente = false;
    return t;
}
```

`pantalla.h`: actualizar comentarios de `pantalla_apagar` (`// brillo 0, DISPOFF, SLPIN y LVGL parado; el toque sigue
detectándose`) y `pantalla_encender` (`// reanuda LVGL y saca al panel del sleep; llamar a pantalla_mostrar() tras
repintar`), declarar `void pantalla_mostrar(void);` y `bool pantalla_toque_pendiente(void);   // true una vez por toque (también apagada)`.

- [ ] **Step 2: `main.c`**

1. Tipo y estáticos (junto a `t_encendido_ms`):
   ```c
   typedef enum { MOTIVO_USUARIO, MOTIVO_AVISO } motivo_t;   // por qué se encendió la pantalla
   static bool pantalla_por_usuario = true;                  // arranque = usuario
   ```
   y cambiar la declaración adelantada a `static void encender_pantalla(motivo_t motivo);`.
2. `encender_pantalla`:
   ```c
   static void encender_pantalla(motivo_t motivo)
   {
       t_encendido_ms = ahora_ms();                  // cada evento cuenta como actividad (como antes)
       if (motivo == MOTIVO_USUARIO) {
           pantalla_por_usuario = true;
       }
       if (pantalla_encendida()) {
           return;
       }
       pantalla_por_usuario = (motivo == MOTIVO_USUARIO);
       pantalla_encender();
       // Repintar con la hora al día antes de que el panel se vea.
       time_t ahora = time(NULL);
       struct tm t;
       localtime_r(&ahora, &t);
       pantalla_lock(0);
       ui_hora(&t);
       lv_obj_invalidate(lv_screen_active());
       lv_refr_now(pantalla_disp());
       pantalla_unlock();
       pantalla_mostrar();
   }
   ```
   Cambiar todas las llamadas: `encender_pantalla(MOTIVO_AVISO)` en `evento_notif` (llamada y aviso), `aviso_local`,
   y en `evento_andia` (`ANDIA_EV_ESTADO`, `ANDIA_EV_SIGUE`, `ANDIA_EV_RESPUESTA`, `ANDIA_EV_ERROR`);
   `encender_pantalla(MOTIVO_USUARIO)` en `cb_microfono` y en el PWR corto del bucle.
3. `refrescar_bateria`: envolver `pantalla_lock(0); ui_bateria(...); ui_ram(...); pantalla_unlock();` en
   `if (pantalla_encendida()) { ... }` (el log y `red_permitir_ahorro` siguen siempre). Igual en `refrescar_energia_ui`.
4. Bucle principal: envolver el bloque `if (ahora_ms() - ultimo_wifi_ui > 5000) {...}` y el bloque `if (ahora != ultimo_seg) {...}`
   en `if (pantalla_encendida())`. Reemplazar el bloque final "Pantalla: se apaga sola..." por:
   ```c
           // Toque: enciende la pantalla apagada; en una pantalla encendida por un aviso, pasa a ser "por el usuario".
           if (pantalla_toque_pendiente()) {
               if (!pantalla_encendida()) {
                   encender_pantalla(MOTIVO_USUARIO);
               } else {
                   pantalla_por_usuario = true;
               }
           }
           // Apagado por inactividad (con LVGL en marcha su cuenta de inactividad es fiable).
           int64_t limite = (int64_t)aj.tiempo_s * 1000;
           if (pantalla_encendida() && (int64_t)pantalla_ms_sin_tocar() > limite && ahora_ms() - t_encendido_ms > limite) {
               apagar_pantalla();
           }
   ```
   y borrar `t_apagado_ms` (ya no se usa) de los estáticos y de `apagar_pantalla`.

- [ ] **Step 3: Compilar, flashear, verificar**

Expected: a los 20 s sin tocar, `pantalla: apagada (panel en sleep, LVGL parado)`; el panel queda negro. Un toque:
`pantalla: encendida en N ms` (N ≈ 120-250), aparece la carátula con la hora correcta y sin restos, el toque no activó
ningún botón. PWR corto enciende y apaga. Con la pantalla apagada, mandar una notificación desde el iPhone la enciende
con la tarjeta; tocarla abre el detalle. Deslizar, abrir apps y Snake siguen fluidos. Repetir apagar/encender 10 veces.
Si el panel enciende con basura o no enciende: ver riesgo "panel tras SLPIN" del spec (reset por GPIO11 al despertar).

- [ ] **Step 4: Commit**

```bash
cd ~/reloj_pulsera && git add firmware/components/reloj_hw firmware/main/main.c
git commit -m "Pantalla: panel en sleep (SLPIN) y LVGL parado al apagarse; despierta por el toque (INT del táctil) con la hora ya repintada"
```

### Task 9: Regla de energía (C puro) con test en host

**Files:**
- Create: `firmware/components/reloj_energia/include/energia_politica.h`
- Create: `firmware/components/reloj_energia/energia_politica.c`
- Create: `test_host/test_energia.c`
- Modify: `firmware/components/reloj_energia/CMakeLists.txt`, `test_host/Makefile`

**Interfaces:**
- Produces: `energia_entradas_t {usb, vigilia, pantalla, pantalla_por_usuario, conversacion}`,
  `energia_salidas_t {wifi, cpu_rapida, dormir, lvgl}`, `energia_modo_t {ENERGIA_USB, ENERGIA_CONVERSACION,
  ENERGIA_VIGILIA, ENERGIA_ACTIVO, ENERGIA_REPOSO}`, `energia_modo_t energia_decidir(const energia_entradas_t *e, energia_salidas_t *s)`,
  `const char *energia_modo_nombre(energia_modo_t m)`.

- [ ] **Step 1: Cabecera**

```c
// firmware/components/reloj_energia/include/energia_politica.h
#pragma once
// Regla de energía del reloj: qué debe estar encendido según el estado. C puro, sin estado: se testea en host.
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    bool usb;                  // USB conectado (PMIC)
    bool vigilia;              // ajuste "Oye Andia siempre"
    bool pantalla;             // pantalla encendida
    bool pantalla_por_usuario; // el último encendido fue por toque o PWR (no por un aviso)
    bool conversacion;         // ANDIA en un estado distinto de INACTIVA
} energia_entradas_t;
typedef struct {
    bool wifi;        // WiFi encendido
    bool cpu_rapida;  // candado de frecuencia máxima
    bool dormir;      // light sleep permitido (reposo)
    bool lvgl;        // LVGL en marcha
} energia_salidas_t;
typedef enum { ENERGIA_USB, ENERGIA_CONVERSACION, ENERGIA_VIGILIA, ENERGIA_ACTIVO, ENERGIA_REPOSO } energia_modo_t;
energia_modo_t energia_decidir(const energia_entradas_t *e, energia_salidas_t *s);
const char *energia_modo_nombre(energia_modo_t m);   // "USB", "conversación", "vigilia", "activo", "reposo"
#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: Test (falla)**

```c
// test_host/test_energia.c
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
    //                                     usb vig pant usr conv    wifi cpu  dormir modo
    caso("reposo",              (energia_entradas_t){ 0, 0, 0, 0, 0 }, 0, 0, 1, ENERGIA_REPOSO);
    caso("pantalla por usuario",(energia_entradas_t){ 0, 0, 1, 1, 0 }, 1, 1, 0, ENERGIA_ACTIVO);
    caso("pantalla por aviso",  (energia_entradas_t){ 0, 0, 1, 0, 0 }, 0, 1, 0, ENERGIA_ACTIVO);
    caso("usuario sin pantalla",(energia_entradas_t){ 0, 0, 0, 1, 0 }, 0, 0, 1, ENERGIA_REPOSO);
    caso("conversación a oscuras",(energia_entradas_t){ 0, 0, 0, 0, 1 }, 1, 1, 0, ENERGIA_CONVERSACION);
    caso("vigilia en reposo",   (energia_entradas_t){ 0, 1, 0, 0, 0 }, 1, 0, 0, ENERGIA_VIGILIA);
    caso("vigilia con pantalla",(energia_entradas_t){ 0, 1, 1, 1, 0 }, 1, 1, 0, ENERGIA_VIGILIA);
    caso("usb en reposo",       (energia_entradas_t){ 1, 0, 0, 0, 0 }, 1, 1, 0, ENERGIA_USB);
    caso("usb y conversación",  (energia_entradas_t){ 1, 0, 1, 1, 1 }, 1, 1, 0, ENERGIA_USB);
    caso("vigilia y conversación",(energia_entradas_t){ 0, 1, 0, 0, 1 }, 1, 1, 0, ENERGIA_CONVERSACION);
    assert(strcmp(energia_modo_nombre(ENERGIA_REPOSO), "reposo") == 0);
    puts("test_energia: OK");
    return 0;
}
```

Makefile: `test_energia` en `test:` (y `./test_energia`), en `clean`, y la regla
`test_energia: test_energia.c $(ENERGIA)/energia_politica.c` con `$(CC) $(CFLAGS) -o $@ $^`.

- [ ] **Step 3: Correr y ver que falla**

Run: `cd ~/reloj_pulsera/test_host && make test_energia`
Expected: error, no existe `energia_politica.c`.

- [ ] **Step 4: Implementar**

```c
// firmware/components/reloj_energia/energia_politica.c
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
```

CMakeLists: `SRCS "bateria_registro.c" "energia_politica.c"`.

- [ ] **Step 5: Correr los tests**

Run: `cd ~/reloj_pulsera/test_host && make test`
Expected: todas las líneas `OK`, incluida `test_energia: OK`.

- [ ] **Step 6: Commit**

```bash
cd ~/reloj_pulsera && git add firmware/components/reloj_energia test_host
git commit -m "Energía: regla pura (USB, vigilia, pantalla y su motivo, conversación → WiFi, CPU, dormir, LVGL) con test en host"
```

### Task 10: Gestor de energía en el reloj y vigilia desactivada por defecto

**Files:**
- Create: `firmware/components/reloj_energia/include/energia.h`, `firmware/components/reloj_energia/energia.c`
- Modify: `firmware/components/reloj_energia/CMakeLists.txt`
- Modify: `firmware/main/main.c`

**Interfaces:**
- Produces: `esp_err_t energia_init(void)`, `void energia_actualizar(const energia_entradas_t *e)`,
  `energia_modo_t energia_modo(void)`, `uint32_t energia_segundos_en_modo(void)`.
- Consumes: `energia_decidir` (tarea 9), `red_encender/red_apagar` (tarea 6).

- [ ] **Step 1: `energia.h` y `energia.c`**

```c
// firmware/components/reloj_energia/include/energia.h
#pragma once
// Gestor de energía: aplica la regla de energia_politica (WiFi por demanda, candados de PM en la etapa 2) y lleva
// el modo actual para el log y "Acerca de".
#include <stdint.h>
#include "esp_err.h"
#include "energia_politica.h"
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t energia_init(void);                              // con CONFIG_PM_ENABLE: esp_pm_configure + candados
void energia_actualizar(const energia_entradas_t *e);      // aplica solo lo que cambió; registra los cambios de modo
energia_modo_t energia_modo(void);
uint32_t energia_segundos_en_modo(void);
#ifdef __cplusplus
}
#endif
```

```c
// firmware/components/reloj_energia/energia.c
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
```

CMakeLists: `SRCS "bateria_registro.c" "energia_politica.c" "energia.c"`, `REQUIRES esp_timer reloj_red`, `PRIV_REQUIRES esp_pm`.

- [ ] **Step 2: `main.c`**

1. `#include "energia.h"`. Vigilia por defecto desactivada: `aj = { 60, 60, 20, 100, false }`; comentario encima:
   `// "Oye Andia" solo si el usuario lo activa (impide el reposo).`
2. Función (después de `apagar_pantalla`):
   ```c
   // Le cuenta al gestor de energía el estado actual; barato, se llama en cada vuelta del bucle y tras cada cambio.
   static void energia_refrescar(void)
   {
       energia_entradas_t e = {
           .usb = pmic_usb_conectado(),
           .vigilia = aj.vigilia,
           .pantalla = pantalla_encendida(),
           .pantalla_por_usuario = pantalla_por_usuario,
           .conversacion = andia_estado() != ANDIA_INACTIVA,
       };
       energia_actualizar(&e);
   }
   ```
   (declarar `static void energia_refrescar(void);` arriba, junto a `encender_pantalla`).
3. Llamarla al final de `encender_pantalla` (tras `pantalla_mostrar()`), al final de `apagar_pantalla`, en `cb_vigilia`
   (tras `andia_vigilia`), y al final del cuerpo del `while (true)` del bucle principal.
4. `app_main`: tras `red_init` quitar el `red_encender()` provisional de la tarea 6 y poner
   `ESP_ERROR_CHECK(energia_init()); energia_refrescar();   // arranque = pantalla por el usuario: WiFi encendido`.
5. `refrescar_energia_ui`: el texto pasa a dos líneas:
   ```c
       snprintf(t, sizeof(t), "Modo %s desde hace %lu min\nDescarga %s (30 min) · %s (2 h)",
                energia_modo_nombre(energia_modo()), (unsigned long)(energia_segundos_en_modo() / 60), a, c);
   ```
   (el búfer `t` y `aj.energia` de la UI ya tienen 96 bytes).
6. Bucle: en el bloque de PMIC (`usb_puesto` / `usb_quitado`), nada más: el refresco de fin de vuelta lo recoge.

- [ ] **Step 3: Compilar, flashear, verificar la lista del spec (§8, puntos 1-5)**

Con USB: consola `energia: modo USB → ...` no aparece (arranca en USB); Acerca de dice `Modo USB`.
Sin USB (consola no disponible: verificar en pantalla y por comportamiento):
1. Desactivar la vigilia en Configuración → ANDIA. Esperar el apagado y 1 min más. Tocar: enciende; abrir enseguida
   Configuración → WiFi: `Red: sin conexión` durante 2-3 s y luego conectada (prueba de que en reposo estaba apagado);
   Acerca de dice `Modo activo desde hace 0 min`.
2. PWR largo desde reposo: pantalla ANDIA con `Conectando…` 2-4 s, luego `Escuchando…`; conversación completa con voz.
3. Sonido → Probar desde una pantalla recién encendida: suena.
4. Vigilia activada: tras apagarse la pantalla, decir "Oye Andia, qué hora es" → responde (no entró en reposo).
5. Volver a conectar USB: Acerca de `Modo USB`; consola viva.
Con USB y `monitor_win.sh 120`, provocar transiciones tapando la vigilia y PWR corto: deben salir líneas
`energia: modo ... → ...` coherentes y `red: WiFi apagado` / `WiFi encendido` solo cuando toca.

- [ ] **Step 4: Commit**

```bash
cd ~/reloj_pulsera && git add firmware/components/reloj_energia firmware/main/main.c
git commit -m "Energía: gestor con regla pura (WiFi solo con USB, vigilia, conversación o pantalla por el usuario); vigilia desactivada por defecto; modo en Acerca de"
```

### Task 11: Medida de la etapa 1 (la hace Felipe)

- [ ] **Step 1:** Protocolo de `docs/energia.md`, 2 h en reposo con vigilia desactivada. Añadir la fila "1" con el
  commit de la tarea 10. Opcional: 2 h con vigilia activada, para saber cuánto cuesta.
- [ ] **Step 2:** Commit: `git add docs/energia.md && git commit -m "docs: medida tras la etapa 1"`.

---

## Etapa 2 — Dormir la CPU

### Task 12: PM de ESP-IDF, sueño del BLE y despertar por toque desde light sleep

**Files:**
- Modify: `firmware/sdkconfig.defaults` (bloque nuevo) y `firmware/sdkconfig` (local, sin borrar)
- Modify: `firmware/components/reloj_energia/energia.c`, `firmware/components/reloj_energia/CMakeLists.txt`

- [ ] **Step 1: Configuración**

Añadir al final de `sdkconfig.defaults`:

```
# ---- Energía (F4): CPU dormida entre eventos, BLE con sueño del controlador; con USB nunca duerme ----
CONFIG_PM_ENABLE=y
CONFIG_FREERTOS_USE_TICKLESS_IDLE=y
CONFIG_BT_LE_SLEEP_ENABLE=y
CONFIG_BT_LE_LP_CLK_SRC_MAIN_XTAL=y
CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION=y
CONFIG_ESP_SLEEP_POWER_DOWN_FLASH=y
CONFIG_ESP_PHY_MAC_BB_PD=y
```

Aplicarlo al `sdkconfig` local **sin borrarlo** (guarda las claves WiFi). Guardar esto como
`herramientas/aplicar_defaults.sh` y ejecutarlo dos veces (la segunda recoge las opciones que solo existen con
`CONFIG_PM_ENABLE` ya activo):

```bash
#!/usr/bin/env bash
# Copia al sdkconfig local las opciones CONFIG_X=y del bloque indicado de sdkconfig.defaults (sin borrar el sdkconfig,
# que guarda las claves WiFi). Uso: herramientas/aplicar_defaults.sh CONFIG_PM_ENABLE CONFIG_FREERTOS_USE_TICKLESS_IDLE ...
set -e
cd "$(dirname "$0")/../firmware"
for k in "$@"; do
    if grep -q "^# $k is not set" sdkconfig; then sed -i "s/^# $k is not set/$k=y/" sdkconfig
    elif ! grep -q "^$k=" sdkconfig; then echo "$k=y" >> sdkconfig
    fi
done
. ~/esp/esp-idf/export.sh > /dev/null
idf.py reconfigure > /dev/null
for k in "$@"; do grep -H "^$k=" sdkconfig || echo "FALTA $k"; done
```

Run (dos veces): `~/reloj_pulsera/herramientas/aplicar_defaults.sh CONFIG_PM_ENABLE CONFIG_FREERTOS_USE_TICKLESS_IDLE CONFIG_BT_LE_SLEEP_ENABLE CONFIG_BT_LE_LP_CLK_SRC_MAIN_XTAL CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION CONFIG_ESP_SLEEP_POWER_DOWN_FLASH CONFIG_ESP_PHY_MAC_BB_PD`
Expected: las siete líneas con `=y`, ninguna `FALTA`.

- [ ] **Step 2: Despertar por GPIO15 en `energia.c`**

Includes: `#include "driver/gpio.h"`, `#include "esp_sleep.h"`. Dentro de `#if CONFIG_PM_ENABLE`:

```c
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
    (void)us; (void)arg;
    gpio_wakeup_disable(GPIO_TACTIL_INT);
    return gpio_set_intr_type(GPIO_TACTIL_INT, GPIO_INTR_NEGEDGE);
}
```

En `energia_init`, dentro del bloque PM, tras crear los candados:

```c
    esp_pm_sleep_cbs_register_config_t cbs = { .enter_cb = al_entrar_sueno, .exit_cb = al_salir_sueno };
    ESP_RETURN_ON_ERROR(esp_pm_light_sleep_register_cbs(&cbs), TAG, "callbacks de sueño");
    ESP_RETURN_ON_ERROR(esp_sleep_enable_gpio_wakeup(), TAG, "despertar por GPIO");
```

CMakeLists: `PRIV_REQUIRES esp_pm esp_driver_gpio`.

- [ ] **Step 3: Compilar, flashear, verificar**

Con USB (consola): arranque con `energia: PM activo: 40-160 MHz con light sleep automático` y la línea de `pm:`
de ESP-IDF con las frecuencias; todo lo de la tarea 10 sigue igual; la consola no se corta nunca (USB impide dormir).
Sin USB, vigilia desactivada:
1. Esperar 1 min apagado. Tocar: enciende (puede tardar unos ms más). PWR corto: enciende. Interfaz fluida con la
   pantalla encendida (candado de CPU).
2. PWR largo desde reposo: conversación completa con voz.
3. **Prueba 6 del spec**: 1 h en reposo. Después: la hora de la carátula coincide con el iPhone; el icono BLE sigue
   conectado; una notificación nueva del iPhone enciende la pantalla con su tarjeta. Si el iPhone se desconectó o no
   llega la notificación: **la etapa 2 no se da por buena**; dejar `CONFIG_BT_LE_SLEEP_ENABLE=n` (repetir el paso 1 sin
   esa opción y con `sed -i 's/^CONFIG_BT_LE_SLEEP_ENABLE=y/# CONFIG_BT_LE_SLEEP_ENABLE is not set/' firmware/sdkconfig`)
   y repetir la prueba solo con el light sleep de la CPU; anotar el resultado en `docs/energia.md`.
4. Snake y Sensores siguen fluidos con la pantalla encendida.

- [ ] **Step 4: Commit**

```bash
cd ~/reloj_pulsera && git add firmware/sdkconfig.defaults firmware/components/reloj_energia herramientas/aplicar_defaults.sh
git commit -m "Energía (etapa 2): PM con tickless idle y light sleep automático en reposo, sueño del controlador BLE, despertar por la INT del táctil; con USB nunca duerme"
```

### Task 13: Medida de la etapa 2 y documentación

**Files:**
- Modify: `docs/energia.md`, `README.md` (sección "Uso del reloj" y "Estado"), `docs/plan.md` (§F4)

- [ ] **Step 1:** Medida de 2 h en reposo (protocolo). Fila "2" en `docs/energia.md`. Si el objetivo (≤ 4 %/h) no se
  alcanzó, anotar la cifra y la sospecha (BLE sin sueño, códec abierto, WiFi encendido): es la entrada de la
  siguiente ronda (spec §11).
- [ ] **Step 2:** `README.md`: en "Uso del reloj" añadir `- Reposo: a batería, con la pantalla apagada y sin "Oye Andia
  siempre" (Configuración → ANDIA, desactivado por defecto), el reloj apaga WiFi, audio y sensores y duerme la CPU;
  solo sigue el Bluetooth con el iPhone. ANDIA por PWR largo tarda 2-4 s en conectar desde reposo. "Acerca de" muestra
  el modo y la descarga en %/h.` y en "Estado" la fecha y la cifra medida. `docs/plan.md` §F4: marcar lo hecho y
  remitir a `docs/energia.md`.
- [ ] **Step 3:** Commit: `git add README.md docs && git commit -m "docs: reposo con PM, medidas de energía y estado de F4"`.

---

## Verificación final contra el spec

- §4 regla y modos → tareas 9 y 10. §5.1 red → 6. §5.2 pantalla → 8. §5.3 voz → 5. §5.4 IMU → 4. §5.5 bucle → 8 y 10.
  §5.6 ANDIA → 6 y 7. §6 PM → 12. §7 medición → 1, 2, 3, 11, 13. §8 pruebas → pasos de verificación de 8, 10 y 12 y
  el test en host de 9. §9 riesgos → notas en 8 (panel) y 12 (BLE).

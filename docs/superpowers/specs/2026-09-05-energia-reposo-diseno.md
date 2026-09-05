# Energía en reposo — diseño (fase F4, primera parte)

Fecha: 2026-09-05. Estado: borrador para revisión de Felipe.

## 1. Objetivo

Que el reloj gaste lo mínimo cuando está en reposo a batería: pantalla apagada, sin conversación con ANDIA y sin
la vigilia "Oye Andia" activada. En reposo solo queda vivo el BLE con el iPhone (hora y notificaciones siguen
llegando); todo lo demás se apaga y la CPU duerme entre eventos. Criterio de éxito orientativo: **un día completo
de reposo con una carga**, es decir, una descarga de 4 %/h o menos (Waveshare cita 3-4 h con la pantalla apagada
con su firmware). Cada 1 %/h son unos 4 mA con la batería de 400 mAh.

## 2. Decisiones ya tomadas

| Tema | Decisión |
| --- | --- |
| Vigilia "Oye Andia" | Pasa a venir **desactivada por defecto**. Sigue disponible como ajuste en Configuración; activada, el reloj no entra en reposo (se comporta como hoy) |
| ANDIA en reposo | Solo por botón (PWR largo). Desde reposo tarda 2-4 s en estar lista (arranque del WiFi + WebSocket); la pantalla muestra "conectando…" mientras tanto |
| WiFi en reposo | **Apagado**. Los avisos y recordatorios que la Pi empuje mientras el reloj duerme no llegan hasta que despierte (aceptado) |
| BLE en reposo | Conectado, con el sueño del controlador activado en la etapa 2 |
| Con USB conectado | Nunca entra en reposo ni duerme: consola viva y comportamiento de hoy (ya se hacía con el ahorro del WiFi) |
| Una conversación en curso | Nunca entra en reposo aunque la pantalla se apague |
| Deep sleep | Descartado: se cae la conexión BLE y se pierden notificaciones |
| Enfoque | Gestor de energía central con regla pura testeable en host, construido en tres etapas medibles |

## 3. Diagnóstico de partida (2026-09-05)

Con la pantalla apagada, el firmware actual mantiene despierto casi todo:

- CPU a 160 MHz fija, tick de 1000 Hz, sin `CONFIG_PM_ENABLE` ni tickless idle: nunca duerme.
- Vigilia siempre activa: ES7210 + DMA del I²S capturando a 16 kHz, tarea de voz cada 32 ms, WebSocket permanente
  con ping cada 20 s (cliente) y cada 30 s (propio). El driver del I²S mantiene un candado de PM mientras el canal
  esté habilitado: aunque se activara el PM, no dormiría.
- WiFi conectado siempre (modem sleep mínimo a batería).
- BLE sin sueño del controlador (`CONFIG_BT_LE_SLEEP_ENABLE` no está).
- Giroscopio y acelerómetro a 125 Hz desde el arranque; solo los lee la app Sensores.
- LVGL sigue activo: tick de 5 ms, sondeo del táctil por I²C cada 30 ms, redibujo de la hora cada segundo enviado
  por QSPI a un panel que solo recibió DISPOFF (no SLPIN).
- Ruta del altavoz abierta desde el arranque: ES8311, I²S de salida y amplificador NS4150B habilitado (GPIO6).
- Rieles del AXP2101 sin tocar (los de fábrica).
- Bucle principal cada 250 ms leyendo el PMIC por I²C (barato; el problema es que entre lecturas no duerme).

## 4. Arquitectura: componente `reloj_energia`

Dos archivos con responsabilidades separadas:

- **`energia_politica.c/.h`** — C puro, sin ESP-IDF, sin estado interno. Recibe el estado del reloj y devuelve qué
  debe estar encendido. La misma entrada produce siempre la misma salida: el test en host es una tabla.
- **`energia.c/.h`** — pegamento sobre ESP-IDF: crea los candados de PM, aplica la decisión llamando a `red`,
  `pantalla` y compañía, y escribe en el log cada cambio de modo con el motivo.

### 4.1 Entradas

Las recoge el bucle principal (`main.c`) y llama a `energia_actualizar()` cada vez que alguna cambia:

| Entrada | De dónde sale |
| --- | --- |
| `usb` | PMIC (`pmic_usb_conectado`), como hoy |
| `vigilia` | ajuste `aj.vigilia` (NVS), por defecto `false` |
| `pantalla` | `pantalla_encendida()` |
| `pantalla_por_usuario` | motivo del último encendido: toque o PWR = usuario; notificación o llamada = aviso. Si el usuario toca una pantalla encendida por un aviso, pasa a ser "por usuario" |
| `conversacion` | estado de ANDIA distinto de `ANDIA_INACTIVA` |

### 4.2 Salidas y reglas

| Salida | Regla |
| --- | --- |
| `wifi` | `usb ∨ vigilia ∨ conversacion ∨ (pantalla ∧ pantalla_por_usuario)` |
| `cpu_rapida` (candado de frecuencia máxima) | `usb ∨ pantalla ∨ conversacion` |
| `dormir` (light sleep permitido) | `¬usb ∧ ¬vigilia ∧ ¬pantalla ∧ ¬conversacion` |
| `lvgl` | `pantalla` (la aplica `pantalla.c` al encender/apagar; la salida existe para el test y el log) |

Una notificación que enciende la pantalla **no** arranca el WiFi. El audio y el IMU no entran en la regla: el
módulo de voz abre y cierra el códec por demanda, y el giroscopio se enciende solo dentro de la app Sensores.

### 4.3 Modos (solo para el log y "Acerca de")

`USB`, `CONVERSACION`, `VIGILIA`, `ACTIVO` (pantalla encendida) y `REPOSO`, en ese orden de prioridad.

### 4.4 Interfaz propuesta

```c
// energia_politica.h (C puro)
typedef struct { bool usb, vigilia, pantalla, pantalla_por_usuario, conversacion; } energia_entradas_t;
typedef struct { bool wifi, cpu_rapida, dormir, lvgl; } energia_salidas_t;
typedef enum { ENERGIA_USB, ENERGIA_CONVERSACION, ENERGIA_VIGILIA, ENERGIA_ACTIVO, ENERGIA_REPOSO } energia_modo_t;
energia_modo_t energia_decidir(const energia_entradas_t *e, energia_salidas_t *s);

// energia.h (ESP-IDF)
esp_err_t energia_init(void);                              // candados de PM; esp_pm_configure en la etapa 2
void energia_actualizar(const energia_entradas_t *e);      // aplica solo lo que cambió y registra el modo
energia_modo_t energia_modo(void);
uint32_t energia_segundos_en_modo(void);
```

## 5. Cambios por módulo

### 5.1 Red (`reloj_red/red.c`)

- `red_encender()` / `red_apagar()` sobre `esp_wifi_start()` / `esp_wifi_stop()`; `esp_wifi_init` se hace una sola
  vez en `red_init`, que ya no arranca el WiFi por sí sola (lo decide el gestor).
- Bandera `deseada`: el manejador de `WIFI_EVENT_STA_DISCONNECTED` no reintenta cuando el apagado fue nuestro.
- ANDIA ya para su cliente WebSocket al recibir `ANDIA_EV_RED` con `red_conectada = false`, así que no queda ningún
  temporizador de reconexión (5 s) despertando la CPU. Al encender, la secuencia es la de hoy: asociación, DHCP,
  evento de red, y ANDIA abre el WebSocket.

### 5.2 Pantalla (`reloj_hw/pantalla.c`)

- **Apagar**: brillo 0 → DISPOFF (0x28) → SLPIN (0x10) → `lvgl_port_stop()`. Se detienen el tick de 5 ms, el sondeo
  del táctil y todo redibujo. Los comandos van por `esp_lcd_panel_io_tx_param`, como el brillo (el driver
  `esp_lcd_sh8601` no implementa `disp_sleep`).
- **Encender**: `lvgl_port_resume()` → refrescar la hora y dejar pintar un cuadro → SLPOUT (0x11) → espera que exige
  el panel (~120 ms) → DISPON (0x29) → brillo. Así nunca se ve la hora vieja.
- **Despertar por toque**: `esp_lcd_touch_register_interrupt_callback()` sobre la INT del táctil (GPIO15, que el BSP
  ya configura como interrupción por nivel bajo). El callback notifica a la tarea principal. En la etapa 2 se añade
  `gpio_wakeup_enable(15, GPIO_INTR_LOW_LEVEL)` + `esp_sleep_enable_gpio_wakeup()` para salir del light sleep.
- Con LVGL parado, `lv_display_get_inactive_time` deja de valer: el tiempo sin tocar lo lleva el bucle principal con
  sus propias marcas (`ahora_ms()` del último toque o encendido).

### 5.3 Voz (`reloj_voz/voz.c`)

- `voz_init` crea los handles pero **no** abre el códec. La primera captura o el primer audio a reproducir abre
  micrófono y altavoz (`esp_codec_dev_open`); cuando ambos llevan ~2 s sin uso, la propia tarea de voz los cierra
  (`esp_codec_dev_close`). Cerrar desactiva los canales I²S (suelta el candado de PM del driver) y el ES8311 baja el
  GPIO6 del amplificador.
- Volumen y ganancia del micrófono se guardan en variables y se aplican en cada apertura.
- Tras cada apertura se descartan 8 tramas de captura (reutiliza `tramas_a_descartar`, hoy usado tras reproducir).
- Los pitidos (`tono()` en `main.c`) pasan por `voz_reproducir_alimentar`, así que funcionan desde reposo con ~30 ms
  de retraso por abrir el códec. En vigilia la captura nunca para, luego el códec nunca se cierra: como hoy.

### 5.4 IMU (`reloj_hw/imu.c`)

- `imu_init` deja acelerómetro y giroscopio **apagados**. `imu_activar(bool)` los enciende al entrar en la app
  Sensores y los apaga al salir (lo llama el bucle principal según `ui_en_sensores()`).

### 5.5 Bucle principal (`main/main.c`)

- Sigue despertando cada 250 ms para `pmic_sondear`: PWR no tiene línea al ESP y el flag de pulsación queda retenido
  en el AXP2101 hasta leerlo. Es la vía de PWR corto (encender pantalla) y PWR largo (ANDIA).
- Con la pantalla apagada no refresca hora, WiFi ni batería en la interfaz.
- `encender_pantalla(motivo)` guarda el motivo (usuario / aviso). Encienden la pantalla: toque (INT), PWR corto,
  notificación con aviso y llamada entrante, como hoy.
- Llama a `energia_actualizar()` tras cada evento que cambie una entrada (PMIC, pantalla, ANDIA, ajuste de vigilia).
- La vigilia arranca por defecto desactivada (`aj.vigilia = false`); el ajuste guardado en NVS manda si existe.

### 5.6 ANDIA (`reloj_andia/andia.c`)

- Al pedir una conversación por botón sin red, muestra "conectando…" y espera al evento de red + WebSocket abierto
  antes de empezar a capturar (hoy asume que la red está). Plazo: 10 s; si vence, error "sin conexión" como hoy.

## 6. Etapa 2: gestión de energía de ESP-IDF

`sdkconfig.defaults`:

```
CONFIG_PM_ENABLE=y
CONFIG_PM_DFS_INIT_AUTO=y                 # 40-160 MHz, light sleep automático con tickless idle
CONFIG_FREERTOS_USE_TICKLESS_IDLE=y
CONFIG_BT_LE_SLEEP_ENABLE=y
CONFIG_BT_LE_LP_CLK_SRC_MAIN_XTAL=y       # ya está; la placa no tiene cristal de 32 kHz cableado al ESP
CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION=y     # con USB nunca duerme: la consola sigue viva
CONFIG_ESP_SLEEP_POWER_DOWN_FLASH=y
CONFIG_ESP_PHY_MAC_BB_PD=y
```

Es la configuración de `examples/bluetooth/nimble/power_save/sdkconfig.40m.esp32c6` de ESP-IDF v5.5.5. **No** se
activa `CONFIG_PM_POWER_DOWN_PERIPHERAL_IN_LIGHT_SLEEP`: los drivers de I²C, QSPI e I²S conservan su estado.

Con el cristal de 40 MHz como reloj de bajo consumo, el cristal sigue encendido durante el sueño del BLE: ahorra
menos que con 32 kHz, pero es lo que la placa permite sin hardware nuevo.

`energia.c` sostiene dos candados: `ESP_PM_CPU_FREQ_MAX` mientras `cpu_rapida` y `ESP_PM_NO_LIGHT_SLEEP` mientras
`¬dormir`. En reposo suelta ambos y el light sleep es automático entre eventos; despiertan el chip el BLE (llegada de
notificación), el bucle cada 250 ms (botón PWR) y la INT del táctil. El RTC del ESP sigue en sueño ligero, así que
`time()` no se desvía; además está el PCF85063.

## 7. Medición (etapa 0)

- Registro en RAM de una muestra por minuto: `{uint16 mV, uint8 %}`, 240 muestras (4 h, ~1 KB).
- "Acerca de" muestra: modo actual y desde cuándo, descarga en %/h sobre los últimos 30 min y 2 h.
- Protocolo: cargar a tope → desconectar USB → 2 h en reposo → leer %/h. Se hace **antes** de la etapa 1, tras la 1
  y tras la 2. El AXP2101 no mide corriente; el % de su medidor de carga es el dato.

## 8. Pruebas

- **Host**: `test_host/test_energia.c` con la tabla completa de entradas → salidas y modo de `energia_decidir`,
  incluido en `make test`.
- **Reloj**, lista por etapa:
  1. Despertar por toque, por PWR corto, por notificación con aviso y por llamada entrante.
  2. ANDIA por PWR largo desde reposo: "conectando…", WiFi vuelve, conversación completa con voz.
  3. Pitido del botón desde reposo (códec reabierto).
  4. Vigilia activada en Configuración → el modo no baja de VIGILIA; desactivada → REPOSO.
  5. Con USB: modo USB, no duerme, la consola sigue.
  6. Tras 1 h en reposo: hora correcta, iPhone sigue conectado y una notificación nueva llega y enciende la pantalla.
  7. Medición de 2 h con el protocolo del punto 7.

## 9. Riesgos

| Riesgo | Cómo se acota |
| --- | --- |
| BLE + light sleep con el cristal de 40 MHz desincroniza la conexión con el iPhone | Prueba 6 antes de dar la etapa 2 por buena; si falla, se queda la etapa 1 y se estudia un cristal de 32 kHz |
| La INT del táctil (GPIO15) nunca se ha usado | Primer paso de la etapa 1: log que confirme que baja al tocar. Si no, el despertar por toque pasa a ser un sondeo del táctil cada 250 ms desde el bucle |
| Basura del DMA al reabrir el códec | Descarte de 8 tramas tras cada apertura (mecanismo existente) |
| Panel no pinta bien tras SLPIN | Reinicio por GPIO11 y reinicialización del panel al despertar (~200 ms más) |
| RAM | PM añade poco; registro de batería 1 KB. El TLS del túnel sigue siendo el punto ajustado, como hoy |
| Latencia de ANDIA desde reposo (2-4 s) | Aceptada; "conectando…" en pantalla |

## 10. Orden de trabajo

1. **Etapa 0**: registro de batería + "Acerca de". Medida base de 2 h.
2. **Etapa 1**, en este orden: giroscopio solo en Sensores → códec por demanda → WiFi por demanda (+ "conectando…"
   en ANDIA) → pantalla en sleep, LVGL parado e INT del táctil → gestor de energía con test en host → vigilia
   desactivada por defecto. Medida de 2 h.
3. **Etapa 2**: PM, tickless, sueño del BLE, candados. Prueba 6 y medida de 2 h.

Cada etapa se compila, se flashea, se verifica con la lista del punto 8 y se hace commit por separado.

## 11. Fuera de alcance (después)

- Pedir al iPhone un intervalo de conexión BLE más relajado en reposo (`ble_gap_update_params`).
- Apagar rieles del AXP2101 como hace XiaoZhi (solo DC1 y ALDO1/2), tras volcar el estado de los registros.
- Despertar por gesto de muñeca con el wake-on-motion del QMI8658 (INT1 en GPIO16; el componente ya lo expone).
- Enviar la estadística de batería a la Pi para guardarla.
- Poner el táctil en hibernación (necesita el reset por GPIO10 para despertar).

## 12. Referencias

- `docs/plan.md` §F4 y `docs/hardware.md` (PMIC, pines, batería).
- ESP-IDF v5.5.5: `examples/bluetooth/nimble/power_save/`, `components/esp_pm`, `esp_driver_usb_serial_jtag/Kconfig`.
- Drivers: `esp_lcd_touch` (callback de INT), `esp_codec_dev` (open/close habilita y deshabilita el I²S; ES8311 maneja
  el pin del amplificador), `esp_driver_i2s/i2s_common.c` (candado de PM por canal).

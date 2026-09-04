# Plan: firmware propio para el reloj ESP32-C6-Touch-AMOLED-2.06

> **Estado 2026-09-03:** F0, F1 y F2 completas y verificadas en hardware con el iPhone. Desvíos respecto del plan:
> iOS ya no lista periféricos genéricos en Ajustes (emparejamiento inicial vía nRF Connect); flasheo/consola desde
> Windows por atascos de usbip; gestos con la convención del teléfono; fuente de emojis Noto como respaldo.
> **F3a (servicio ANDIA en la Pi) completa el 2026-09-03 23:30**: repo `~/andia` (main), servicio `andia` en
> coramohost:8001, probado con el PC como reloj falso (voz Mayuri, activación "Oye Andia" sobre el texto, temporizador,
> recordatorio → aviso). **F3b** (cliente de audio en el reloj: `reloj_red`, `reloj_voz`, `reloj_andia`) y **F3e**
> (túnel `wss://andia.example.com/ws` por Cloudflare, TLS 1.2, audio IMA ADPCM 4:1 en ambos sentidos) implementados
> el 2026-09-04; conversación completa verificada desde el PC por el túnel; pendiente verificarla en el reloj físico.

## Contexto

Felipe compró el reloj de desarrollo Waveshare **ESP32-C6-Touch-AMOLED-2.06** y quiere un firmware
**totalmente propio** que, en este orden: (1) se vincule por Bluetooth al iPhone, (2) muestre las
notificaciones del iPhone en el reloj, (3) más adelante interactúe con su asistente virtual (todavía
sin definir). El firmware de fábrica es el lanzador *esp-brookesia* + el asistente de voz *XiaoZhi AI*;
no se reutiliza nada de él salvo el conocimiento del hardware. La capa de drivers sí se toma del
BSP oficial publicado por Waveshare en el registro de componentes de Espressif (es "soporte de placa",
no "el firmware").

Decisiones ya tomadas con Felipe (2026-09-03):

| Tema | Decisión |
| --- | --- |
| Base | App y UI 100 % propias sobre ESP-IDF v5.5.5 + BSP `waveshare/esp32_c6_touch_amoled_2_06` + LVGL 9.5 + NimBLE |
| Asistente | Sin definir; se decide después de la fase 2. La arquitectura reserva RAM y deja libres mic/altavoz/WiFi |
| Entorno | Compilar en WSL (repo en `~/reloj_pulsera`, acceso directo en `Desktop\reloj_pulsera`); reloj pasado a WSL con **usbipd-win** |
| Uso | Prototipo de escritorio: la autonomía es una fase aparte (F4); en F1 solo apagado de pantalla por inactividad |

Supuestos (Felipe puede cambiarlos): vinculación **sin app de iPhone**, desde Ajustes → Bluetooth
(protocolo ANCS de Apple, requiere *bonding*); emparejamiento **Just Works** (sin código) en F1, con opción
de código de 6 dígitos después; nombre BLE `Reloj Felipe` (configurable); textos de la UI en español.

## Hardware que importa (verificado en esquemático, BSP y puerto de XiaoZhi)

- **ESP32-C6** chip desnudo (no módulo), 160 MHz, **512 KB SRAM, sin PSRAM**, flash 16 MB (QIO). USB-C directo al
  USB-Serial/JTAG nativo (GPIO12/13) → en Windows aparece como **COM3**, VID 303A PID 1001; no hay chip puente.
- **Pantalla** AMOLED 410×502, driver CO5300 por QSPI (GPIO0 clk, 1-4 datos, 5 CS, 11 reset). El BSP la maneja con el
  driver `esp_lcd_sh8601` (comandos compatibles), `esp_lcd_panel_set_gap(panel, 0x16, 0)` (desplazamiento X de 22 px)
  y un *rounder* que alinea áreas a coordenadas pares. Brillo = registro 0x51 (0-255); no hay backlight PWM.
- **Táctil** FT3168 por I²C (0x38) con driver `esp_lcd_touch_ft5x06`; INT GPIO15, RST GPIO10.
- **I²C compartido** GPIO7 SCL / GPIO8 SDA: táctil, **AXP2101** (0x34), **QMI8658** IMU (0x6B), **PCF85063** RTC (0x51),
  ES8311 (0x18), ES7210 (0x40).
- **Botones**: BOOT = GPIO9. **PWR va al pin PWRON del AXP2101, no a un GPIO**: se lee por el estado de IRQ del PMIC
  (su línea IRQ no llega al ESP → se sondea por I²C). La wiki dice GPIO0/GPIO10/"EXIO6": está mal (texto del modelo S3).
- **Audio** (para la fase del asistente): ES8311 DAC → amplificador NS4150B (enable GPIO6) → altavoz; ES7210 ADC con 2 micrófonos;
  I²S MCLK 19, BCLK 20, DIN 21, WS 22, DOUT 23. El BSP expone `bsp_audio_codec_speaker_init()` y `bsp_audio_codec_microphone_init()`.
- **Batería** ~400 mAh: 1 h a brillo máximo, 3-4 h con pantalla apagada, ~6 h en bajo consumo (datos de Waveshare).
- El BSP **no** inicializa el AXP2101 (la pantalla funciona con los rieles por defecto). XiaoZhi deja solo DC1 = 3,3 V y
  ALDO1/2 = 3,3 V (micrófonos), apaga el resto, y configura apagado por PWR sostenido 4 s (reg 0x27 = 0x10) y
  cargador 400 mA / 4,1 V. Sirve de referencia para nuestro `reloj_hw`.
- Modo descarga: si esptool no logra resetear, mantener BOOT y encender; **queda pegado en descarga** hasta apagar y
  volver a encender (PWR ≥ 6 s y luego PWR de nuevo).

## Entorno de desarrollo (fase F0)

Estado hoy: sin ESP-IDF ni esptool en WSL ni Windows; sin usbipd-win; `Desktop\reloj_pulsera` existe vacía;
WSL Ubuntu ("Ubuntu" en `wsl -l`), Python 3.14.4, CMake/Ninja/git/gcc OK, node 22, `uv` disponible, 917 GB libres;
Felipe no está en el grupo `dialout`.

**Windows, una vez, PowerShell como administrador (lo ejecuta Felipe):**

```powershell
winget install --exact dorssel.usbipd-win
usbipd list                                  # anotar el BUSID de "USB JTAG/serial debug unit" (303a:1001)
usbipd bind --busid <BUSID>
cmd /c rmdir "C:\Users\Felipe\Desktop\reloj_pulsera"
cmd /c mklink /D "C:\Users\Felipe\Desktop\reloj_pulsera" "\\wsl.localhost\Ubuntu\home\felipe\reloj_pulsera"
```

(Si el `mklink` molesta, alternativa sin administrador: un acceso directo `.lnk` a esa ruta UNC.)

**Cada sesión (sin administrador, desde WSL):** `herramientas/usb.sh` ejecuta
`usbipd.exe attach --wsl --busid <BUSID> --auto-attach` en segundo plano; el reloj aparece como `/dev/ttyACM0`.
`--auto-attach` cubre la re-enumeración del USB-Serial/JTAG al resetear/flashear.

**WSL:**

```bash
sudo usermod -aG dialout felipe            # Felipe con "!" (sudo pide contraseña); reabrir la terminal
sudo apt install -y ccache usbutils flex bison gperf libffi-dev libssl-dev dfu-util
mkdir -p ~/esp && cd ~/esp && git clone -b v5.5.5 --recursive --shallow-submodules --depth 1 https://github.com/espressif/esp-idf.git
cd ~/esp/esp-idf && ./install.sh esp32c6
echo 'alias idf=". $HOME/esp/esp-idf/export.sh"' >> ~/.bashrc
```

Contingencia Python 3.14 (si `install.sh` falla al crear el venv): `uv python install 3.12`, crear el entorno con
`"$(uv python find 3.12)" tools/idf_tools.py install-python-env` y exportar `IDF_PYTHON_ENV_PATH` a ese venv antes
de `export.sh`. ESP-IDF v5.5.5 es la versión que Waveshare usa en su CI y la que trae el ejemplo NimBLE ANCS.

## Estructura del repositorio (`~/reloj_pulsera`, git)

```
README.md                  cómo compilar/flashear, estado, decisiones
docs/                      notas: hardware.md (pines/trampas), ancs.md (protocolo), wiki/ (texto de la wiki bajado)
referencias/waveshare/     submódulo git (shallow) del repo waveshareteam/ESP32-C6-Touch-AMOLED-2.06
referencias/xiaozhi_board/ copia del puerto de fábrica (config.h, .cc) y del ejemplo NimBLE ble_ancs de ESP-IDF
respaldo/                  (ignorado por git) volcado de fábrica de 16 MB + bin del repo + efuse_summary.txt
herramientas/              usb.sh (attach usbipd), respaldo_flash.sh, restaurar_fabrica.sh, fuentes.sh (lv_font_conv)
firmware/                  proyecto ESP-IDF
  CMakeLists.txt  sdkconfig.defaults  partitions.csv
  main/            main.c (arranque, orden de init, tarea de app), idf_component.yml, Kconfig.projbuild (nombre BLE, tiempos)
  components/
    reloj_hw/      pmic (AXP2101 vía XPowersLib, C++ con API C), rtc_pcf85063 (driver propio), botones (PWR por PMIC, BOOT GPIO9), pantalla (encender/apagar, brillo)
    reloj_ble/     NimBLE: anuncio con solicitud ANCS, bonding, reconexión, cliente CTS (hora), cliente ANCS, cola de eventos hacia la app
    reloj_notif/   almacén de notificaciones (anillo en RAM), mapa bundle-id → nombre de app, parser de atributos ANCS (C puro, testeable en host)
    reloj_ui/      LVGL 9: carátula, lista, detalle, aviso emergente, llamada entrante, ajustes/Bluetooth/acerca de; fuentes generadas con acentos
  test_host/       tests en C de reloj_notif (parser + anillo) compilados con gcc en WSL (`make test`)
```

Dependencias (`main/idf_component.yml`): `waveshare/esp32_c6_touch_amoled_2_06 ^2.0.0`, `lvgl/lvgl 9.5.0`,
`cube32esp/xpowerslib ^0.3.3`, y en F4 `waveshare/qmi8658 ^2.0.0`. El BSP arrastra `esp_lcd_sh8601`,
`esp_lcd_touch_ft5x06`, `esp_lvgl_port ^2` y `esp_codec_dev`.

`sdkconfig.defaults` (base tomada de `examples/esp-idf/02_lvgl_demo_v9` + ejemplo `nimble/ble_ancs`):
target esp32c6, flash QIO 16 MB, partición custom, `CONFIG_FREERTOS_HZ=1000`, `CONFIG_BSP_I2C_NUM=0`,
**`CONFIG_BSP_DISPLAY_LVGL_BUF_HEIGHT=40`** (el BSP usa este Kconfig para el buffer LVGL: 410×40×2 = 33 KB en vez de los
82 KB por defecto), LVGL con malloc de libc, `CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=1` (un solo núcleo), fuentes Montserrat
solo las usadas, `CONFIG_BT_ENABLED=y`, `CONFIG_BT_NIMBLE_ENABLED=y`, `CONFIG_BT_NIMBLE_NVS_PERSIST=y`,
NimBLE solo rol periférico, 1 conexión, `CONFIG_BT_NIMBLE_EXT_ADV=n` (anuncio *legacy*, lo que iOS lista en Ajustes),
MTU preferido 256, `CONFIG_BT_NIMBLE_SM_SC=y`.

`partitions.csv`: `nvs 0x9000 0x6000` (guarda los *bonds*), `phy_init 0xf000 0x1000`, `factory app 4M`,
`datos spiffs 2M` (reservada para el asistente). OTA queda anotado como futuro.

## Arquitectura del firmware

**Tareas**: (a) tarea LVGL de `esp_lvgl_port` (la crea el BSP); (b) tarea host NimBLE (`nimble_port_freertos_init`);
(c) `reloj_app`, tarea propia que consume una cola `app_events` y es la **única** que toca LVGL (bajo
`bsp_display_lock`), sondea el PMIC cada 250 ms (IRQ de PWR, VBUS, batería cada 10 s) y lleva el temporizador de pantalla.
Los callbacks de NimBLE nunca llaman a LVGL: solo encolan eventos.

**Flujo de una notificación**: iPhone → *Notification Source* (8 bytes: evento, flags, categoría, UID) → si es
"Added", `reloj_ble` escribe en *Control Point* `GetNotificationAttributes(UID; AppIdentifier, Title 64, Subtitle 48,
Message 192, Date)` → respuesta por *Data Source* en fragmentos que se reensamblan por longitudes declaradas →
`reloj_notif` parsea y guarda en un anillo de 24 entradas (~9 KB) → evento a `reloj_app` → UI: enciende pantalla,
aviso emergente, contador en la carátula. "Removed" saca del anillo; flag *PreExisting* se guarda sin aviso;
"Descartar" en el reloj = `PerformNotificationAction(UID, Negative)`; llamada entrante (categoría 1) = pantalla propia con
contestar/rechazar (Positive/Negative). Nombres de app: tabla local de bundle-ids comunes (WhatsApp, Mensajes,
Teléfono, Mail, Instagram, Calendario, …) y `GetAppAttributes(DisplayName)` para los desconocidos (caché de 16).

**BLE** (`reloj_ble`, basado en `examples/bluetooth/nimble/ble_ancs` de ESP-IDF v5.5.5 pero reescrito como componente):
anuncio conectable *legacy* con flags + nombre + **Service Solicitation 128 bits = UUID ANCS
`7905F431-B5CE-4E99-A40F-4B1E122D00D0`** (`fields.sol_uuids128`; es lo que hace que iOS lo muestre en Ajustes y
reconecte solo); `sm_bonding=1`, `sm_sc=1`, io_cap sin entrada/salida (Just Works), distribución de claves ENC **e ID**
(la IRK del iPhone es necesaria para reconocer su dirección aleatoria al reconectar); dirección propia pública.
Al conectar se pide MTU y se fuerza cifrado (`ble_gap_security_initiate`); en `ENC_CHANGE` OK se descubre CTS (0x1805,
lee 0x2A2B → `settimeofday` + RTC) y ANCS (servicio → características → descriptores → suscribir *Data Source* y luego
*Notification Source*). Al desconectar, vuelve a anunciar. `BLE_GAP_EVENT_REPEAT_PAIRING` (el iPhone "olvidó" el reloj)
borra el bond viejo y acepta el nuevo. Ajustes → "Olvidar iPhone" = `ble_store_util_delete_all`.

**Hora**: PCF85063 (I²C 0x51, registros BCD 0x04-0x0A) con driver propio de ~120 líneas (referencia:
`SensorPCF85063.hpp` de SensorLib en el repo Waveshare). Al arrancar se lee el RTC; con cada conexión se corrige desde
el iPhone (CTS). Zona horaria fija Chile (`TZ` en Kconfig; CTS ya entrega hora local).

**UI** (`reloj_ui`, LVGL 9, gestos): carátula (hora grande, fecha, iconos BLE/batería, no leídas) → deslizar arriba:
lista (app, título, hora) → tocar: detalle con mensaje completo y botones Descartar/Cerrar → deslizar abajo desde la
carátula: ajustes (brillo, Bluetooth: estado/nombre/olvidar, Acerca de: versión, heap libre, dirección MAC).
Aviso emergente 6 s al llegar una notificación no silenciosa. Pantalla apagada a los 20 s sin tocar (brillo 0 +
`esp_lcd_panel_disp_on_off(false)`); despierta con toque (INT GPIO15) o PWR corto. Fuentes con acentos generadas
con `npx lv_font_conv` (Montserrat viene dentro del componente LVGL) rangos `0x20-0x7F,0xA0-0xFF`; los emoji se
sustituyen por `□`.

**Presupuesto de RAM** (se mide, no se supone): registrar `esp_get_free_heap_size()` tras el BSP+LVGL, tras NimBLE
y con la UI completa; objetivo **≥ 100 KB libres al cerrar F2** para dejar sitio a WiFi/audio. Perillas si falta:
`LVGL_BUF_HEIGHT`, contadores de mbuf de NimBLE, tamaños de pila, nivel de log.

## Fases

### F0 — Entorno, respaldo y "hola mundo" (1 sesión)

1. Pasos de Windows (Felipe) y WSL de la sección anterior; `idf.py --version` debe decir v5.5.5.
2. `mkdir ~/reloj_pulsera && git init`; submódulo `referencias/waveshare` (shallow); copiar a `referencias/` y `docs/wiki/`
   el material ya bajado en el scratchpad de esta sesión
   (`/tmp/claude-1000/-home-felipe/0389fffe-d631-4886-b15d-81881bf09e32/scratchpad/waveshare/{wiki*.txt,bsp,xiaozhi,ancs}`).
3. **Respaldo del firmware de fábrica antes de escribir nada**: `herramientas/respaldo_flash.sh` →
   `esptool.py --chip esp32c6 -p /dev/ttyACM0 -b 921600 read_flash 0 0x1000000 respaldo/fabrica_<fecha>.bin`,
   `espefuse.py summary`, sha256 y comparación informativa con `referencias/waveshare/firmware/*.bin`.
4. Compilar y flashear tal cual `referencias/waveshare/examples/esp-idf/02_lvgl_demo_v9` (`idf.py set-target esp32c6`,
   `idf.py -p /dev/ttyACM0 flash monitor`) para validar toolchain + usbipd + BSP.
5. **Prueba de restauración**: `esptool.py write_flash 0 respaldo/fabrica_<fecha>.bin` → debe arrancar el firmware de fábrica.
6. Esqueleto `firmware/` con BSP + LVGL mostrando "Hola" y la hora del RTC, log del heap libre; primer commit.

Verificación: demo LVGL con táctil funcionando; sha256 del respaldo anotado en `respaldo/`; fábrica restaurada y luego
"Hola" en pantalla; `idf.py monitor` legible desde WSL.

### F1 — Vinculación con el iPhone y hora (2-3 sesiones)

1. `reloj_hw`: PMIC (XPowersLib sobre el bus del BSP: batería %, VBUS, IRQ de PWR corto/largo, apagado a 4 s como XiaoZhi,
   `disableTSPinMeasure()`), RTC PCF85063, control de pantalla (brillo/apagado) y temporizador de inactividad.
2. `reloj_ble`: init NimBLE, anuncio con solicitud ANCS, bonding Just Works, reconexión automática, CTS → hora.
3. `reloj_ui`: carátula + ajustes/Bluetooth (estado: anunciando / conectado / vinculado; nombre; "Olvidar iPhone"; MAC)
   + acerca de (heap). Fuentes con acentos generadas.
4. Kconfig del proyecto: nombre BLE, segundos de pantalla, zona horaria.

Verificación en hardware: en el iPhone, Ajustes → Bluetooth → "Reloj Felipe" en "Otros dispositivos" → Emparejar →
el reloj muestra "Vinculado" y la hora cambia a la del iPhone (log: `enc_change status=0`, `CTS: <fecha>`). Reiniciar el
reloj: reconecta solo en < 10 s sin tocar el iPhone. Alejarse/acercarse: reconecta. "Omitir dispositivo" en el iPhone y
volver a emparejar: funciona (REPEAT_PAIRING). PWR corto enciende/apaga pantalla; 20 s sin tocar la apaga; PWR 4 s apaga
el reloj. Heap libre anotado en `docs/ram.md`.

### F2 — Notificaciones ANCS (2-3 sesiones)

1. `reloj_notif`: parser de *Data Source* con reensamblado, anillo de 24, mapa de apps; **tests en host primero**
   (`test_host/`, gcc): fragmentos partidos en cualquier byte, atributos vacíos, UTF-8 con acentos, anillo lleno, Removed de
   UID inexistente.
2. `reloj_ble`: descubrimiento ANCS, suscripciones, cola de peticiones al *Control Point* (una pendiente a la vez),
   GetAppAttributes, PerformNotificationAction, eventos a la app.
3. `reloj_ui`: aviso emergente, lista, detalle con Descartar, contador en carátula, pantalla de llamada entrante.

Verificación en hardware: mensaje de WhatsApp al propio número (el bot de Felipe sirve de emisor) → aparece en < 2 s con
"WhatsApp", remitente y texto con tildes; SMS/Mail/Calendario igual; "Descartar" en el reloj lo borra del centro de
notificaciones del iPhone; llamar al iPhone → pantalla de llamada, rechazar desde el reloj corta; al reconectar, las
notificaciones existentes llegan con flag PreExisting sin aviso emergente; 50 notificaciones seguidas no rompen el heap
(comparar heap antes/después). Nota iOS: los modos de Concentración suprimen ANCS a nivel de sistema.

### F3 — Asistente (decisión pendiente, se planifica aparte)

Dos rutas viables cuando Felipe decida: (a) cliente propio por WiFi contra su `server.py` (micrófono ES7210 → WAV/Opus
por HTTP → texto/audio de vuelta → pantalla/altavoz ES8311); (b) mantener un XiaoZhi apuntando a un servidor
compatible (xiaozhi-esp32-server) con sus claves. Mientras tanto: WiFi no se inicializa, pines de audio libres, RAM
reservada. Regalo gratuito de F2: los avisos que el bot mande por WhatsApp ya aparecen en el reloj.

### F4 — Autonomía (después de F2)

Apagado de rieles no usados del AXP2101, *light sleep* con conexión BLE mantenida (`CONFIG_PM_ENABLE`,
tickless idle, `CONFIG_BT_LE_SLEEP_ENABLE`), despertar por táctil/PWR/gesto de muñeca (QMI8658 wake-on-motion por INT1),
brillo adaptativo, medición con el % de batería del PMIC en el tiempo.

## Riesgos y trampas conocidas

- **RAM**: 512 KB sin PSRAM para LVGL + NimBLE (+ WiFi/audio después). Mitigación: buffer LVGL de 40 líneas, NimBLE (no
  Bluedroid), medir en cada fase, objetivo ≥ 100 KB libres.
- **usbipd**: el USB-Serial/JTAG se re-enumera al entrar/salir de modo descarga; `--auto-attach` y, si se pierde, reatachar.
  Plan B sin usbipd: compilar en WSL y flashear con `py.exe -m esptool --port COM3` desde Windows.
- **Python 3.14** en WSL vs ESP-IDF: contingencia con `uv` + Python 3.12 descrita arriba.
- **iOS**: solo lista periféricos que solicitan ANCS; exige bonding; "Omitir dispositivo" deja un bond huérfano en el reloj
  (se maneja); Concentración silencia ANCS; los emoji no tienen glifo.
- **Límite de sesión de la cuenta**: los 7 agentes de lectura en paralelo lo agotaron hoy (se reinicia a las 15:50);
  en ejecución, preferir trabajo directo y agentes solo para tareas independientes grandes.
- **Restauración de fábrica**: siempre desde `respaldo/fabrica_<fecha>.bin` a offset 0 (imagen completa de 16 MB); nunca
  mezclar bootloader/particiones de fábrica con las nuestras.
- **AMOLED**: evitar UI estática horas enteras a brillo alto (quemado); el apagado por inactividad ya lo cubre.

## Referencias

- Wiki: https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-2.06 (+ subpáginas Arduino, ESP-IDF, FAQ, Firmware-Flashing).
- Repo: https://github.com/waveshareteam/ESP32-C6-Touch-AMOLED-2.06 (commit 2a3608d); `docs/hardware.md` es la tabla de pines fiable.
- BSP: https://github.com/waveshareteam/Waveshare-ESP32-components/tree/master/bsp/esp32_c6_touch_amoled_2_06 (v2.0.0).
- Puerto de fábrica XiaoZhi: https://github.com/78/xiaozhi-esp32/tree/main/main/boards/waveshare/esp32-c6-touch-amoled-2.06.
- Ejemplo ANCS NimBLE: https://github.com/espressif/esp-idf/tree/v5.5.5/examples/bluetooth/nimble/ble_ancs.
- Especificación ANCS de Apple (UUIDs, comandos, atributos) y CTS (0x1805 / 0x2A2B).

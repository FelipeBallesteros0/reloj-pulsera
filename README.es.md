# Reloj de pulsera — firmware propio para el Waveshare ESP32-C6-Touch-AMOLED-2.06

*[Read in English](README.md)*

Un firmware escrito desde cero para la placa de desarrollo con forma de reloj de Waveshare: **notificaciones del
iPhone por BLE (ANCS)**, hora sincronizada, cliente del asistente de voz **ANDIA**, algunas apps y un trabajo de
energía que llevó la batería de ~11,5 h a unas **40 h estimadas** en reposo (2,5 %/h medido durante 5 h).

No se reutiliza código del firmware de fábrica (lanzador esp-brookesia + XiaoZhi AI); solo el BSP oficial de la placa,
como drivers.

| | |
| --- | --- |
| MCU | ESP32-C6 (RISC-V, 160 MHz), **512 KB de SRAM, sin PSRAM**, flash de 16 MB |
| Pantalla | AMOLED de 2,06" 410×502 (CO5300, QSPI), táctil capacitivo FT3168, esquinas con radio de ~115 px |
| Además | PMIC AXP2101, RTC PCF85063, IMU QMI8658, códec ES8311 + ES7210 (altavoz + 2 micrófonos), Li-Po de ~400 mAh |
| Pila | ESP-IDF **v5.5.5**, LVGL **9.5**, **NimBLE** |

## Qué hace

- **Se vincula al iPhone** sin app en el teléfono y toma la hora de él (Current Time Service) para el RTC.
- **Notificaciones** (Apple Notification Center Service): tarjeta emergente, lista y detalle con acentos y emojis
  (Noto Emoji monocroma como respaldo); **Descartar** en el reloj la borra también del iPhone; pantalla de llamada.
- **Asistente de voz ANDIA** (cliente): micrófono → WebSocket → un servidor en una Raspberry Pi (voz a texto, LLM,
  síntesis de voz) → altavoz. Funciona por la red de casa (`ws://`) o por un túnel de Cloudflare (`wss://`, TLS 1.2).
  El audio va en IMA ADPCM 4:1 con señal débil y en PCM con señal fuerte. Modo de escucha permanente opcional
  («Oye Andia»). El servidor es un proyecto aparte y privado; el protocolo se resume abajo.
- **Apps**: Configuración (sonido, pantalla, ANDIA, Bluetooth, WiFi, acerca de, apagar), Sensores (IMU), Flappy, Snake.
- **Energía**: light sleep con tickless idle y sueño del módem BLE con la pantalla apagada; WiFi, códec, panel e IMU
  solo se encienden mientras se usan. «Acerca de» muestra el historial de batería, la descarga y el motivo del último
  arranque.

## Vincular con el iPhone (primera vez)

iOS 16+ **no muestra en Ajustes → Bluetooth los periféricos BLE genéricos**, aunque soliciten ANCS. El primer
emparejamiento se hace desde una app de escaneo BLE:

1. Instala **nRF Connect for Mobile** (Nordic) en el iPhone.
2. Scanner → SCAN → conectar a **«Reloj»**. El reloj pide cifrado e iOS muestra su «Solicitud de enlace» → Enlazar.
3. Listo: iOS guarda el enlace y desde entonces reconecta solo (sin la app).

Para desvincular: en el reloj, Configuración → Bluetooth → «Olvidar iPhone»; en el iPhone, Ajustes → Bluetooth → Omitir.

## Uso

- Carátula: hora, fecha, Bluetooth y batería. **Deslizar hacia arriba → apps; hacia abajo → notificaciones.**
- Botón PWR: corto = pantalla encendida/apagada; largo = hablar con ANDIA; mantenido más tiempo apaga el reloj (PMIC).

## Compilar y flashear

Se compila en WSL2 con ESP-IDF v5.5.5 y se flashea desde Windows (esptool por el USB-Serial/JTAG nativo, COM3),
porque usbip se atasca en transferencias grandes.

```bash
. ~/esp/esp-idf/export.sh
cd firmware
idf.py menuconfig                      # la primera vez: Reloj → tus redes WiFi, URL y token de ANDIA
idf.py build
../herramientas/flash_win.sh           # flashea firmware/build por COM3
../herramientas/monitor_win.sh 15 COM3 --reset
```

Tus redes WiFi y la URL/token de ANDIA quedan en `firmware/sdkconfig`, que **git ignora** (no lo subas nunca). La
consola por USB-Serial/JTAG solo imprime si el puerto está abierto desde el arranque; por eso `--reset`.

Antes de flashear nada, respalda la imagen de fábrica: `herramientas/respaldo_flash.sh`
(se restaura con `herramientas/restaurar_fabrica.sh`).

## Estructura

- `firmware/` — proyecto ESP-IDF. Componentes: `reloj_hw` (pantalla, PMIC, RTC, IMU, botones), `reloj_ble` (NimBLE,
  ANCS, CTS), `reloj_notif` (almacén de notificaciones), `reloj_ui` (interfaz LVGL y apps), `reloj_red` (WiFi),
  `reloj_voz` (audio I²S, ADPCM, búfer anillo), `reloj_andia` (cliente WebSocket), `reloj_energia` (política de
  energía y registro de batería).
- `test_host/` — pruebas unitarias que corren en el PC (`make` en esa carpeta): ANCS, CTS, ADPCM, anillo, registro
  de batería y política de energía.
- `docs/` — notas de hardware, presupuesto de RAM (`ram.md`), medidas de energía (`energia.md`), el plan y los
  documentos de diseño.
- `herramientas/` — scripts de flasheo, consola, USB a WSL, respaldo/restauración y generación de fuentes.
- `referencias/` — material de terceros solo para consultar (repo de Waveshare como submódulo, BSP, puerto de
  XiaoZhi para la placa, ejemplo ANCS de ESP-IDF).

## Protocolo de ANDIA (resumen)

Tramas de texto JSON y tramas binarias de audio por un único WebSocket. El reloj abre con
`{"t":"hola","token":"…","reloj":"<MAC>","v":1,"audio":"adpcm","bajada":"pcm|adpcm"}`; una conversación empieza con
`{"t":"inicio",…}`, el reloj envía el audio del micrófono (16 kHz mono, bloques ADPCM) y el servidor responde con
texto y audio de voz (`bajada`: `pcm` o `adpcm`, negociado en cada conversación). Detalle en
`docs/superpowers/specs/2026-09-03-andia-diseno.md`.

## Limitaciones

Es un prototipo de escritorio: solo iPhone (ANCS), interfaz en español, el asistente necesita tu propio servidor, y
la placa no tiene sello ni motor de vibración. Las cifras de batería son de una sola unidad.

## Licencia

MIT (ver `LICENSE`). El material de terceros mantiene su licencia: ver `NOTICE.md`.

# Reloj de pulsera — firmware propio para Waveshare ESP32-C6-Touch-AMOLED-2.06

Firmware propio (ESP-IDF v5.5.5 + LVGL 9.5 + NimBLE) para el reloj de desarrollo de Waveshare.
Objetivos por fases: (F1) vincularse al iPhone por Bluetooth y tomar la hora, (F2) mostrar las
notificaciones del iPhone (protocolo ANCS de Apple), (F3) asistente virtual, (F4) autonomía.

El plan completo y las decisiones están en `docs/plan.md`.

## Estructura

- `firmware/` — proyecto ESP-IDF (la app).
- `docs/` — notas de hardware, protocolo ANCS, RAM, y el texto de la wiki de Waveshare (`docs/wiki/`).
- `referencias/` — material de terceros solo para consultar: repo de Waveshare (submódulo), BSP v2.0.0,
  puerto de fábrica de XiaoZhi para esta placa, ejemplo NimBLE ANCS de ESP-IDF.
- `respaldo/` — (no versionado) volcado completo de la flash de fábrica y resumen de eFuses.
- `herramientas/` — scripts: pasar el USB a WSL, respaldar/restaurar flash, generar fuentes.

## Emparejar con el iPhone (primera vez)

iOS 16+ **no muestra en Ajustes → Bluetooth** a los periféricos BLE genéricos, aunque soliciten ANCS (política de
Apple; le pasa a todos los relojes caseros). El emparejamiento inicial se hace desde una app de escaneo BLE:

1. Instalar **nRF Connect for Mobile** (Nordic) en el iPhone.
2. Scanner → SCAN → conectar a **"Reloj"**. El reloj pide cifrado y iOS muestra su diálogo "Solicitud de enlace" → Enlazar.
3. Listo: el bond queda en el sistema; iOS reconecta solo cada vez que el reloj anuncia (sin la app). Al vincular,
   el reloj toma la hora del iPhone (Current Time Service) y la guarda en el RTC.

Para desemparejar: en el reloj, Ajustes → "Olvidar iPhone"; en el iPhone, Ajustes → Bluetooth → "Omitir dispositivo".

## Uso del reloj

- Carátula: hora, fecha, Bluetooth y batería. **Deslizar hacia abajo → notificaciones; hacia arriba → ajustes** (como en el teléfono).
- Notificaciones: tarjeta emergente 6 s al llegar (tocarla abre el detalle); en el detalle, **Descartar** la borra también
  del iPhone. Llamada entrante: pantalla propia con Contestar/Rechazar.
- Ajustes: brillo, estado Bluetooth, "Olvidar iPhone" (pide un segundo toque), versión y RAM libre.
- Botón PWR: corto = pantalla on/off; 4 s = apagar el reloj. La pantalla se apaga sola a los 20 s sin tocar.

Estado (2026-09-03): **F0, F1 y F2 completas y probadas con el iPhone** (vinculación, hora, notificaciones con acentos y
emojis, descartar). Pendiente: probar llamada entrante, ráfagas de notificaciones y modos de Concentración; F3 (asistente)
por decidir; F4 (autonomía).

## Compilar y flashear

Flujo por defecto: **compilar en WSL, flashear y leer la consola desde Windows** (esptool nativo en COM3).
usbip funciona para adjuntar el reloj a WSL, pero se atasca en transferencias masivas (una lectura de flash quedó
congelada en 172 KB); el esptool de Windows lee/escribe los 16 MB en ~1 minuto.

```bash
idf                                  # alias de ". ~/esp/esp-idf/export.sh"
cd firmware && idf.py build          # compila (ESP-IDF v5.5.5, target esp32c6)
../herramientas/flash_win.sh         # flashea firmware/build por COM3 (py.exe -m esptool)
../herramientas/monitor_win.sh 15 COM3 --reset   # captura 15 s de consola reiniciando el reloj (ver nota)
```

Nota sobre la consola: la salida por USB-Serial/JTAG solo llega si el puerto está abierto **desde el arranque**; si
se abre después, no sale nada. Por eso el monitor se usa con `--reset`. Para capturas largas en segundo plano usar
`SALIDA=C:/ruta/consola.log herramientas/monitor_win.sh 1200 COM3 --reset` (escribe el archivo directamente; la
salida estándar de un .exe lanzado en segundo plano desde WSL llega vacía). La primera línea del archivo trae el
PID de Windows para pararlo: `powershell Stop-Process -Id <pid>`.

Otras herramientas:

- `herramientas/usb.sh windows|wsl|estado` — si hace falta el reloj en WSL (`/dev/ttyACM0`, p. ej. `idf.py monitor`
  interactivo), `usb.sh wsl` lo adjunta con re-attach automático; `usb.sh windows` lo devuelve a COM3.
- `herramientas/respaldo_flash.sh` — volcado de 16 MB desde WSL (lento/atascable por usbip; preferir desde Windows:
  `py.exe -m esptool --chip esp32c6 -p COM3 -b 921600 read-flash 0 0x1000000 C:/ruta/fabrica.bin`).
- `herramientas/restaurar_fabrica.sh <imagen>` — vuelve a la fábrica desde `respaldo/` (o con esptool de Windows, `write-flash 0x0`).
- Regla udev `/etc/udev/rules.d/99-esp32.rules` (MODE 0666 para VID 303a) para que WSL pueda abrir el puerto sin `dialout`.

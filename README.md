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

## Compilar y flashear (WSL)

```bash
idf                      # alias de ". ~/esp/esp-idf/export.sh"
herramientas/usb.sh      # pasa el reloj (COM3 en Windows) a WSL como /dev/ttyACM0
cd firmware && idf.py -p /dev/ttyACM0 flash monitor
```

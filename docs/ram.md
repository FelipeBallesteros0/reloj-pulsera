# Presupuesto de RAM (ESP32-C6: 512 KB SRAM, sin PSRAM)

Medido con `esp_get_free_heap_size()` en la consola. Objetivo del plan: **≥ 100 KB libres al cerrar F2** para dejar
sitio a WiFi/audio en F3.

| Fecha | Commit | Momento | Libre | Bloque mayor | Notas |
| --- | --- | --- | ---: | ---: | --- |
| 2026-09-03 | effe798 | al arrancar (antes del BSP) | 326 268 B | 303 104 B | heap_init: 313 KiB + 11 KiB |
| 2026-09-03 | effe798 | tras BSP + LVGL (buffer 40 líneas = 33 KB) | 229 876 B | 212 992 B | el BSP+port+SPI cuestan ~96 KB en total |
| 2026-09-03 | effe798 | con la pantalla "Hola" dibujada | 228 908 B | 212 992 B | 3 etiquetas |

Referencia: el firmware de fábrica (brookesia phone demo, IDF 5.5.1) arranca con 412 KiB de heap porque no reserva
IRAM para nuestra configuración (`CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM` y optimización -O2 no cambian esto; revisar si
hace falta apurar).

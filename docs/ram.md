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
| 2026-09-03 | 3668523 | F2 completa, vinculado y suscrito a ANCS, 2 notificaciones | ~166 000 B | ~147 000 B | tras NimBLE sin conexión: 188 KB; la conexión + cliente GATT cuestan ~22 KB; fuente emoji va en flash |
| 2026-09-04 | d71904a | F3b+F3e: WiFi a dieta arrancado, BLE vinculado, sin conversación | ~57 000 B | ~42 000 B | WiFi+lwip cuestan ~110 KB (STATIC_RX 4, DYN 8/8, sin AMPDU/IPv6); códec en mono (estéreo costaba 36 KB) |
| 2026-09-04 | d71904a | wss:// conectado con la Pi por Cloudflare (TLS 1.2, búferes dinámicos) | ~34 000 B | | TLS cuesta ~23 KB en reposo; en conversación baja a ~25-28 KB |

Lecciones de F3e (túnel): Cloudflare manda registros TLS de hasta **16 KB** (medido: 16408 B); con
`MBEDTLS_SSL_IN_CONTENT_LEN=6144` mbedTLS falla con `-0x7100` (BAD_INPUT_DATA, "requesting more data than fits") en cuanto
llega el audio de respuesta. Ahora está en 16384; con `MBEDTLS_DYNAMIC_BUFFER` el búfer de entrada se reserva al tamaño de
cada registro y se libera al consumirlo, así que solo cuesta RAM mientras llega un registro grande. Para que sean chicos,
el audio va en **IMA ADPCM 4:1** (8 KB/s) y la Pi espacia los trozos (medido tras el cambio: registro mayor 2085 B). Con
las ventanas TCP de 2880 B, PCM crudo (32 KB/s) exigía RTT < 90 ms; con ADPCM basta < 360 ms.

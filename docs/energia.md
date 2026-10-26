# Medidas de energía

Protocolo: cargar a tope → desconectar USB → pantalla apagada, sin tocar, 2 h → leer "Acerca de" (descarga a 2 h).
1 %/h ≈ 4 mA (batería de 400 mAh). Vigilia desactivada salvo que se diga lo contrario.

**Cómo leer el medidor** (aprendido el 2026-09-05): el AXP2101 marca 100 % mientras carga y, minutos después de
quitar el USB, cae a la carga real (~70 % tras una carga "completa" de una hora). El % también baja de golpe con la
pantalla encendida (estimación por tensión bajo carga) y se recupera al apagarla. Por eso el dato válido es la
**ventana de 2 h de "Acerca de"** leída nada más encender la pantalla, no la diferencia con el 100 % de partida; la
línea muestra además los mV y el % de hace 30 min y 2 h para juzgarla. Leer rápido y apagar. La medida más fiable
es dejarlo **toda la noche** y leer por la mañana; "Acerca de" indica también el motivo y la hora del último arranque,
porque un reinicio vacía el registro.

Referencia de Waveshare con su firmware: 1 h a brillo máximo, 3-4 h con pantalla apagada, ~6 h en bajo consumo.

| Fecha | Etapa | Estado del firmware | %/h (2 h) | Notas |
| --- | --- | --- | --- | --- |
| 2026-09-05 17:05-19:08 | 0 (base, descartada: ventanas ruidosas) | `b456fd8` (Acerca de: descarga…) | **~5** (ventana 2 h a las 19:08; a las 19:20, con la pantalla encendida entretanto, 9) | **vigilia activada**, WiFi en casa, BLE con el iPhone. Carátula 100 % al desconectar → 62 % a las 19:08 → 52 % a las 19:20: el 100 % era el de carga, no vale. ≈ 20 mA de media |
| 2026-09-05 20:12 → 06 08:42 | 0 (base) | `0d416fb` | **~9** (de 4213 mV lleno a 2 % / 3413 mV en ~11,5 h; cambio de hora esa noche) | **vigilia ACTIVADA** (Felipe creyó haberla apagado; la consola del día siguiente dijo `vigilia activada`), WiFi en casa, BLE. Lectura a las 21:41 de las ventanas: 24 %/h (30 min), 17,6 %/h (2 h): el medidor salta; la cifra buena es la de toda la noche. Por la mañana todas las ventanas en `--`: **el reloj se reinició** (registro en RAM vacío), seguramente brownout al agotarse. ≈ 35 mA de media, **autonomía ~11,5 h** |
| 2026-09-06 22:32 → 07 <10:29 | 1 | `2516812` (gestor de energía) | **se agotó antes de 12 h** (a las 10:29, 2 % y 3535 mV, arranque por `encendido`: el PMIC cortó y el USB lo revivió) | **vigilia desactivada** (persistencia arreglada). WiFi, códec, panel e IMU apagados en reposo, pero la CPU sigue a 160 MHz y el BLE sin sueño: ~25-30 mA de suelo. Sin mejora apreciable frente a la base: la etapa 2 es la palanca |
| 2026-09-07 12:35 | 2 (parcial) | `a589014` + LVGL en flash | (4 min de reposo) **dormido 88 %** (19 777 sueños ≈ 82/s, los eventos BLE) | Con PM la primera prueba dio 0 sueños: dos candados escondidos, el I²S que el BSP deja habilitado y la tarea del port de LVGL girando cada 1 ms con los temporizadores parados. Arreglados. RAM: el PM cuesta ~22 KB (retención del módem para el sueño del BLE); sacar LVGL de la IRAM (`LV_ATTRIBUTE_FAST_MEM_USE_IRAM`) liberó 112 KB → 136 KB libres en reposo |
| 2026-10-25 22:30 → 26 03:48 | 2 | `b79d8e0` (main tras integrar f4) | **2,5** (ventana 2 h: 94 → 89 %; 30 min: 4,0) | 5 h 20 min en reposo, pantalla apagada, BLE vinculado, vigilia desactivada. 100 % de carga → 89 % / 4013 mV. «Último reposo»: dormido 77 % (149 sueños, tramo corto al encender). ≈ 10 mA de media → **autonomía estimada ~40 h** (frente a ~11,5 h de la base) |

# Medidas de energía

Protocolo: cargar a tope → desconectar USB → pantalla apagada, sin tocar, 2 h → leer "Acerca de" (descarga a 2 h).
1 %/h ≈ 4 mA (batería de 400 mAh). Vigilia desactivada salvo que se diga lo contrario.

**Cómo leer el medidor** (aprendido el 2026-09-05): el AXP2101 marca 100 % mientras carga y, minutos después de
quitar el USB, cae a la carga real (~70 % tras una carga "completa" de una hora). El % también baja de golpe con la
pantalla encendida (estimación por tensión bajo carga) y se recupera al apagarla. Por eso el dato válido es la
**ventana de 2 h de "Acerca de"** leída nada más encender la pantalla, no la diferencia con el 100 % de partida; la
línea muestra además los mV y el % de hace 30 min y 2 h para juzgarla. Leer rápido y apagar.

Referencia de Waveshare con su firmware: 1 h a brillo máximo, 3-4 h con pantalla apagada, ~6 h en bajo consumo.

| Fecha | Etapa | Estado del firmware | %/h (2 h) | Notas |
| --- | --- | --- | --- | --- |
| 2026-09-05 17:05-19:08 | 0 (base) | `b456fd8` (Acerca de: descarga…) | **~5** (ventana 2 h a las 19:08; a las 19:20, con la pantalla encendida entretanto, 9) | **vigilia activada**, WiFi en casa, BLE con el iPhone. Carátula 100 % al desconectar → 62 % a las 19:08 → 52 % a las 19:20: el 100 % era el de carga, no vale. ≈ 20 mA de media |
| (fecha) | 0 (base) | `b456fd8` | | **vigilia desactivada** |

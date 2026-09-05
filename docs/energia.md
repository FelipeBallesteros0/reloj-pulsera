# Medidas de energía

Protocolo: cargar a tope → desconectar USB → pantalla apagada, sin tocar, 2 h → leer "Acerca de" (descarga a 2 h).
1 %/h ≈ 4 mA (batería de 400 mAh). Vigilia desactivada salvo que se diga lo contrario.

Referencia de Waveshare con su firmware: 1 h a brillo máximo, 3-4 h con pantalla apagada, ~6 h en bajo consumo.

| Fecha | Etapa | Estado del firmware | %/h (2 h) | Notas |
| --- | --- | --- | --- | --- |
| 2026-09-05 | 0 (base) | `b456fd8` (Acerca de: descarga…) | ~18 (carátula: 100 % → 62 % en ~2 h 05) | **vigilia activada**, WiFi en casa, BLE con el iPhone. "Acerca de" marcó "5 %" en una de sus ventanas (Felipe no precisó cuál): el medidor del AXP2101 parece quedarse en 100 % un rato tras cargar y caer de golpe; ≈ 75 mA de media |
| (fecha) | 0 (base) | `b456fd8` | | **vigilia desactivada** |

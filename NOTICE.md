# Third-party material

The MIT license in `LICENSE` covers the code written for this project. These parts keep their own licenses:

| Path | What | License |
| --- | --- | --- |
| `firmware/fuentes_src/NotoEmoji.ttf`, `firmware/components/reloj_ui/fuentes/emoji_20.c` | Noto Emoji (Google), and the LVGL font generated from it | SIL Open Font License 1.1 |
| `firmware/components/reloj_ui/fuentes/fuente_*.c` | Montserrat (shipped with LVGL), converted with lv_font_conv | SIL Open Font License 1.1 |
| `referencias/waveshare/` (git submodule) | Waveshare's ESP32-C6-Touch-AMOLED-2.06 repository | see that repository |
| `referencias/bsp_v2.0.0/` | Copy of the `waveshare/esp32_c6_touch_amoled_2_06` BSP, for reference | see its `idf_component.yml` / component registry page |
| `referencias/xiaozhi_board/` | Board port from 78/xiaozhi-esp32, for reference | MIT |
| `referencias/ancs_ejemplo/` | ESP-IDF `ble_ancs` example (NimBLE), for reference | Apache License 2.0 (file headers) |

Dependencies fetched at build time by the ESP-IDF component manager (BSP, LVGL, XPowersLib, QMI8658 driver,
esp_websocket_client…) are not stored in this repository and keep their own licenses.

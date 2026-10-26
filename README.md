# Wrist watch — custom firmware for the Waveshare ESP32-C6-Touch-AMOLED-2.06

*[Leer en español](README.es.md)*

A from-scratch firmware for Waveshare's watch-shaped ESP32-C6 dev board: **iPhone notifications over BLE (ANCS)**,
time sync, a voice assistant client (**ANDIA**), a few apps and a power-management pass that took the board from
~11.5 h of battery to <!-- AUTONOMIA -->**(overnight measurement pending)**.

No code from the factory firmware (esp-brookesia launcher + XiaoZhi AI) is reused; only the official board support
package (BSP) is used, as drivers.

| | |
| --- | --- |
| MCU | ESP32-C6 (RISC-V, 160 MHz), **512 KB SRAM, no PSRAM**, 16 MB flash |
| Display | 2.06" AMOLED 410×502 (CO5300, QSPI), capacitive touch FT3168, corners with a ~115 px radius |
| Also on board | AXP2101 PMIC, PCF85063 RTC, QMI8658 IMU, ES8311 + ES7210 audio codec (speaker + 2 mics), ~400 mAh Li-Po |
| Stack | ESP-IDF **v5.5.5**, LVGL **9.5**, **NimBLE** |

## What it does

- **Pairs with an iPhone** without an app on the phone and takes the time from it (Current Time Service) into the RTC.
- **Notifications** (Apple Notification Center Service): pop-up card, list, detail view with accents and emojis
  (monochrome Noto Emoji as fallback); **Dismiss** on the watch also removes it from the iPhone; incoming call screen.
- **ANDIA voice assistant** (client): microphone → WebSocket → a server on a Raspberry Pi (speech-to-text, LLM, TTS)
  → speaker. Works over the home LAN (`ws://`) or through a Cloudflare tunnel (`wss://`, TLS 1.2). Audio is
  IMA ADPCM 4:1 when the link is weak and PCM when it is strong. Optional always-listening mode ("Oye Andia").
  The server is a separate, private project; the protocol is described below.
- **Apps**: Settings (sound, display, ANDIA, Bluetooth, WiFi, about, power off), Sensors (IMU), Flappy, Snake.
- **Power management**: light sleep with tickless idle and BLE modem sleep while the screen is off; WiFi, codec,
  panel and IMU are only powered while used. "About" shows battery history, discharge rate and the last reset reason.

## Pairing with the iPhone (first time)

iOS 16+ **does not list generic BLE peripherals in Settings → Bluetooth**, even when they solicit ANCS. The first
pairing is done from a BLE scanner app:

1. Install **nRF Connect for Mobile** (Nordic) on the iPhone.
2. Scanner → SCAN → connect to **"Reloj"**. The watch requests encryption and iOS shows its pairing request → Pair.
3. Done: the bond is stored by iOS, which reconnects on its own from then on (no app needed).

To unpair: on the watch, Settings → Bluetooth → "Olvidar iPhone"; on the iPhone, Settings → Bluetooth → Forget.

## Using it

- Watch face: time, date, Bluetooth and battery. **Swipe up → apps; swipe down → notifications.**
- PWR button: short press = screen on/off; long press = talk to ANDIA; holding it longer powers the watch off (PMIC).
- The UI is in Spanish.

## Building and flashing

Built on WSL2 with ESP-IDF v5.5.5; flashed from Windows (esptool over the native USB-Serial/JTAG, COM3), because
usbip stalls on large transfers.

```bash
. ~/esp/esp-idf/export.sh
cd firmware
idf.py menuconfig                      # first time: Reloj → your WiFi networks, ANDIA URL and token
idf.py build
../herramientas/flash_win.sh           # flashes firmware/build through COM3
../herramientas/monitor_win.sh 15 COM3 --reset
```

Your WiFi networks and the ANDIA URL/token go in `idf.py menuconfig` → *Reloj* and are saved in `firmware/sdkconfig`,
which is **ignored by git** (never commit it). The console on the USB-Serial/JTAG only prints if the port is open from
boot, hence `--reset`.

Before flashing anything, back up the factory image: `herramientas/respaldo_flash.sh`
(restore with `herramientas/restaurar_fabrica.sh`).

## Repository layout

- `firmware/` — ESP-IDF project. Components: `reloj_hw` (display, PMIC, RTC, IMU, buttons), `reloj_ble` (NimBLE,
  ANCS, CTS), `reloj_notif` (notification store), `reloj_ui` (LVGL UI and apps), `reloj_red` (WiFi), `reloj_voz`
  (I²S audio, ADPCM, ring buffer), `reloj_andia` (WebSocket client), `reloj_energia` (power policy and battery log).
- `test_host/` — unit tests that run on the PC (`make` in that folder): ANCS parsing, CTS, ADPCM, ring buffer,
  battery log, power policy.
- `docs/` — hardware notes, RAM budget (`ram.md`), power measurements (`energia.md`), the plan and the design
  documents (in Spanish).
- `herramientas/` — flashing, console, USB-to-WSL, backup/restore and font generation scripts.
- `referencias/` — third-party material for reference only (Waveshare repo as a submodule, BSP, XiaoZhi board port,
  ESP-IDF ANCS example).

## ANDIA protocol (summary)

JSON text frames plus binary audio frames over one WebSocket. The watch opens with
`{"t":"hola","token":"…","reloj":"<MAC>","v":1,"audio":"adpcm","bajada":"pcm|adpcm"}`; a conversation starts with `{"t":"inicio",…}`,
the watch streams microphone audio (16 kHz mono, ADPCM blocks) and the server answers with text and speech audio
(`bajada`: `pcm` or `adpcm`, negotiated per conversation). Details in
`docs/superpowers/specs/2026-09-03-andia-diseno.md`.

## Limitations

A desk prototype: iPhone only (ANCS), Spanish UI, the assistant needs your own server, and the board has no case
seal or vibration motor. Battery figures are measured on one unit.

## License

MIT (see `LICENSE`). Third-party material keeps its own license: see `NOTICE.md`.

# Hardware del ESP32-C6-Touch-AMOLED-2.06 (lo verificado)

Fuentes: esquemático V1.0 (`referencias/waveshare/Schematic/`), `referencias/waveshare/docs/hardware.md`,
BSP v2.0.0 (`referencias/bsp_v2.0.0/`), puerto de fábrica de XiaoZhi (`referencias/xiaozhi_board/`).
La wiki tiene texto copiado del modelo S3 y se equivoca en los botones: no fiarse de ella para GPIOs.

## MCU y memoria
- ESP32-C6 chip desnudo (no módulo), RISC-V 160 MHz, **512 KB SRAM, sin PSRAM**, flash externa 16 MB (XM25QH128, QIO).
- USB-C directo al USB-Serial/JTAG nativo (GPIO12 D−, GPIO13 D+). En Windows: `USB\VID_303A&PID_1001` → COM3.
- Antena PCB + conector iPEX sin poblar.

## Mapa de pines
| Función | GPIO |
| --- | ---: |
| AMOLED QSPI clock | 0 |
| AMOLED QSPI D0 / D1 / D2 / D3 | 1 / 2 / 3 / 4 |
| AMOLED CS | 5 |
| Amplificador de altavoz (enable) | 6 |
| I²C SCL (compartido) | 7 |
| I²C SDA (compartido) | 8 |
| Botón BOOT | 9 |
| Táctil RESET | 10 |
| AMOLED RESET | 11 |
| USB D− / D+ | 12 / 13 |
| Táctil INT | 15 |
| QMI8658 INT1 / INT2 (comparten pines con la UART0 de los pads) | 16 / 17 |
| I²S MCLK / BCLK / DIN (ADC→MCU) / WS / DOUT (MCU→DAC) | 19 / 20 / 21 / 22 / 23 |

Sin backlight PWM: el brillo es el registro 0x51 del panel (0-255). `LCD_TE` existe en el flex pero no está documentado (¿GPIO18?).

## Dispositivos I²C (bus GPIO7/8, 400 kHz)
| Chip | Dirección | Uso |
| --- | --- | --- |
| FT3168 táctil | 0x38 | driver `esp_lcd_touch_ft5x06` |
| AXP2101 PMIC | 0x34 | batería, carga, botón PWR, rieles, apagado |
| QMI8658 IMU | 0x6B | acelerómetro/giroscopio (fase F4) |
| PCF85063 RTC | 0x51 | hora con respaldo de batería (cristal propio de 32,768 kHz) |
| ES8311 códec | 0x18 | DAC → NS4150B → altavoz |
| ES7210 ADC | 0x40 | 2 micrófonos MEMS |

## Pantalla
- CO5300, 410×502, RGB565. El BSP usa `esp_lcd_sh8601` (comandos compatibles) con `esp_lcd_panel_set_gap(panel, 0x16, 0)`
  (22 px de desplazamiento en X) y un *rounder* que alinea las áreas de dibujo a coordenadas pares.
- Buffer LVGL del BSP: `CONFIG_BSP_DISPLAY_LVGL_BUF_HEIGHT` líneas × 410 × 2 bytes (por defecto 100 líneas = 82 KB; nosotros 40).

## Botones
- **PWR va al pin PWRON del AXP2101**, no a un GPIO. Se detecta por los flags de IRQ del PMIC (`PKEY_SHORT`, `PKEY_LONG`),
  sondeados por I²C (la línea IRQ del PMIC no llega al ESP). Apagado por hardware al mantenerlo (XiaoZhi: reg 0x27=0x10 → 4 s).
- **BOOT = GPIO9** (también strapping de descarga).

## PMIC AXP2101 (según el puerto de XiaoZhi)
- Solo DC1 = 3,3 V encendido (reg 0x80=0x01, 0x82=(3300-1500)/100), ALDO1/2 = 3,3 V para los micrófonos (0x90=0x03, 0x92/0x93);
  el resto apagado. Cargador: 4,1 V (0x64=0x02), precarga 50 mA (0x61=0x02), carga 400 mA (0x62=0x0A), fin 25 mA (0x63=0x01).
- 0x22=0b110: PWRON largo como fuente de apagado. `disableTSPinMeasure()` porque no hay termistor útil.
- La pantalla funciona sin tocar el PMIC (rieles por defecto).

## Modo descarga y USB
- esptool suele resetear a descarga por el USB-Serial/JTAG. Si no: mantener BOOT y encender. **Queda pegado en modo descarga**
  hasta apagar y volver a encender (PWR ≥ 6 s, luego PWR de nuevo).
- El puerto USB desaparece y reaparece al resetear: en WSL usar `usbipd attach --auto-attach` (`herramientas/usb.sh`).

## Batería
- Li-Po 3,7 V, conector MX1.25, ~400 mAh. Waveshare: 1 h a brillo máximo, 3-4 h con pantalla apagada, ~6 h en bajo consumo.

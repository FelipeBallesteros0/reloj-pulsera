#!/usr/bin/env bash
# Restaura una imagen COMPLETA de la flash (volcado de fábrica) en el offset 0.
# Uso: herramientas/restaurar_fabrica.sh <imagen.bin> [puerto]
set -euo pipefail
IMG=${1:?uso: restaurar_fabrica.sh <imagen.bin> [puerto]}
PORT=${2:-/dev/ttyACM0}
SIZE=$(stat -c %s "$IMG")
if [ "$SIZE" -gt 16777216 ] || [ "$SIZE" -lt 4194304 ]; then
  echo "Tamaño raro ($SIZE bytes): solo se restauran volcados completos (hasta 16 MiB)" >&2; exit 1
fi
[ "$SIZE" -eq 16777216 ] || echo "Aviso: la imagen no mide exactamente 16 MiB ($SIZE bytes); se escribe igual desde 0x0."
esptool.py --chip esp32c6 -p "$PORT" -b 921600 write_flash 0x0 "$IMG"
echo "Listo. Si el reloj no arranca solo: apagar y encender (PWR 6 s, luego PWR de nuevo)."

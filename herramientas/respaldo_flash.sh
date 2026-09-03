#!/usr/bin/env bash
# Vuelca los 16 MB de flash del reloj a respaldo/fabrica_<fecha>.bin, más eFuses, flash_id y sha256.
# Uso: herramientas/respaldo_flash.sh [puerto]   (requiere el entorno ESP-IDF activo: alias idf)
set -euo pipefail
PORT=${1:-/dev/ttyACM0}
DIR="$(cd "$(dirname "$0")/.." && pwd)/respaldo"
mkdir -p "$DIR"
STAMP=$(date +%Y%m%d_%H%M)
OUT="$DIR/fabrica_$STAMP.bin"
command -v esptool.py >/dev/null || { echo "Activa ESP-IDF primero (alias idf)" >&2; exit 1; }
esptool.py --chip esp32c6 -p "$PORT" -b 921600 flash_id | tee "$DIR/flash_id_$STAMP.txt"
esptool.py --chip esp32c6 -p "$PORT" -b 921600 read_flash 0 0x1000000 "$OUT"
espefuse.py --chip esp32c6 -p "$PORT" summary > "$DIR/efuse_$STAMP.txt" || echo "(espefuse falló, no es grave)"
sha256sum "$OUT" | tee -a "$DIR/SHA256SUMS"
ls -l "$OUT"

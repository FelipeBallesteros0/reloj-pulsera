#!/usr/bin/env bash
# Plan B: flashea desde Windows (esptool nativo, COM3) el build hecho en WSL. Evita los atascos de usbip.
# Uso: herramientas/flash_win.sh [dir_build] [puerto]     (por defecto firmware/build y COM3)
set -euo pipefail
BUILD=$(cd "${1:-$(dirname "$0")/../firmware/build}" && pwd)
PORT=${2:-COM3}
[ -f "$BUILD/flash_args" ] || { echo "No hay $BUILD/flash_args: compila primero (idf.py build)" >&2; exit 1; }
# flash_args trae rutas relativas al build; el Python de Windows las abre por UNC.
UNC="\\\\wsl.localhost\\Ubuntu${BUILD//\//\\}"
ARGS=()
while read -r a b; do
  [ -z "$a" ] && continue
  if [[ "$a" == 0x* ]]; then ARGS+=("$a" "$UNC\\${b//\//\\}"); else ARGS+=($a $b); fi
done < "$BUILD/flash_args"
cd /mnt/c
py.exe -m esptool --chip esp32c6 -p "$PORT" -b 921600 --before default_reset --after hard_reset write-flash "${ARGS[@]}"

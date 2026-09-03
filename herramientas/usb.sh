#!/usr/bin/env bash
# Pasa el reloj (ESP32-C6 USB JTAG/serial, VID 303a PID 1001) de Windows a WSL con usbipd-win.
# Requisito (una sola vez, PowerShell como administrador): usbipd bind --busid <BUSID>
set -euo pipefail
cd /mnt/c
LINE=$(usbipd.exe list 2>/dev/null | tr -d '\r' | grep -i '303a:1001' | head -1 || true)
if [ -z "$LINE" ]; then
  echo "No veo el reloj en 'usbipd list'. ¿Está conectado por USB? ¿Está instalado usbipd-win?" >&2
  exit 1
fi
BUSID=$(echo "$LINE" | awk '{print $1}')
STATE=$(echo "$LINE" | grep -o -E 'Not shared|Shared|Attached' | head -1 || echo "?")
echo "Reloj en BUSID $BUSID ($STATE)"
case "$STATE" in
  "Not shared")
    echo "Falta ejecutar en PowerShell como administrador: usbipd bind --busid $BUSID" >&2
    exit 2 ;;
  "Attached")
    echo "Ya estaba adjuntado a WSL." ;;
  *)
    # --auto-attach mantiene el proceso vivo y vuelve a adjuntar tras cada re-enumeración (reset, modo descarga).
    setsid nohup usbipd.exe attach --wsl --busid "$BUSID" --auto-attach >/tmp/usbipd_attach.log 2>&1 </dev/null &
    disown ;;
esac
for _ in $(seq 1 30); do [ -e /dev/ttyACM0 ] && break; sleep 0.5; done
if ls -l /dev/ttyACM* 2>/dev/null; then
  echo "Listo: usa -p /dev/ttyACM0"
else
  echo "No apareció /dev/ttyACM0; revisa /tmp/usbipd_attach.log" >&2
  exit 3
fi

#!/usr/bin/env bash
# Conmuta el reloj (ESP32-C6 USB JTAG/serial, VID 303a PID 1001) entre Windows y WSL con usbipd-win.
#   herramientas/usb.sh wsl      → lo adjunta a WSL (/dev/ttyACM0) con --auto-attach (re-adjunta tras cada reset)
#   herramientas/usb.sh windows  → mata el bucle de auto-attach y lo devuelve a Windows (COM3)
#   herramientas/usb.sh estado   → muestra en qué lado está
# Requisito (una sola vez, PowerShell como administrador): usbipd bind --busid <BUSID>
set -euo pipefail
cd /mnt/c
# El PATH que hereda WSL puede ser viejo (sin usbipd-win): usar la ruta completa si hace falta.
USBIPD=$(command -v usbipd.exe || echo "/mnt/c/Program Files/usbipd-win/usbipd.exe")
MODO=${1:-wsl}

linea() { "$USBIPD" list 2>/dev/null | tr -d '\r' | grep -i '303a:1001' | head -1 || true; }
LINE=$(linea)
if [ -z "$LINE" ]; then
  echo "No veo el reloj en 'usbipd list'. ¿Está conectado por USB? ¿Está instalado usbipd-win?" >&2
  exit 1
fi
BUSID=$(echo "$LINE" | awk '{print $1}')
STATE=$(echo "$LINE" | grep -o -E 'Not shared|Shared|Attached' | head -1 || echo "?")
echo "Reloj en BUSID $BUSID ($STATE)"

matar_bucle() {
  # El proceso de auto-attach vive del lado Windows; matarlo desde WSL con kill no basta.
  powershell.exe -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name='usbipd.exe'\" | Where-Object { \$_.CommandLine -match 'attach' } | ForEach-Object { Stop-Process -Id \$_.ProcessId -Force }" 2>/dev/null || true
}

case "$MODO" in
  estado)
    ls -l /dev/ttyACM* 2>/dev/null || echo "WSL: sin /dev/ttyACM"
    powershell.exe -NoProfile -Command "Get-CimInstance Win32_SerialPort | Select-Object -ExpandProperty DeviceID" 2>/dev/null | tr -d '\r' | sed 's/^/Windows: /'
    ;;
  windows)
    matar_bucle
    if [ "$STATE" = "Attached" ]; then "$USBIPD" detach --busid "$BUSID"; fi
    for _ in $(seq 1 20); do
      P=$(powershell.exe -NoProfile -Command "Get-CimInstance Win32_SerialPort | Select-Object -ExpandProperty DeviceID" 2>/dev/null | tr -d '\r')
      [ -n "$P" ] && { echo "Listo en Windows: $P"; exit 0; }
      sleep 2
    done
    echo "No apareció el puerto COM en Windows" >&2; exit 3 ;;
  wsl)
    case "$STATE" in
      "Not shared")
        echo "Falta ejecutar en PowerShell como administrador: usbipd bind --busid $BUSID" >&2
        exit 2 ;;
      "Attached")
        echo "Ya estaba adjuntado a WSL." ;;
      *)
        # --auto-attach mantiene el proceso vivo y vuelve a adjuntar tras cada re-enumeración (reset, modo descarga).
        setsid nohup "$USBIPD" attach --wsl --busid "$BUSID" --auto-attach >/tmp/usbipd_attach.log 2>&1 </dev/null &
        disown ;;
    esac
    for _ in $(seq 1 30); do [ -e /dev/ttyACM0 ] && break; sleep 0.5; done
    if ls -l /dev/ttyACM* 2>/dev/null; then echo "Listo: usa -p /dev/ttyACM0"; else
      echo "No apareció /dev/ttyACM0; revisa /tmp/usbipd_attach.log" >&2; exit 3; fi ;;
  *) echo "uso: usb.sh [wsl|windows|estado]" >&2; exit 1 ;;
esac

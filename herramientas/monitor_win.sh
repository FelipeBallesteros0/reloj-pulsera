#!/usr/bin/env bash
# Plan B: captura la consola del reloj desde Windows (pyserial en COM3) durante N segundos, sin resetearlo.
# Uso: herramientas/monitor_win.sh [segundos] [puerto] [--reset]
SEG=${1:-15}; PORT=${2:-COM3}; RESET=${3:-}
cd /mnt/c && py.exe - "$PORT" "$SEG" "$RESET" <<'PY'
import sys, time, serial
port, seg, reset = sys.argv[1], float(sys.argv[2]), sys.argv[3] == "--reset"
with serial.Serial(port, 115200, timeout=0.2) as s:
    if reset:
        s.dtr = False; s.rts = True; time.sleep(0.1); s.rts = False   # pulso de reset por USB-Serial/JTAG
    fin = time.time() + seg
    while time.time() < fin:
        d = s.read(4096)
        if d:
            sys.stdout.write(d.decode("utf-8", "replace")); sys.stdout.flush()
PY

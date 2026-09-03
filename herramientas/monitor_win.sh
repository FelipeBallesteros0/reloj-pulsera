#!/usr/bin/env bash
# Plan B: captura la consola del reloj desde Windows (pyserial en COM3) durante N segundos, sin resetearlo.
# Uso: herramientas/monitor_win.sh [segundos] [puerto] [--reset]
# Variable SALIDA=<ruta Windows> → escribe ahí (línea a línea) además de stdout. Necesario si se lanza en segundo
# plano desde WSL: la salida de un .exe lanzado en background llega vacía (trampa del interop), el archivo no.
SEG=${1:-15}; PORT=${2:-COM3}; RESET=${3:-}
cd /mnt/c && py.exe - "$PORT" "$SEG" "$RESET" "${SALIDA:-}" <<'PY'
import sys, time, serial, codecs, os
port, seg, reset, salida = sys.argv[1], float(sys.argv[2]), sys.argv[3] == "--reset", sys.argv[4]
dec = codecs.getincrementaldecoder("utf-8")("replace")   # no partir tildes entre lecturas
try:
    sys.stdout.reconfigure(encoding="utf-8")                 # la consola de Windows no es UTF-8 por defecto
except Exception:
    pass
f = open(salida, "a", encoding="utf-8") if salida else None
if f:
    f.write(f"PID_WINDOWS={os.getpid()}\n"); f.flush()   # para poder matarlo con Stop-Process -Id
with serial.Serial(port, 115200, timeout=0.2) as s:
    if reset:
        s.dtr = False; s.rts = True; time.sleep(0.1); s.rts = False   # pulso de reset por USB-Serial/JTAG
    fin = time.time() + seg
    while time.time() < fin:
        d = s.read(4096)
        if d:
            txt = dec.decode(d)
            sys.stdout.write(txt); sys.stdout.flush()
            if f:
                f.write(txt); f.flush()
if f:
    f.close()
PY

#!/usr/bin/env bash
# Consola + control remoto del reloj desde Windows (firmware con CONFIG_RELOJ_CONTROL_USB=y).
# Abre COM3 durante N segundos, guarda la consola en SALIDA y envía al reloj cada línea nueva de ORDENES.
# Uso (en segundo plano): SALIDA=C:/Users/Public/consola.log herramientas/control_win.sh 3600 COM3 [--reset] &
#       y para mandar una orden: herramientas/reloj.sh "swipe up"
SEG=${1:-600}; PORT=${2:-COM3}; RESET=${3:-}
ORDENES='C:/Users/Public/reloj_ordenes.txt'
cd /mnt/c && py.exe - "$PORT" "$SEG" "$RESET" "${SALIDA:-C:/Users/Public/reloj_consola.log}" "$ORDENES" <<'PY'
import sys, time, serial, codecs, os
port, seg, reset, salida, ordenes = sys.argv[1], float(sys.argv[2]), sys.argv[3] == "--reset", sys.argv[4], sys.argv[5]
dec = codecs.getincrementaldecoder("utf-8")("replace")
open(ordenes, "w").close()                       # empieza vacío: solo cuentan las órdenes nuevas
pos = 0
f = open(salida, "a", encoding="utf-8")
f.write(f"PID_WINDOWS={os.getpid()}\n"); f.flush()
with serial.Serial(port, 115200, timeout=0.05) as s:
    if reset:
        s.dtr = False; s.rts = True; time.sleep(0.1); s.rts = False
    fin = time.time() + seg
    while time.time() < fin:
        d = s.read(4096)
        if d:
            f.write(dec.decode(d)); f.flush()
        try:
            with open(ordenes, "r", encoding="utf-8") as o:
                o.seek(pos); nuevo = o.read(); pos = o.tell()
        except OSError:
            nuevo = ""
        for linea in nuevo.splitlines():
            if linea.strip():
                s.write((linea.strip() + "\n").encode()); s.flush()
                f.write(f"\n>>> {linea.strip()}\n"); f.flush()
f.close()
PY

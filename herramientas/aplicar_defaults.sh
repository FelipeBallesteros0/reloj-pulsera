#!/usr/bin/env bash
# Copia al sdkconfig local las opciones CONFIG_X=y indicadas (sin borrar el sdkconfig, que guarda las claves WiFi).
# Ejecutar dos veces: la segunda recoge las opciones que solo existen cuando otra (p. ej. CONFIG_PM_ENABLE) ya está activa.
# Uso: herramientas/aplicar_defaults.sh CONFIG_PM_ENABLE CONFIG_FREERTOS_USE_TICKLESS_IDLE ...
set -e
cd "$(dirname "$0")/../firmware"
for k in "$@"; do
    if grep -q "^# $k is not set" sdkconfig; then sed -i "s/^# $k is not set/$k=y/" sdkconfig
    elif ! grep -q "^$k=" sdkconfig; then echo "$k=y" >> sdkconfig
    fi
done
. ~/esp/esp-idf/export.sh > /dev/null
idf.py reconfigure > /dev/null
for k in "$@"; do grep -H "^$k=" sdkconfig || echo "FALTA $k"; done

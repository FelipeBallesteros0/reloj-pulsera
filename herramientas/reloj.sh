#!/usr/bin/env bash
# Manda una orden al reloj a través de control_win.sh (que debe estar corriendo). Ej.: herramientas/reloj.sh "tap 205 251"
printf '%s\n' "$*" >> /mnt/c/Users/Public/reloj_ordenes.txt

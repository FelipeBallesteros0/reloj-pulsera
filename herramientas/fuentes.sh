#!/usr/bin/env bash
# Genera las fuentes LVGL con acentos (rango ASCII + Latin-1) a partir de la Montserrat que trae el componente LVGL.
# Uso: herramientas/fuentes.sh   (requiere node/npx; la primera vez descarga lv_font_conv)
set -euo pipefail
R="$(cd "$(dirname "$0")/.." && pwd)"
TTF="$R/firmware/managed_components/lvgl__lvgl/scripts/built_in_font/Montserrat-Medium.ttf"
[ -f "$TTF" ] || { echo "No está $TTF: compila una vez (idf.py build) para que baje LVGL" >&2; exit 1; }
OUT="$R/firmware/components/reloj_ui/fuentes"
mkdir -p "$OUT"
# Latin-1 completo (0xA0-0xFF) cubre á é í ó ú ñ ü ¿ ¡ º ª; 0x2022 (•) y 0x2026 (…) para las listas.
RANGO="0x20-0x7F,0xA0-0xFF,0x2022,0x2026"
for T in 16 20 24; do
  npx --yes lv_font_conv@1.5.3 --no-compress --font "$TTF" -r "$RANGO" --size $T --bpp 4 --format lvgl \
      --lv-include lvgl.h -o "$OUT/fuente_$T.c" --lv-font-name fuente_$T
done
# Dígitos grandes para la carátula: solo lo necesario (dígitos, dos puntos, espacio).
npx --yes lv_font_conv@1.5.3 --no-compress --font "$TTF" -r "0x20,0x30-0x3A" --size 72 --bpp 4 --format lvgl \
    --lv-include lvgl.h -o "$OUT/fuente_72.c" --lv-font-name fuente_72
# Emojis monocromos (Noto Emoji, OFL) como fuente de respaldo de las de texto: rangos de símbolos y pictogramas.
EMOJI="$R/firmware/fuentes_src/NotoEmoji.ttf"
if [ -f "$EMOJI" ]; then
  npx --yes lv_font_conv@1.5.3 --no-compress --font "$EMOJI" \
      -r "0x2600-0x27BF,0x2B50,0x2B55,0x1F300-0x1F64F,0x1F680-0x1F6FF,0x1F90C-0x1F9FF,0x1FA70-0x1FAFF" \
      --size 20 --bpp 4 --format lvgl --lv-include lvgl.h -o "$OUT/emoji_20.c" --lv-font-name emoji_20
else
  echo "Aviso: falta $EMOJI (NotoEmoji[wght].ttf de google/fonts); no se regenera emoji_20.c" >&2
fi
ls -l "$OUT"

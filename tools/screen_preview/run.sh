#!/bin/sh
# Builds the screen preview tool on a PC and renders sample screens to PNG.
# Needs a firmware build first (pio run -e smalltv) for the libraries in
# .pio/libdeps, plus Python with Pillow for the PNG step.
#   sh tools/screen_preview/run.sh [OUTDIR]
set -eu
cd "$(dirname "$0")/../.."
OUT=${1:-.pio/preview}
LIB=.pio/libdeps/smalltv
GFX="$LIB/GFX Library for Arduino/src"
U8G2="$LIB/U8g2/src"
BIN=.pio/preview-tool
mkdir -p "$OUT" .pio/preview-obj
# The font file is large; compile it once.
[ -f .pio/preview-obj/u8g2_fonts.o ] || cc -c -O1 "$U8G2/clib/u8g2_fonts.c" -I"$U8G2/clib" -o .pio/preview-obj/u8g2_fonts.o
c++ -std=c++17 -O1 -w -DGFX_SKIP_OUTPUT_BEGIN \
  -Itools/screen_preview/shim -I"$GFX" -I"$U8G2" -Isrc/features/sysmon \
  tools/screen_preview/preview.cpp "$GFX/Arduino_GFX.cpp" "$GFX/Arduino_G.cpp" \
  src/features/sysmon/SysmonView.cpp src/features/sysmon/SysmonData.cpp \
  .pio/preview-obj/u8g2_fonts.o -o "$BIN"
"$BIN" sysmon "$OUT/sysmon.ppm" 42 68 65 40 8 100
"$BIN" sysmon "$OUT/sysmon-busy.ppm" 97 91 100 940 120 1100
"$BIN" sysmon "$OUT/sysmon-idle.ppm" 3 24 7 0.2 0.1 10
"$BIN" sysmon-waiting "$OUT/sysmon-waiting.ppm" 75
python3 - "$OUT" <<'PY'
import sys, glob
from PIL import Image
for p in glob.glob(sys.argv[1] + "/*.ppm"):
    Image.open(p).save(p[:-4] + ".png")
PY
echo "previews in $OUT"

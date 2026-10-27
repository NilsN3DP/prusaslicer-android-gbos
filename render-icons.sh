#!/bin/bash
# Rasterises PrusaSlicer's left bar SVG icons for the Android side bar (runs in WSL).
set -e
command -v rsvg-convert >/dev/null || apt-get install -y -qq librsvg2-bin >/dev/null
SRC="/mnt/z/Prusa Slicer Googlebook OS/src/PrusaSlicer-version_3.0.0-alpha12/resources/icons"
OUT="/mnt/z/Prusa Slicer Googlebook OS/android-native/app-project/app/src/main/res/drawable-xxhdpi"
mkdir -p "$OUT"
for pair in lb_slicing:ic_lb_slicing lb_printers:ic_lb_connect lb_printables:ic_lb_printables cog:ic_lb_preferences user:ic_lb_account; do
  svg=${pair%%:*}; png=${pair##*:}
  rsvg-convert -w 72 -h 72 "$SRC/$svg.svg" -o "$OUT/$png.png"
done
ls -la "$OUT"

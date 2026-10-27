#!/bin/bash
# Copies SDL's Java glue (org.libsdl.app) matching the built SDL version into the app project.
set -e
d=$(find /work/PrusaSlicer-android/deps/build -type d -path '*android-project/app/src/main/java/org/libsdl/app' | head -1)
[ -n "$d" ] || { echo "SDL android-project nicht gefunden"; find /work/PrusaSlicer-android/deps/build -maxdepth 3 -iname '*sdl*' | head; exit 1; }
t="/mnt/z/Prusa Slicer Googlebook OS/android-native/app-project/app/src/main/java/org/libsdl/app"
mkdir -p "$t"
cp "$d"/*.java "$t/"
ls "$t"
ls /work/PrusaSlicer-android/deps/build/destdir/usr/local/lib | grep -i sdl

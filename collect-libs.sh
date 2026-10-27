#!/bin/bash
# Copies the native libraries and PrusaSlicer's resources into the Gradle project (runs in WSL).
set -euo pipefail
BASE="/mnt/z/Prusa Slicer Googlebook OS/android-native"
SRC=/work/PrusaSlicer-android
NDK=/opt/android-ndk-r27c
STRIP=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip
OUT="$BASE/app-project/jniLibs/x86_64"

mkdir -p "$OUT"
cp "$(find $SRC/build-android -name libmain.so | head -1)" "$OUT/libmain.so"
cp "$SRC/deps/build/destdir/usr/local/lib/libSDL2.so" "$OUT/libSDL2.so"
cp "$NDK/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/x86_64-linux-android/libc++_shared.so" "$OUT/"
$STRIP --strip-unneeded "$OUT"/*.so
ls -la "$OUT"

# resources -> assets/resources (test data is only used by unit tests)
mkdir -p "$BASE/app-project/assets"
rsync -a --delete --exclude test_data "$SRC/resources/" "$BASE/app-project/assets/resources/"
du -sh "$BASE/app-project/assets/resources"

# upstream's ES shaders mix GLSL ES 1.00 and 3.00; unify them for Android
python3 "$BASE/fix-es-shaders.py" "$BASE/app-project/assets/resources/shaders/ES"

# ANGLE (Googlebook) rejects ES fragment shaders without a default float precision
grep -rL "precision " --include=*.fs "$BASE/app-project/assets/resources/shaders/ES" | while IFS= read -r f; do
  grep -q "^#version 300 es" "$f" && sed -i '0,/^#version 300 es/s//#version 300 es\nprecision highp float;\nprecision highp int;/' "$f" || true
done
echo "precision fix applied"

# Wireframe: geometry shaders need GLSL ES 3.20, and all stages of a program must share the version
W="$BASE/app-project/assets/resources/shaders/ES/gouraud_light_wireframe"
sed -i -e 's/^#version 100$/#version 320 es/' -e 's/^varying vec2 vertex_intensity\[\];/in vec2 vertex_intensity[];/' \
       -e 's/^varying vec2 intensity;/out vec2 intensity;/' -e 's/^noperspective varying vec3 edge_distance;/out vec3 edge_distance;/' "$W.gs"
grep -q "^precision" "$W.gs" || sed -i 's/^#version 320 es$/#version 320 es\nprecision highp float;/' "$W.gs"
sed -i 's/^#version 300 es$/#version 320 es/' "$W.vs" "$W.fs"
echo "wireframe shader: GLSL ES 3.20"

#!/bin/bash
# Cross-compiles PrusaSlicer (libmain.so) for Android x86_64. Runs as root in WSL "ps-build", after build-deps.sh.
set -euxo pipefail
exec 9>/tmp/ps-android-deps.lock
flock 9
exec >> "/mnt/z/Prusa Slicer Googlebook OS/android-native/app.log" 2>&1
echo "======== neuer Lauf $(date +%T) ========"
export LC_ALL=C.UTF-8
BASE="/mnt/z/Prusa Slicer Googlebook OS"
SRC=/work/PrusaSlicer-android
PORT=/work/android-port

bash "$BASE/android-native/apply-patches.sh" "$SRC"
# the project path contains spaces, which add_subdirectory does not survive
mkdir -p "$PORT"
rsync -a --delete "$BASE/android-native/slic3r-platform-sdl" "$BASE/android-native/slic3r-app-android" \
  "$BASE/android-native/slic3r-shared-android" "$PORT/"

cd "$SRC"
if [ -f build-android/CMakeCache.txt ]; then cmake build-android; fi
[ -f build-android/CMakeCache.txt ] || cmake -S . -B build-android -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$BASE/android-native/android-x86_64.toolchain.cmake" \
  -DCMAKE_PREFIX_PATH="$SRC/deps/build/destdir/usr/local" \
  -DSLIC3R_ANDROID_PORT_DIR="$PORT" \
  -DSLIC3R_STATIC=ON -DSLIC3R_PCH=OFF -DSLIC3R_RELEASE_DEBUG_SYMBOLS=OFF \
  || { echo "EXIT=1"; exit 1; }
# -k 0: report every compile error of a run, not just the first
cmake --build build-android --target main -- -k 0 || { echo "EXIT=1"; exit 1; }
echo APP_DONE; echo "EXIT=0"

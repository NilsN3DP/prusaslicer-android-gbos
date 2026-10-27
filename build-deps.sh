#!/bin/bash
# Cross-compiles PrusaSlicer's dependencies for Android x86_64 with the NDK. Runs as root in WSL "ps-build".
set -euxo pipefail
# only one build at a time (wsl.exe exiting does not stop a running ninja): wait for it,
# then log ourselves so a waiting run never truncates the log of the running one
exec 9>/tmp/ps-android-deps.lock
flock 9
exec >> "/mnt/z/Prusa Slicer Googlebook OS/android-native/build.log" 2>&1
echo "======== neuer Lauf $(date +%T) ========"
export LC_ALL=C.UTF-8
BASE="/mnt/z/Prusa Slicer Googlebook OS"
SRC=/work/PrusaSlicer-android
NDK=/opt/android-ndk-r27c
TC="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
API=29

[ -f "$SRC/CMakeLists.txt" ] || rsync -a "$BASE/src/PrusaSlicer-version_3.0.0-alpha12/" "$SRC/"
cd "$SRC"
bash "$BASE/android-native/apply-patches.sh" "$SRC"

# autotools deps (GMP, MPFR, OpenSSL) pick the compiler from the environment
export CC="$TC/x86_64-linux-android$API-clang" CXX="$TC/x86_64-linux-android$API-clang++"
export AR="$TC/llvm-ar" RANLIB="$TC/llvm-ranlib" STRIP="$TC/llvm-strip" NM="$TC/llvm-nm" LD="$TC/ld.lld"
export ANDROID_NDK_ROOT="$NDK" ANDROID_NDK_HOME="$NDK"
export PATH="$TC:$PATH"

# re-read recipes on every run (apply-patches may have changed them)
if [ -f deps/build/CMakeCache.txt ]; then cmake deps/build; fi
[ -f deps/build/CMakeCache.txt ] || cmake -S deps -B deps/build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$BASE/android-native/android-x86_64.toolchain.cmake" \
  "-DPrusaSlicer_deps_PACKAGE_EXCLUDES=wxWidgets|sentry|GLFW|GLEW|OpenCSG|Catch2|Trumpeloeil"
cmake --build deps/build -j"$(nproc)" || { echo "EXIT=1"; exit 1; }
echo DEPS_DONE; echo "EXIT=0"

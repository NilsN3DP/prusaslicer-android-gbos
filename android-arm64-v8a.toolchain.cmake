# Android arm64 (phones, tablets, ARM Chromebooks) cross toolchain for PrusaSlicer and its deps.
set(ANDROID_ABI arm64-v8a)
set(ANDROID_PLATFORM android-29)
set(ANDROID_STL c++_shared)
include(/opt/android-ndk-r27c/build/cmake/android.toolchain.cmake)

# Used by deps/+GMP, +MPFR, +OpenSSL for autotools --host
set(TOOLCHAIN_PREFIX aarch64-linux-android)

# Deps install into their own prefix, which must stay searchable despite the NDK sysroot.
# ONLY: never pick up headers / libraries of the build host (paths already under a root are not re-rooted).
list(APPEND CMAKE_FIND_ROOT_PATH /work/PrusaSlicer-android/deps/build-arm64-v8a/destdir/usr/local)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

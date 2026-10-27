#!/bin/bash
# Resets one dep (after recipe patches) and regenerates the deps project: regen.sh <Dep>
set -e
B=/work/PrusaSlicer-android/deps/build
rm -rf $B/dep_$1-prefix/src/dep_$1-stamp $B/dep_$1-prefix/src/dep_$1
cmake $B >/dev/null 2>&1
echo "reset $1, CXXFLAGS mit gnu11: $(grep -c 'CXXFLAGS=-std=gnu11' $B/build.ninja)"

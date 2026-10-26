#!/bin/sh
# Configure and build the AmigaOS target. tools/amiga/build.sh runs this
# natively or inside the derived image (REPO=/src, PISTORM3D=/pistorm3d).
set -eu

: "${REPO:?REPO is not set}"
: "${PISTORM3D:?PISTORM3D is not set}"
: "${CPU:=68040}"
: "${FPU:=68881}"
: "${OPT:=-O2}"
: "${STACK:=2097152}"
: "${JOBS:=4}"

BUILD_DIR="$REPO/build/amiga"

# CMake caches absolute paths. The wrapper mounts the repository at /src inside
# Docker and uses the host path natively, so a tree built in the other mode
# cannot be reused; drop it instead of failing with a stale-cache error.
if [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
    cached=$(sed -n 's/^CMAKE_CACHEFILE_DIR:INTERNAL=//p' "$BUILD_DIR/CMakeCache.txt")
    if [ -n "$cached" ] && [ "$cached" != "$BUILD_DIR" ]; then
        printf 'cmake-build: %s was configured for %s; removing it\n' "$BUILD_DIR" "$cached" >&2
        rm -rf "$BUILD_DIR"
    fi
fi

cmake -S "$REPO" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$REPO/cmake/amigaos.toolchain.cmake" \
    -DAMIGAOS=ON \
    -DPISTORM3D="$PISTORM3D" \
    -DOPENCMR2_AMIGA_CPU="$CPU" \
    -DOPENCMR2_AMIGA_FPU="$FPU" \
    -DOPENCMR2_AMIGA_OPT="$OPT" \
    -DOPENCMR2_AMIGA_STACK="$STACK" \
    -DCMAKE_BUILD_TYPE=

cmake --build "$BUILD_DIR" --parallel "$JOBS"

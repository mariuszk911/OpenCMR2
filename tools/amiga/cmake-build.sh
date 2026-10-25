#!/bin/sh
# Configure and build the AmigaOS target. tools/amiga/build.sh runs this
# natively or inside the derived image (REPO=/src, PISTORM3D=/pistorm3d).
set -eu

: "${REPO:?REPO is not set}"
: "${PISTORM3D:?PISTORM3D is not set}"
: "${CPU:=68040}"
: "${FPU:=68881}"
: "${OPT:=-O2}"
: "${STACK:=262144}"
: "${JOBS:=4}"

cmake -S "$REPO" -B "$REPO/build/amiga" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$REPO/cmake/amigaos.toolchain.cmake" \
    -DAMIGAOS=ON \
    -DPISTORM3D="$PISTORM3D" \
    -DOPENCMR2_AMIGA_CPU="$CPU" \
    -DOPENCMR2_AMIGA_FPU="$FPU" \
    -DOPENCMR2_AMIGA_OPT="$OPT" \
    -DOPENCMR2_AMIGA_STACK="$STACK" \
    -DCMAKE_BUILD_TYPE=

cmake --build "$REPO/build/amiga" --parallel "$JOBS"

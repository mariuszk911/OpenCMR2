#!/bin/sh
# OpenCMR2 WarpOS (PowerPC Amiga) build wrapper.
#
# Native only: ppc-amigaos-gcc (adtools, GCC 11) with the MOS2WOS SDK and the
# PPC MiniGL tree (API 29, dispatch ABI 5). Configures CMake with
# cmake/warpos.toolchain.cmake in build/warpos, which also runs the WarpOS
# contract check and the conversion to the hunk executable, and stages
# dist/warpos/.
#
# Do not invoke the cross compiler for a project build; use this script.
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
REPO=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd -P)

WOS_TOOLCHAIN_BIN=${WOS_TOOLCHAIN_BIN:-/opt/ppc-amigaos/bin}
MOS2WOS_ROOT=${MOS2WOS_ROOT:-"$HOME/amiga/toolchains/mos2wos/gg"}
WARPOS_MINIGL=${WARPOS_MINIGL:-"$HOME/amiga/R200_MiniGL_PPC"}
OPT=${OPT:--O2}
STACK=${STACK:-2097152}
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}
CPU=603e
BUILD="$REPO/build/warpos"
DIST="$REPO/dist/warpos"

usage() {
    cat <<'EOF'
usage: tools/warpos/build.sh [--g4] [--clean] [--info]

  --g4      the G4 variant: -mcpu=7400 (no compiler-generated AltiVec), in
            build/warpos-g4 and dist/warpos-g4; runs on 7400/7410 boards only
  --clean   remove the build and dist directories before configuring
  --info    print the toolchain, SDK, MiniGL tree and flags, then stop

Environment overrides:
  WOS_TOOLCHAIN_BIN  ppc-amigaos-gcc binaries (default: /opt/ppc-amigaos/bin)
  MOS2WOS_ROOT       MOS2WOS SDK root, ppc-warpos/ inside (default: ~/amiga/toolchains/mos2wos/gg)
  WARPOS_MINIGL      PPC MiniGL tree, include/ and lib/libminigl.a (default: ~/amiga/R200_MiniGL_PPC)
  OPT                optimisation flags (default: -O2)
  STACK              stack the converter's stub swaps in, bytes (default: 2097152)
  JOBS               parallel build jobs (default: host CPU count)
EOF
}

fail() {
    printf 'build.sh: %s\n' "$*" >&2
    exit 1
}

clean=0
info=0
for arg in "$@"; do
    case "$arg" in
        --g4) CPU=7400; BUILD="$REPO/build/warpos-g4"; DIST="$REPO/dist/warpos-g4" ;;
        --clean) clean=1 ;;
        --info) info=1 ;;
        -h|--help) usage; exit 0 ;;
        *) usage >&2; exit 2 ;;
    esac
done

CC="$WOS_TOOLCHAIN_BIN/ppc-amigaos-gcc"
[ -x "$CC" ] || fail "missing $CC (set WOS_TOOLCHAIN_BIN)"
[ -f "$MOS2WOS_ROOT/ppc-warpos/lib/startupwos.o" ] || fail "missing MOS2WOS SDK under $MOS2WOS_ROOT (set MOS2WOS_ROOT)"
[ -f "$WARPOS_MINIGL/lib/libminigl.a" ] || fail "missing $WARPOS_MINIGL/lib/libminigl.a (set WARPOS_MINIGL)"
command -v cmake >/dev/null 2>&1 || fail "cmake not found"
command -v ninja >/dev/null 2>&1 || fail "ninja not found"
command -v python3 >/dev/null 2>&1 || fail "python3 not found (elf2warpos)"

if [ "$info" -eq 1 ]; then
    printf 'repo:       %s\n' "$REPO"
    printf 'compiler:   %s\n' "$("$CC" --version | head -n 1)"
    printf 'sdk:        %s/ppc-warpos\n' "$MOS2WOS_ROOT"
    printf 'minigl:     %s (%s)\n' "$WARPOS_MINIGL" "$(git -C "$WARPOS_MINIGL" log --oneline -1 2>/dev/null || echo 'not a git tree')"
    printf 'cmake:      %s\n' "$(cmake --version | head -n 1)"
    printf 'opt/stack:  %s / %s\n' "$OPT" "$STACK"
    printf 'build/dist: %s / %s\n' "$BUILD" "$DIST"
    printf 'flags:      see cmake/warpos.toolchain.cmake (-mcpu=%s -mtune=7400, ET_REL link)\n' "$CPU"
    exit 0
fi

if [ "$clean" -eq 1 ]; then
    rm -rf "$BUILD" "$DIST"
fi

cmake -S "$REPO" -B "$BUILD" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$REPO/cmake/warpos.toolchain.cmake" \
    -DCMAKE_BUILD_TYPE= \
    -DWOS_TOOLCHAIN_BIN="$WOS_TOOLCHAIN_BIN" \
    -DMOS2WOS_ROOT="$MOS2WOS_ROOT" \
    -DWARPOS_MINIGL="$WARPOS_MINIGL" \
    -DOPENCMR2_WARPOS_OPT="$OPT" \
    -DOPENCMR2_WARPOS_CPU="$CPU" \
    -DWARPOS_STACK="$STACK"
cmake --build "$BUILD" -j "$JOBS"

mkdir -p "$DIST"
cp "$BUILD/opencmr2" "$DIST/opencmr2"
cp "$SCRIPT_DIR/OpenCMR2-WOS" "$DIST/OpenCMR2-WOS"
cp "$REPO/LICENSE" "$DIST/LICENSE"
printf 'build.sh: staged %s\n' "$DIST"

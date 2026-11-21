#!/bin/sh
# OpenCMR2 AmigaOS 68k build wrapper.
#
# Picks a native m68k-amigaos-gcc, Docker, or Docker through flatpak-spawn (the
# VS Code Flatpak sandbox), validates the pinned PiStorm3D 29.1 SDK, configures
# CMake with cmake/amigaos.toolchain.cmake and stages dist/amiga/.
#
# Do not invoke the cross compiler for a project build; use this script.
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
REPO=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd -P)

IMAGE=${IMAGE:-opencmr2/amiga-toolchain:latest}
CPU=${CPU:-68040}
FPU=${FPU:-68881}
OPT=${OPT:--O2}
STACK=${STACK:-2097152}
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}
MINIGL_TIMING=${MINIGL_TIMING:-OFF}
PISTORM3D_ROOT=${PISTORM3D:-"$REPO/SDKs/PiStorm3D"}
OGLES2_ROOT=${OGLES2_SDK:-"$REPO/SDKs/ogles2-sdk-1.3.3"}

usage() {
    cat <<'EOF'
usage: tools/amiga/build.sh [--clean] [--info] [--probe] [--layout]

  --clean   remove build/amiga, dist/amiga and dist/amiga-tests before configuring
  --info    print the resolved mode, image, compiler, CMake, SDK and flags
  --probe   run the M0.4 compiler/toolchain probes instead of building
  --layout  run the M0.5 layout probe instead of building

Environment overrides:
  PISTORM3D  PiStorm3D SDK root (default: SDKs/PiStorm3D)
  OGLES2_SDK ogles2 SDK 1.3.3 root (default: SDKs/ogles2-sdk-1.3.3)
  IMAGE      Docker image (default: opencmr2/amiga-toolchain:latest)
  CPU        Amiga CPU for -m (default: 68040)
  FPU        Amiga FPU for -m (default: 68881)
  OPT        optimisation flags (default: -O2)
  STACK      swapstack size in bytes (default: 2097152)
  JOBS       parallel build jobs (default: host CPU count)
  MINIGL_TIMING  ON adds MiniGL per-section clock calls for a Task 0.1
                 timing run (default: OFF; diagnostic builds only)
EOF
}

fail() {
    printf 'build.sh: %s\n' "$*" >&2
    exit 1
}

clean=0
info=0
probe=0
layout=0
for arg in "$@"; do
    case "$arg" in
        --clean) clean=1 ;;
        --info) info=1 ;;
        --probe) probe=1 ;;
        --layout) layout=1 ;;
        --help|-h) usage; exit 0 ;;
        *) printf 'build.sh: unknown option: %s\n' "$arg" >&2; usage >&2; exit 2 ;;
    esac
done

# ---- SDK pin (29.1 client SDK) -----------------------------------------------------
[ -d "$PISTORM3D_ROOT" ] || fail "PiStorm3D SDK not found: $PISTORM3D_ROOT (set PISTORM3D)"
case "$PISTORM3D_ROOT" in
    /*) ;;
    *) PISTORM3D_ROOT=$(CDPATH= cd -- "$PISTORM3D_ROOT" && pwd -P) ;;
esac

[ -d "$OGLES2_ROOT" ] || fail "ogles2 SDK not found: $OGLES2_ROOT (set OGLES2_SDK)"
case "$OGLES2_ROOT" in
    /*) ;;
    *) OGLES2_ROOT=$(CDPATH= cd -- "$OGLES2_ROOT" && pwd -P) ;;
esac
[ -f "$OGLES2_ROOT/include/proto/ogles2.h" ] || fail "missing $OGLES2_ROOT/include/proto/ogles2.h"

SDK="$PISTORM3D_ROOT/SDK"
[ -f "$SDK/lib/libminigl.a" ] || fail "missing $SDK/lib/libminigl.a"
[ -f "$SDK/minigl-shared-library/include/proto/minigl.h" ] \
    || fail "missing MiniGL headers under $SDK/minigl-shared-library/include"
[ -f "$PISTORM3D_ROOT/bin/minigl.library" ] || fail "missing $PISTORM3D_ROOT/bin/minigl.library"
head -n 1 "$SDK/README.txt" 2>/dev/null | grep -q '29\.1' \
    || fail "SDK README is not 29.1: $SDK/README.txt"
# The client SDK is the pin (its README above, and sizeof(MGLDispatchTable) in
# minigl_abi_check.cpp). The library beside it may be any release with the same
# dispatch ABI: 29.1 or newer (PiStorm3D 30.0 ships the 29.1 client SDK).
lib_ver=$(grep -ao '\$VER: minigl.library [0-9]*\.[0-9]*' "$PISTORM3D_ROOT/bin/minigl.library" \
    | head -n 1 | sed 's/.* //')
lib_major=${lib_ver%%.*}
lib_minor=${lib_ver#*.}
[ -n "$lib_ver" ] && { [ "$lib_major" -gt 29 ] || { [ "$lib_major" -eq 29 ] && [ "$lib_minor" -ge 1 ]; }; } \
    || fail "$PISTORM3D_ROOT/bin/minigl.library is not 29.1 or newer (found '${lib_ver:-none}')"

# ---- runner selection -------------------------------------------------------
DOCKER_VIA_FLATPAK=0
if command -v m68k-amigaos-gcc >/dev/null 2>&1; then
    MODE=native
elif command -v docker >/dev/null 2>&1; then
    MODE=docker
elif command -v flatpak-spawn >/dev/null 2>&1 \
        && flatpak-spawn --host sh -c 'command -v docker >/dev/null 2>&1'; then
    MODE=docker
    DOCKER_VIA_FLATPAK=1
else
    fail "no m68k-amigaos-gcc in PATH and no Docker (direct or via flatpak-spawn)"
fi

docker_cmd() {
    if [ "$DOCKER_VIA_FLATPAK" = 1 ]; then
        flatpak-spawn --host docker "$@"
    else
        docker "$@"
    fi
}

if [ "$MODE" = native ]; then
    command -v cmake >/dev/null 2>&1 || fail "cmake is not in PATH"
    command -v ninja >/dev/null 2>&1 || fail "ninja is not in PATH"
    cmake_version=$(cmake --version | sed -n '1s/^cmake version //p')
    [ -n "$cmake_version" ] || fail "could not determine the CMake version"
    cmake_major=${cmake_version%%.*}
    cmake_rest=${cmake_version#*.}
    cmake_minor=${cmake_rest%%.*}
    if [ "$cmake_major" -lt 3 ] || { [ "$cmake_major" -eq 3 ] && [ "$cmake_minor" -lt 24 ]; }; then
        fail "CMake 3.24 or newer is required (found $cmake_version)"
    fi
fi

if [ "$MODE" = docker ] && ! docker_cmd image inspect "$IMAGE" >/dev/null 2>&1; then
    printf 'build.sh: building %s\n' "$IMAGE" >&2
    docker_cmd build -t "$IMAGE" -f "$REPO/tools/amiga/Dockerfile" "$REPO/tools/amiga"
fi

if [ "$info" = 1 ]; then
    if [ "$MODE" = native ]; then
        mode_line="native"
        compiler=$(m68k-amigaos-gcc --version | head -n 1)
        cmake_line=$(cmake --version | head -n 1)
    else
        mode_line="docker ($IMAGE)"
        [ "$DOCKER_VIA_FLATPAK" = 1 ] && mode_line="docker via flatpak-spawn ($IMAGE)"
        compiler=$(docker_cmd run --rm "$IMAGE" m68k-amigaos-gcc --version | head -n 1)
        cmake_line=$(docker_cmd run --rm "$IMAGE" cmake --version | head -n 1)
    fi
    printf 'repo:       %s\n' "$REPO"
    printf 'mode:       %s\n' "$mode_line"
    printf 'sdk:        %s\n' "$PISTORM3D_ROOT"
    printf 'sdk pin:    29.1 (README, minigl.library and MGLDispatchTable check)\n'
    printf 'compiler:   %s\n' "$compiler"
    printf 'cmake:      %s\n' "$cmake_line"
    printf 'generator:  Ninja\n'
    printf 'cpu:        %s\n' "$CPU"
    printf 'fpu:        %s\n' "$FPU"
    printf 'opt:        %s\n' "$OPT"
    printf 'stack:      %s\n' "$STACK"
    printf 'jobs:       %s\n' "$JOBS"
    printf 'build:      %s\n' "$REPO/build/amiga"
    printf 'dist:       %s\n' "$REPO/dist/amiga"
    printf 'flags:      -m%s -m%s -mhard-float -mcrt=nix20 -fno-omit-frame-pointer -fno-ivopts\n' "$CPU" "$FPU"
    printf '            C: -fexcess-precision=standard; C++: -ffloat-store\n'
    printf '            -fno-builtin-sin -fno-builtin-cos %s\n' "$OPT"
    exit 0
fi

if [ "$clean" = 1 ]; then
    rm -rf "$REPO/build/amiga" "$REPO/dist/amiga" "$REPO/dist/amiga-tests"
fi

fix_ownership() {
    for dir in "$REPO/build/amiga" "$REPO/dist/amiga" "$REPO/dist/amiga-tests"; do
        [ -e "$dir" ] || continue
        if find "$dir" ! -user "$(id -u)" -print -quit 2>/dev/null | grep -q .; then
            case "$dir" in
                "$REPO"/*) rel=${dir#"$REPO"/} ;;
                *) rel=$dir ;;
            esac
            printf 'build.sh: fixing ownership under %s\n' "$dir" >&2
            docker_cmd run --rm --user 0:0 -v "$REPO":/src "$IMAGE" \
                chown -R "$(id -u):$(id -g)" "/src/$rel"
        fi
    done
}

if [ "$probe" = 1 ] || [ "$layout" = 1 ]; then
    tool=probe.sh
    if [ "$layout" = 1 ]; then
        tool=layout.sh
    fi
    if [ "$MODE" = native ]; then
        REPO="$REPO" PISTORM3D="$PISTORM3D_ROOT" OGLES2_SDK="$OGLES2_ROOT" CPU="$CPU" FPU="$FPU" OPT="$OPT" JOBS="$JOBS" \
            sh "$REPO/tools/amiga/$tool"
    else
        fix_ownership
        docker_cmd run --rm \
            --user "$(id -u):$(id -g)" \
            -e HOME=/tmp \
            -e REPO=/src \
            -e PISTORM3D=/pistorm3d \
            -e OGLES2_SDK=/ogles2 \
            -e CPU="$CPU" -e FPU="$FPU" -e OPT="$OPT" -e JOBS="$JOBS" \
            -v "$REPO":/src \
            -v "$PISTORM3D_ROOT":/pistorm3d:ro \
            -v "$OGLES2_ROOT":/ogles2:ro \
            -w /src \
            "$IMAGE" \
            "sh" "/src/tools/amiga/$tool"
    fi
    exit 0
fi

if [ "$MODE" = native ]; then
    REPO="$REPO" PISTORM3D="$PISTORM3D_ROOT" OGLES2_SDK="$OGLES2_ROOT" CPU="$CPU" FPU="$FPU" OPT="$OPT" STACK="$STACK" JOBS="$JOBS" \
        MINIGL_TIMING="$MINIGL_TIMING" \
        sh "$REPO/tools/amiga/cmake-build.sh"
else
    fix_ownership
    docker_cmd run --rm \
        --user "$(id -u):$(id -g)" \
        -e HOME=/tmp \
        -e REPO=/src \
        -e PISTORM3D=/pistorm3d \
        -e OGLES2_SDK=/ogles2 \
        -e CPU="$CPU" -e FPU="$FPU" -e OPT="$OPT" -e STACK="$STACK" -e JOBS="$JOBS" \
        -e MINIGL_TIMING="$MINIGL_TIMING" \
        -v "$REPO":/src \
        -v "$PISTORM3D_ROOT":/pistorm3d:ro \
        -v "$OGLES2_ROOT":/ogles2:ro \
        -w /src \
        "$IMAGE" \
        sh /src/tools/amiga/cmake-build.sh
fi

# ---- staging ----------------------------------------------------------------
# dist/amiga is the release drawer (tools/amiga/package.sh archives it as is);
# the diagnostics go to dist/amiga-tests, never next to the release files.
mkdir -p "$REPO/dist/amiga" "$REPO/dist/amiga-tests"
cp "$REPO/build/amiga/opencmr2" "$REPO/dist/amiga/opencmr2"
chmod 755 "$REPO/dist/amiga/opencmr2"
# Workbench icon (made by tools/amiga/make_info_icon.py).
cp "$REPO/res/amiga/opencmr2.info" "$REPO/dist/amiga/opencmr2.info"
chmod 644 "$REPO/dist/amiga/opencmr2.info"
rm -rf "$REPO/dist/amiga/tests"
# minigl.library is a user requirement, not part of the release; drop the copy
# older builds staged.
rm -rf "$REPO/dist/amiga/Libs" "$REPO/dist/amiga/License_MiniGL_Library.txt"
for binary in "$REPO"/build/amiga/amiga_smoke "$REPO"/build/amiga/amiga_*_test; do
    [ -f "$binary" ] || continue
    cp "$binary" "$REPO/dist/amiga-tests/"
    chmod 755 "$REPO/dist/amiga-tests/$(basename "$binary")"
done
# Compiled GLES2 shaders (ogles2 finds them in PROGDIR:shaders/ by the hash of
# the shader text; see tools/amiga/shaders/README). Missing binaries are
# compiled on the first run where LIBS:v3dglslc is installed.
if [ -d "$REPO/tools/amiga/shaders" ]; then
    mkdir -p "$REPO/dist/amiga/shaders"
    for shader in "$REPO"/tools/amiga/shaders/*.v3d?; do
        [ -f "$shader" ] || continue
        cp "$shader" "$REPO/dist/amiga/shaders/"
    done
fi
cp "$REPO/LICENSE" "$REPO/dist/amiga/LICENSE"
chmod 644 "$REPO/dist/amiga/LICENSE"
cp "$REPO/res/amiga/README" "$REPO/dist/amiga/README"
chmod 644 "$REPO/dist/amiga/README"

printf 'build.sh: staged %s\n' "$REPO/dist/amiga"

#!/bin/sh
# M0.5 layout probe. Run through tools/amiga/build.sh --layout; the wrapper
# provides REPO, CPU, FPU and OPT.
#
# Domain A includes the game headers and is compiled with -malign-int.
# Domain B includes only the shared port headers and is compiled without it.
# The host i386 reference is produced by the linux-x86 build:
#   build/linux-x86/tests/layout_probe        > build/amiga/host-layout-full.tsv
#   build/linux-x86/tests/layout_probe_shared > build/amiga/host-layout-shared.tsv
set -eu

: "${REPO:?REPO is not set}"
: "${CPU:=68040}"
: "${FPU:=68881}"
: "${OPT:=-O2}"

CC=${CC:-m68k-amigaos-gcc}
CXX=${CXX:-m68k-amigaos-g++}

WORK="$REPO/build/amiga/layout"
LOG="$REPO/build/amiga/m0-layout.log"
mkdir -p "$WORK"
: > "$LOG"

note() {
    printf '%s\n' "$*" >> "$LOG"
    printf '%s\n' "$*"
}

COMMON="-m$CPU -m$FPU -mhard-float -mcrt=nix20 -fno-omit-frame-pointer -fno-builtin-sin -fno-builtin-cos $OPT"
CXXFLAGS="$COMMON -std=gnu++14 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fwrapv -fno-strict-aliasing -fsigned-char -ffp-contract=off -ffloat-store"
GAME_INCLUDES="-I$REPO/game -I$REPO/src -I$REPO/src/port/compat -include $REPO/src/port/types.h -include $REPO/src/port/msvcrt.h"
SHARED_INCLUDES="-I$REPO/src -I$REPO/src/port/compat"

note "M0.5 layout probe"
note "compiler: $($CXX --version | head -n 1)"
note "domain A: game headers, -malign-int"
note "domain B: shared port headers, no -malign-int (port/align.h)"
note ""

$CXX $CXXFLAGS -malign-int $GAME_INCLUDES -S "$REPO/tests/amiga/layout_probe.cpp" \
    -o "$WORK/domain_a.s"
$CXX $CXXFLAGS -DOPENCMR2_LAYOUT_SHARED_ONLY $SHARED_INCLUDES -S \
    "$REPO/tests/amiga/layout_probe.cpp" -o "$WORK/domain_b.s"

python3 "$REPO/tests/amiga/layout_extract.py" "$WORK/domain_a.s" > "$WORK/domain_a.values"
python3 "$REPO/tests/amiga/layout_extract.py" "$WORK/domain_b.s" > "$WORK/domain_b.values"
note "domain A values: $WORK/domain_a.values"
note "domain B values: $WORK/domain_b.values"

# A runnable probe for the target; the extracted values above are the
# authoritative comparison, this binary is for M0.9/M1 target runs.
$CXX $CXXFLAGS -DOPENCMR2_LAYOUT_PRINT -malign-int $GAME_INCLUDES \
    "$REPO/tests/amiga/layout_probe.cpp" -o "$WORK/layout_probe_amiga"
note "runnable probe: $WORK/layout_probe_amiga"

HOST_FULL="$REPO/build/amiga/host-layout-full.tsv"
HOST_SHARED="$REPO/build/amiga/host-layout-shared.tsv"
if [ -f "$HOST_FULL" ]; then
    if python3 "$REPO/tests/amiga/layout_compare.py" \
            --reference "$HOST_FULL" --probe "$WORK/domain_a.values" \
            --label "host i386 vs Amiga Domain A" >> "$LOG" 2>&1; then
        note "compare: host i386 vs Domain A PASS"
    else
        note "compare: host i386 vs Domain A FAIL (see $LOG)"
        exit 1
    fi
else
    note "compare: host reference missing: $HOST_FULL"
fi

if [ -f "$HOST_FULL" ] && [ -f "$HOST_SHARED" ]; then
    if python3 "$REPO/tests/amiga/layout_compare.py" \
            --reference "$HOST_SHARED" --probe "$WORK/domain_b.values" \
            --label "host i386 shared vs Amiga Domain B" >> "$LOG" 2>&1; then
        note "compare: host i386 shared vs Domain B PASS"
    else
        note "compare: host i386 shared vs Domain B FAIL (see $LOG)"
        exit 1
    fi

    game_values=$(( $(wc -l < "$HOST_FULL") - $(wc -l < "$HOST_SHARED") ))
    tail -n +$((game_values + 1)) "$WORK/domain_a.values" > "$WORK/domain_a_shared.values"
    if python3 "$REPO/tests/amiga/layout_compare.py" \
            --reference "$HOST_SHARED" --probe "$WORK/domain_a_shared.values" \
            --label "Amiga Domain A shared vs Domain B" >> "$LOG" 2>&1; then
        note "compare: Domain A shared vs Domain B PASS"
    else
        note "compare: Domain A shared vs Domain B FAIL (see $LOG)"
        exit 1
    fi
fi

note ""
note "These values come from the cross-compiled object; the emitted probe is the"
note "target check. A PiStorm run is recorded in build/amiga/m0-layout-target.log."

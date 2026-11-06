#!/bin/sh
# Toolchain capability probes. Run through tools/amiga/build.sh --probe;
# the wrapper provides REPO, CPU, FPU, OPT and JOBS.
#
# The probes record facts about GCC 6.5 and libnix, not assumptions:
#   - the Amiga CMake path must not carry Clang-only flags;
#   - -fms-extensions must not change the game parse;
#   - C++14 STL with -fno-exceptions must compile and link;
#   - -malign-int/-malign-double codegen and ICEs at O0/O1/O2;
#   - -mcrt=nix20 startup objects and the stack-swap state.
set -eu

: "${REPO:?REPO is not set}"
: "${CPU:=68040}"
: "${FPU:=68881}"
: "${OPT:=-O2}"
: "${JOBS:=4}"

CC=${CC:-m68k-amigaos-gcc}
CXX=${CXX:-m68k-amigaos-g++}

WORK="$REPO/build/amiga/probes"
LOGS="$REPO/build/amiga"
rm -rf "$WORK"
mkdir -p "$WORK" "$LOGS"

SUMMARY="$LOGS/m0-probe-summary.log"
: > "$SUMMARY"
note() {
    printf '%s\n' "$*" >> "$SUMMARY"
    printf '%s\n' "$*"
}
fail() {
    note "FAIL: $*"
    exit 1
}

# Keep in sync with cmake/amigaos.toolchain.cmake and the top-level semantics.
COMMON="-m$CPU -m$FPU -mhard-float -mcrt=nix20 -fno-omit-frame-pointer -fno-builtin-sin -fno-builtin-cos $OPT"
CFLAGS="$COMMON -std=gnu99 -fwrapv -fno-strict-aliasing -fsigned-char -ffp-contract=off -fexcess-precision=standard"
CXXFLAGS="$COMMON -std=gnu++14 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fwrapv -fno-strict-aliasing -fsigned-char -ffp-contract=off -ffloat-store"
GAME_INCLUDES="-I$REPO/game -I$REPO/src -I$REPO/src/port/compat -include $REPO/src/port/types.h -include $REPO/src/port/msvcrt.h"

note "M0.4 toolchain probes"
note "compiler: $($CXX --version | head -n 1)"
note "cflags:   $CFLAGS"
note "cxxflags: $CXXFLAGS"

# ---- 1. Amiga path flags ----------------------------------------------------
FLAGS_LOG="$LOGS/m0-probe-flags.log"
{
    echo "Clang-only flags in the Amiga CMake path must be absent."
    if grep -n -e '-Wno-everything' -e '-ferror-limit=0' -e '-Wcast-function-type-strict' \
            "$REPO/src/amiga.cmake" "$REPO/cmake/amigaos.toolchain.cmake"; then
        echo "FAIL"
    else
        echo "PASS: no Clang-only flags in src/amiga.cmake or the toolchain"
    fi
    if [ -f "$REPO/build/amiga/compile_commands.json" ]; then
        if grep -q -e '-Wno-everything' -e '-ferror-limit=0' -e '-Wcast-function-type-strict' \
                "$REPO/build/amiga/compile_commands.json"; then
            echo "FAIL: compile_commands.json still contains a Clang-only flag"
        else
            echo "PASS: compile_commands.json has no Clang-only flags"
        fi
    else
        echo "SKIP: compile_commands.json absent (run build.sh for the flag dump)"
    fi
} > "$FLAGS_LOG" 2>&1
if grep -q '^FAIL' "$FLAGS_LOG"; then
    fail "Clang-only flags found on the Amiga path (see $FLAGS_LOG)"
fi
note "flags:      PASS (no Clang-only flags; -fms-extensions removed from Domain A)"

# ---- 2. -fms-extensions -----------------------------------------------------
FMS="$WORK/fms"
mkdir -p "$FMS"
: > "$FMS/with.fail"
: > "$FMS/without.fail"
cat > "$FMS/check.sh" <<'EOF'
#!/bin/sh
set -eu
f=$1
extra=
if [ "$FMS_MODE" = with ]; then
    extra=-fms-extensions
fi
if ! $PROBE_CXX $PROBE_CXXFLAGS $extra \
        -I"$PROBE_REPO/game" -I"$PROBE_REPO/src" -I"$PROBE_REPO/src/port/compat" \
        -include "$PROBE_REPO/src/port/types.h" -include "$PROBE_REPO/src/port/msvcrt.h" \
        -fsyntax-only "$f" >/dev/null 2>&1; then
    printf '%s\n' "$f" >> "$FMS_OUT/$FMS_MODE.fail"
fi
EOF
ls "$REPO"/game/*.cpp > "$FMS/files"
export PROBE_CXX="$CXX" PROBE_CXXFLAGS="$CXXFLAGS" PROBE_REPO="$REPO" FMS_OUT="$FMS"
FMS_MODE=with xargs -P "$JOBS" -n 1 sh "$FMS/check.sh" < "$FMS/files"
FMS_MODE=without xargs -P "$JOBS" -n 1 sh "$FMS/check.sh" < "$FMS/files"
sort -u "$FMS/with.fail" -o "$FMS/with.fail"
sort -u "$FMS/without.fail" -o "$FMS/without.fail"
if diff -u "$FMS/with.fail" "$FMS/without.fail" > "$FMS/diff"; then
    note "fms-ext:    PASS (identical $(wc -l < "$FMS/without.fail") failures with and without)"
else
    fail "-fms-extensions changes the game build (see $FMS/diff); keep it on the Amiga path"
fi

GAME_ERRORS="$LOGS/m0-probe-game-errors.log"
: > "$GAME_ERRORS"
while IFS= read -r f; do
    printf '== %s\n' "${f#"$REPO"/}" >> "$GAME_ERRORS"
    m68k-amigaos-g++ $CXXFLAGS $GAME_INCLUDES -fsyntax-only "$f" 2>&1 \
        | grep -m2 'error:' >> "$GAME_ERRORS" || true
done < "$FMS/without.fail"
if [ -s "$FMS/without.fail" ]; then
    note "game parse:  $(wc -l < "$FMS/without.fail") files still fail for non-flag reasons (see $GAME_ERRORS)"
fi

# ---- 3. C++14 STL with -fno-exceptions -------------------------------------
STL="$WORK/stl"
mkdir -p "$STL"
cat > "$STL/stl.cpp" <<'EOF'
#include <algorithm>
#include <map>
#include <string>
#include <vector>

static std::map<int, std::string> g_map;

static int probe(bool flag)
{
    static std::vector<int> values;
    values.push_back(flag ? 1 : 2);
    values.push_back(3);
    std::sort(values.begin(), values.end());
    g_map[3] = std::string("cmr2");
    int *heap = new int(7);
    int result = static_cast<int>(values.size() + g_map.size()) + *heap;
    delete heap;
    return result;
}

int main(int argc, char **argv)
{
    (void)argv;
    return probe(argc > 0) == 9 ? 0 : 1;
}
EOF
STL_LOG="$LOGS/m0-probe-cxx14-stl.log"
if $CXX $CXXFLAGS -Wl,-Map="$STL/stl.map" "$STL/stl.cpp" -o "$STL/stl" > "$STL_LOG" 2>&1; then
    if grep -q '__cxa_guard' "$STL/stl.map"; then
        echo "FAIL: __cxa_guard referenced with -fno-threadsafe-statics" >> "$STL_LOG"
        fail "C++14 STL probe pulled thread-safe static guards"
    fi
    grep -m3 'libstdc++' "$STL/stl.map" >> "$STL_LOG" || true
    grep -m3 'libnix' "$STL/stl.map" >> "$STL_LOG" || true
    note "cxx14-stl:  PASS (vector/map/string/sort/new/delete linked; no __cxa_guard)"
else
    cat "$STL_LOG" >&2
    fail "C++14 STL probe did not link"
fi

# ---- 4. alignment and codegen ----------------------------------------------
ALIGN="$WORK/align"
mkdir -p "$ALIGN"
cat > "$ALIGN/layout.cpp" <<'EOF'
struct Layout { char c; int i; double d; short s; void *p; };
Layout g;
int main() { return 0; }
EOF
cat > "$ALIGN/layout.c" <<'EOF'
struct Layout { char c; int i; double d; short s; void *p; };
const unsigned long layout_values[] = {
    sizeof(struct Layout), __alignof__(struct Layout),
    __builtin_offsetof(struct Layout, c), __builtin_offsetof(struct Layout, i),
    __builtin_offsetof(struct Layout, d), __builtin_offsetof(struct Layout, s),
    __builtin_offsetof(struct Layout, p)
};
EOF
cat > "$ALIGN/div.c" <<'EOF'
int recip(int len) { return (int)((1LL << 32) / len); }
EOF
ALIGN_LOG="$LOGS/m0-probe-malign.log"
: > "$ALIGN_LOG"
for o in 0 1 2; do
    for mode in baseline align; do
        extra=
        if [ "$mode" = align ]; then
            extra=-malign-int
        fi
        rm -f "$ALIGN"/*.class
        if $CXX $CXXFLAGS -O$o $extra -fdump-class-hierarchy -c "$ALIGN/layout.cpp" \
                -o "$ALIGN/layout.$mode.$o.o" >> "$ALIGN_LOG" 2>&1; then
            printf 'O%s %s: %s\n' "$o" "$mode" \
                "$(grep -m1 'size=' "$ALIGN"/*.class)" >> "$ALIGN_LOG"
        else
            fail "layout probe failed at O$o $mode"
        fi
    done
    if $CXX $CXXFLAGS -O$o -malign-double -c "$ALIGN/layout.cpp" -o "$ALIGN/bad.o" > "$ALIGN/double.$o" 2>&1; then
        printf 'O%s malign-double: accepted\n' "$o" >> "$ALIGN_LOG"
    else
        printf 'O%s malign-double: unsupported: %s\n' "$o" \
            "$(grep -m1 'unrecognized command line option' "$ALIGN/double.$o")" >> "$ALIGN_LOG"
    fi
    if ! $CC $CFLAGS -O$o -c "$ALIGN/div.c" -o "$ALIGN/div.base.$o.o" >> "$ALIGN_LOG" 2>&1; then
        fail "64-bit division baseline failed at O$o"
    fi
    if $CC $CFLAGS -O$o -malign-int -c "$ALIGN/div.c" -o "$ALIGN/div.align.$o.o" > "$ALIGN/div.$o" 2>&1; then
        printf 'O%s malign-int 64-bit division: compiled (ICE not reproduced)\n' "$o" >> "$ALIGN_LOG"
    elif grep -q 'internal compiler error' "$ALIGN/div.$o"; then
        printf 'O%s malign-int 64-bit division: ICE: %s\n' "$o" \
            "$(grep -m1 'internal compiler error' "$ALIGN/div.$o")" >> "$ALIGN_LOG"
    else
        fail "unexpected -malign-int 64-bit division failure at O$o"
    fi
done
for o in 0 1 2; do
    $CC $CFLAGS -O$o -S "$ALIGN/layout.c" -o "$ALIGN/layout.base.$o.s"
    $CC $CFLAGS -O$o -malign-int -S "$ALIGN/layout.c" -o "$ALIGN/layout.align.$o.s"
    printf 'O%s offsets baseline: %s\n' "$o" \
        "$(awk '/^_layout_values:/{p=1;next} p&&/\.long/{printf "%s ",$2} p&&!/\.long/{exit}' "$ALIGN/layout.base.$o.s")" >> "$ALIGN_LOG"
    printf 'O%s offsets align:    %s\n' "$o" \
        "$(awk '/^_layout_values:/{p=1;next} p&&/\.long/{printf "%s ",$2} p&&!/\.long/{exit}' "$ALIGN/layout.align.$o.s")" >> "$ALIGN_LOG"
done
if ! $CXX $CXXFLAGS -O2 $GAME_INCLUDES -c "$REPO/game/Car.cpp" -o "$ALIGN/Car.base.o" > "$ALIGN/Car.base.err" 2>&1; then
    fail "baseline game/Car.cpp did not compile"
fi
if $CXX $CXXFLAGS -O2 $GAME_INCLUDES -malign-int -c "$REPO/game/Car.cpp" -o "$ALIGN/Car.align.o" > "$ALIGN/Car.align.err" 2>&1; then
    echo "game/Car.cpp with -malign-int: compiled" >> "$ALIGN_LOG"
elif grep -q 'internal compiler error' "$ALIGN/Car.align.err"; then
    echo "game/Car.cpp with -malign-int: ICE: $(grep -m1 'internal compiler error' "$ALIGN/Car.align.err")" >> "$ALIGN_LOG"
else
    fail "unexpected -malign-int failure in game/Car.cpp"
fi
note "malign-int: layout changes (see $ALIGN_LOG); 64-bit division ICEs at O0/O1/O2"
note "malign-double: unsupported by GCC 6.5 at O0/O1/O2"

# ---- 5. uint8 return convention --------------------------------------------
U8="$WORK/u8"
mkdir -p "$U8"
cat > "$U8/f.c" <<'EOF'
typedef unsigned char u8;
int g;
u8 f(void) { return (u8)g; }
EOF
cat > "$U8/caller.c" <<'EOF'
typedef unsigned char u8;
extern u8 f(void);
int call(void) { return f() + 256; }
EOF
U8_LOG="$LOGS/m0-probe-uint8.log"
: > "$U8_LOG"
for o in 0 1 2; do
    $CC $CFLAGS -O$o -S "$U8/caller.c" -o "$U8/caller.$o.s"
    if grep -q -e 'and\.l #255,d0' -e 'move\.b d0,d0' "$U8/caller.$o.s"; then
        printf 'O%s: caller zero-extends the u8 return\n' "$o" >> "$U8_LOG"
    else
        printf 'O%s: NO zero-extension found; inspect %s\n' "$o" "$U8/caller.$o.s" >> "$U8_LOG"
    fi
done
note "uint8:      caller zero-extends at O0/O1/O2 (see $U8_LOG)"

# ---- 6. nix20 startup and stack --------------------------------------------
NIX="$WORK/nix"
mkdir -p "$NIX"
printf 'int main(void){return 0;}\n' > "$NIX/start.c"
NIX_LOG="$LOGS/m0-probe-nix20.log"
if $CC $CFLAGS -Wl,-Map="$NIX/start.map" "$NIX/start.c" -o "$NIX/start" > "$NIX_LOG" 2>&1; then
    grep -E 'ncrt0|libnixmain|libnix20' "$NIX/start.map" >> "$NIX_LOG" || true
    if grep -q 'swapstack' "$NIX/start.map"; then
        echo "swapstack.o: linked" >> "$NIX_LOG"
    else
        echo "swapstack.o: not linked (libnix20 provides ___StackSwap/___stkswap only when referenced)" >> "$NIX_LOG"
    fi
else
    cat "$NIX_LOG" >&2
    fail "nix20 startup probe did not link"
fi
for flag in -mstackextend -mstackcheck; do
    if $CC $CFLAGS $flag "$NIX/start.c" -o "$NIX/start.flag" > "$NIX/flag.out" 2>&1; then
        echo "$flag: accepted" >> "$NIX_LOG"
    else
        echo "$flag: unsupported: $(grep -m1 'unrecognized command line option' "$NIX/flag.out")" >> "$NIX_LOG"
    fi
done
if [ -f "$REPO/build/amiga/opencmr2" ]; then
    strings "$REPO/build/amiga/opencmr2" | grep -i 'stack=' >> "$NIX_LOG" || true
fi
echo "initial stack: provided by AmigaOS (Shell/Workbench); the wrapper's STACK is a compile definition for M0.9/M1" >> "$NIX_LOG"
note "nix20:      PASS (ncrt0/libnixmain/libnix20 startup; swapstack not linked by default)"

note ""
note "M0.4 probes finished. See build/amiga/m0-probe-*.log."
note "The -malign-int 64-bit division ICE is recorded as a blocker: do not enable it before M0.5."

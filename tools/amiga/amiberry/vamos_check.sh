#!/bin/bash
# Build the planes check with a given decoder source and run it in vamos (amitools in the
# toolchain image). vamos is a hint, not proof: it passed all 549 frames while the real
# machine failed. Usage: vamos_check.sh decoder.cpp [FRAMES]   (run from the repository root)
set -e
DECODER=$(realpath "$1")
FRAMES=${2:-12}
WORK=${WORK:-/tmp/vamos_check}
GAME=${GAME:-$HOME/Desktop/Colin McRae Rally 2}   # a directory with FrontEnd/Videos/cm.bik
mkdir -p "$WORK/video"
cp "$DECODER" "$WORK/video/bink_decoder.cpp"
cp src/video/bink_decoder.h src/video/bink_tables.h "$WORK/video/"
FLAGS="-DBIGENDIAN -DOPENCMR2_AMIGA -I/out/video -I/src/src -m68040 -m68881 -mhard-float -mcrt=nix20 \
 -fno-omit-frame-pointer -fno-ivopts -fno-builtin-sin -fno-builtin-cos -fno-builtin-sinf -fno-builtin-cosf \
 -O2 -std=gnu++14 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fwrapv -fno-strict-aliasing \
 -fsigned-char -ffp-contract=off"
# planes_check links the swapped stack on the target; vamos cannot do that, so this build has none.
docker run --rm -v "$PWD":/src -v "$WORK":/out -w /src opencmr2/amiga-toolchain:latest sh -c "
  m68k-amigaos-g++ $FLAGS -fno-caller-saves -c /out/video/bink_decoder.cpp -o /out/dec.o &&
  m68k-amigaos-g++ $FLAGS -fno-caller-saves -c src/video/yuv_rgba.cpp -o /out/yuv.o &&
  m68k-amigaos-g++ $FLAGS -c tests/amiga/bink_planes_check.cpp -o /out/chk.o &&
  m68k-amigaos-gcc $FLAGS -fno-builtin -fno-math-errno -x c -c src/platform/amiga/amiga_libfix.c -o /out/fix.o &&
  m68k-amigaos-g++ -m68040 -m68881 -mhard-float -mcrt=nix20 -o /out/check /out/chk.o /out/dec.o /out/yuv.o /out/fix.o"
docker run --rm -v "$WORK":/b -v "$GAME":/game:ro -w /game opencmr2/amiga-toolchain:latest \
  vamos -C 68040 -H disable -m 65536 -s 1024 /b/check FrontEnd/Videos/cm.bik "$FRAMES" 2>&1 \
  | grep -E "planes_check|InvalidMemory|PC=" | head

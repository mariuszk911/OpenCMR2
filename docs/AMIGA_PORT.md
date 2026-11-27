# AmigaOS/PiStorm port

How the native AmigaOS 68k build of OpenCMR2 is built and how it is put
together. Player instructions (requirements, installing, settings) are in the
root [README](../README.md). The rules for changing the code are in
[AGENTS.md](../AGENTS.md).

The host builds (Linux, Windows, macOS, Android) are unchanged and remain the
reference for game logic and file layout.

## Target

| Item | Requirement |
| --- | --- |
| Operating system | AmigaOS 3.2 |
| CPU | 68040 + FPU build |
| Hardware | PiStorm running Emu68. Tested only on a PiStorm32 with Emu68 1.1 beta on a CM4 (4 GB) at stock clock |
| Display | RTG, 32-bit mode, 640x480 or larger |
| Audio | AHI (without it the game runs silent) |
| 3D | `minigl.library` 30.0 or newer, installed by the user (not shipped). [PiStorm3D from Aminet](https://aminet.net/package/util/libs/PiStorm3D); optionally ogles2.library 1.3.3 or newer |
| Game data | The user's original PC installation, next to the executable (`PROGDIR:`) |

SDL3 is not linked into the Amiga binary. The platform layer, renderers, audio
and input are native AmigaOS code.

## Building

### Host builds

Host builds and tests must stay green for every change:

```sh
cmake --preset linux
cmake --build --preset linux
ctest --preset linux
```

When fixed-point, layout or disk code changes, also run the 32-bit reference
build (`linux-x86` preset) and `python3 tools/gen_disk_layouts.py --check`.

### Amiga

The Amiga build needs three SDKs under `SDKs/` (not in the repository):

| Directory | Source |
| --- | --- |
| `SDKs/PiStorm3D` | [PiStorm3D.lha](https://aminet.net/util/libs/PiStorm3D.lha) on Aminet (minigl.library 30.0 with the 29.1 client SDK) |
| `SDKs/ogles2-sdk-1.3.3` | `ogles2-1.3.3-sdk.zip` from the [ogles2 v1.3.3 release](https://github.com/ladislawv/ogles2/releases/tag/v1.3.3) |
| `SDKs/m68k-amigaos-ahi` | the AHI developer files in the toolchain image (`/root/build/ahi/m68k-amigaos-ahi`) |

`.github/workflows/build.yml` fetches them with pinned SHA-256 checksums,
builds, packages and attaches the `.lha` to a release for every `v*` tag.
Locally the build goes through one wrapper:

```sh
tools/amiga/build.sh            # incremental build
tools/amiga/build.sh --clean    # full rebuild
tools/amiga/build.sh --info     # image, SDK, compiler, CMake and flags in use
```

The wrapper uses `m68k-amigaos-gcc` (Bebbo's GCC 6.5) when it is in `PATH`,
otherwise the Docker image built from `tools/amiga/Dockerfile`. It produces
`build/amiga/opencmr2`, stages the release drawer in `dist/amiga/` and the
target diagnostics in `dist/amiga-tests/`.

Incremental builds on this toolchain do not notice every header or flag
change. Use `--clean` after changing a structure, a compiler flag or a
force-included header.

**The client SDK and the runtime library are separate pins.** The client is
built against the PiStorm3D 29.1 SDK (`MINIGL_VERSION 29`, dispatch ABI 5,
`sizeof(MGLDispatchTable) == 840`); a 29.0 library reports 836 bytes and
`MiniGLOpen()` refuses it without a diagnostic. The library that runs the
game must be `minigl.library` 30.0 or newer: 29.1 has the same dispatch ABI
and accepts the client, but its present path can stall right after the first
picture. The game checks the runtime version before opening the context and
shows a requester with the Aminet package when it is missing or too old. The
wrapper validates the 29.1 client SDK; it does not stage a runtime library.

### Packaging

`tools/amiga/package.sh` archives `dist/amiga/` as
`dist/OpenCMR2-amiga-<version>.lha` with a top-level `OpenCMR2/` drawer. The
drawer holds the executable and its icon, the compiled GLES2 shaders
(`tools/amiga/shaders/`), the player README (`res/amiga/README`) and the GPLv3
`LICENSE`. It holds no `minigl.library`, SDK, game data or diagnostics. Run it
after `tools/amiga/build.sh --clean`.

## Architecture

### Two compilation domains

The game code must keep the original 32-bit MSVC structure layout, while the
AmigaOS headers must keep theirs. The build therefore has two domains with
different flags:

| Domain | What | AmigaOS/MiniGL headers | `-malign-int` |
| --- | --- | --- | --- |
| A — game layout | `game/`, the disk codecs in `src/port/`, `src/platform/amiga/main_amiga.cpp` | No | Yes |
| B — OS and renderer glue | the rest of `src/platform/amiga/`, the renderers, audio, input, video and network code | Yes | Never |

`src/port/*.h` is the boundary between them. Only fixed-size scalars, opaque
handles, pointers and structures with a checked identical layout cross it.
The game never sees SDL, `exec`, `intuition`, AHI or MiniGL headers.
`main_amiga.cpp` owns startup and reaches the OS only through plain-C
functions implemented in Domain B.

The domains are separate CMake object libraries (`src/amiga.cmake`), so their
flags cannot leak into each other. A layout probe compares the Domain A
structure offsets with the 32-bit host reference.

### Platform layer

`src/platform/amiga/` implements the `Sys_*` API: files and paths
(`PROGDIR:` is both the data and the user directory), settings in
`PROGDIR:opencmr2.ini`, timing through `timer.device`, the log
(`PROGDIR:opencmr2.log` and stderr), requesters, the stack (libnix
`swapstack.o` switches to a 2 MB stack at startup) and display-mode lists from
the display database.

Input is keyboard only. IDCMP raw-key events from the game window keep the
held-key state that `Input_ReadKeyboard`/`Input_IsKeyDown` read; focus loss
clears it so no key stays stuck.

### Renderer selection

The binary holds two renderers behind one `Gfx_*` API (`src/port/gfx.h`):

- **MiniGL** (`src/render/minigl/`), the default. It implements the API
  directly in immediate mode on `minigl.library`.
- **GLES2** (`src/render/gles2/`), on ogles2.library. It runs the shared frame
  recorder (`src/render/gfx_core.cpp`) with an ogles2 executor and shaders
  written for GLSL ES 1.00, shipped precompiled (see
  `tools/amiga/shaders/README`). When `GL_EXT_clip_control` is available it
  renders directly to the default framebuffer (`GL_UPPER_LEFT_EXT`) so V3D
  keeps depth in tile SRAM and overlaps frame presentation without
  an extra blit pass; otherwise it falls back to an offscreen framebuffer. The
  executor batches compatible 2D/3D draws (including CPU world-matrix baking
  for small meshes and screen-space non-overlapping merging for alpha-blended
  3D draws), deduplicates per-frame shader uniforms, and keeps dynamic 3D
  geometry in a `GL_STATIC_DRAW` buffer separate from the 2D stream buffer.
  `[video]` options `gles2_direct`, `gles2_batch` (`0`/`1`/`2`),
  `gles2_static_dynvb`, and `gles2_stats` control these paths for diagnostics.

`src/render/gfx_dispatch_amiga.cpp` picks one from `[video] renderer` on the
first call. When `gles2` cannot start (no ogles2.library, too old, or no
32-bit screen) the game logs why and uses MiniGL.

### Audio output

AHI receives one finished stereo stream. All voices, the music and movie sound
are mixed by the portable integer mixer `src/audio/mixer.cpp` in an
`OpenCMR2 audio` process. `AHIA_SoundFunc` paces the output; a block that is
not ready is replaced by silence. Blocks are sized from the rate AHI reports,
not the one requested. `[audio] rate` and `block_ms` (default 20 ms) can be
set in `opencmr2.ini`. Without `ahi.device` the game runs silent.

### Movies and network

Bink movies use the shared decoder (`src/video/bink_decoder.cpp`) with an
Amiga player (`src/video/movie_amiga.cpp`); see [VIDEO.md](VIDEO.md). Network
play uses ENet over bsdsocket.library; see [NETWORK.md](NETWORK.md#amigaos).

## Data and byte order

The PC data is little-endian; the 68k is big-endian. Raw file bytes are never
cast to multi-byte types. Every disk format the game reads goes through an
audited little-endian codec in `src/port/` with a host test, and the generated
layout tables (`tools/gen_disk_layouts.py`) describe the structures that are
converted in place.

### Saved games

The Amiga build writes its own saved-game format: the original
`SavedGameImage` layout with big-endian words, explicitly packed bitfields and
a header with a signature, a format version and a CRC-32. PC saves are
recognised, skipped and never modified; the player is told once per run. A
save is written to a staging file and renamed over the old one only when the
whole write succeeded, so a failed save keeps the previous one.

The game's configuration (`GameInfo.rcf`) and driver profiles (`.pps`) are
written in the Amiga's native byte order and are not exchangeable with the PC
version.

## Known differences

The renderer differences are logged once with an `AMIGA:` prefix.

| Effect | Amiga behaviour |
| --- | --- |
| Car reflections (cube maps) | MiniGL: off, neutral texture. GLES2: drawn |
| Distance fade (fog) | GLES2: not drawn |
| Render-to-texture effects | MiniGL: not drawn |
| `ADDSIGNED`, `SUBTRACT`, `BUMPENVMAP` texture stages | MiniGL: replaced by `ADD`, `SELECTARG1` and `MODULATE` |
| Point and spot lights | MiniGL: one directional light |
| 3D sound | None (AHI has no 3D effects) |
| Joysticks, gamepads, wheels, force feedback | Not supported |

## libnix and GCC 6.5 gotchas

The toolchain has traps that ordinary C and C++ fall into. The rules are in
[AGENTS.md](../AGENTS.md#68klibnix-rules); the main ones:

- Never use `%f` or `%g` in `printf`; log integers.
- The `*f` maths functions and `llrint`/`lrint` are wrong with
  `-mhard-float`; `src/platform/amiga/amiga_libfix.c` replaces them (rounding
  ties to even, as the original's x87 code did).
- libnix `qsort` can run off the array with a comparator that never returns 0;
  `amiga_qsort.c` replaces it.
- AmigaOS allocation is not zeroed; use `calloc` or `memset`.
- Redirect with `freopen`, never `dup2`. Read environment variables with
  `GetVar`, not `getenv`.
- 64-bit division and some float-to-int conversions crash the compiler in a
  `-malign-int` unit; use the helpers in `src/port/intdiv.*`.
- `abs(short)` picks the `double` overload; never use it as a shift count.
- Bebbo's backend optimiser can store through an address register it never
  loaded (seen with `std::vector::clear()` of a vector at offset 0).
  `tools/amiga/scan_unloaded_regs.py` screens the disassembly for it; check
  for this pattern first when a fault appears only on the target. The Bink
  decoder files are also built with `-fno-caller-saves` (`src/amiga.cmake`).
- No exceptions, RTTI, threads, atomics or `std::filesystem`.

## Upstream sync

1. Merge the upstream branch; never rewrite its history.
2. Run the host builds and tests (and `linux-x86` when fixed-point, layout or
   disk code changed), then `python3 tools/gen_disk_layouts.py --check`.
3. Rebuild the Amiga target with `tools/amiga/build.sh --clean`.
4. Reconcile every `game/` change with the
   [divergence manifest](GAME_DIVERGENCE.md): put the merged upstream commit in
   `tools/game_divergence_base` and run `python3 tools/game_divergence.py`.
   A conflict outside the manifest is an accidental divergence or a new
   upstream feature to absorb deliberately.
5. `src/port/menu_layout.h` and `src/port/x87single.h` stand in for
   `game/Menu.h` and `game/X87Single.h`; their host tests fail when upstream
   changes those headers.
6. `src/video/bink_decoder.{h,cpp}` carries fork changes; re-check the
   reference hashes in [VIDEO.md](VIDEO.md) after merging it.
7. Run the game on the target before calling the sync done.

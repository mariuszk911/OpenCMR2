# WarpOS (PowerPC Amiga) port

A second Amiga target next to the 68k build of [AMIGA_PORT.md](AMIGA_PORT.md):
a WarpOS executable for a classic Amiga with a PowerPC card and a Radeon 9200,
drawn through the PPC MiniGL R200 stack. It shares the game layer, the MiniGL
renderer, the mixer and the settings code with the 68k port.

## Requirements

- AmigaOS 3.x with WarpOS (`powerpc.library`), a 603e or better (the reference
  machine is an A4000, 68060 + MPC7410).
- Radeon 9200 with `Radeon9200.chip` and `Prometheus.card`.
- The PPC MiniGL R200 stack, dispatch ABI 5, from its MiniGLWOS-aligned
  version on (840-byte table, OpenGL clear bits): `minigl_ppc.dll` and
  `minigl_ppc_host` in the game drawer, `minigl.library` 12 in `LIBS:`.
  The binary is built on the same headers as Steffen Haeuser's MiniGLWOS
  (Aminet `util/libs/MiniGLWOS_Classic`, Warp3D), so it should load that DLL
  too; that combination is untested (no Warp3D driver for the Radeon 9200).
- AHI 4 for sound.

## Building

```sh
tools/warpos/build.sh            # build/warpos, stages dist/warpos
tools/warpos/build.sh --clean
tools/warpos/build.sh --info
tools/warpos/build.sh --g4       # build/warpos-g4, dist/warpos-g4
tools/warpos/package.sh          # dist/OpenCMR2-warpos-<version>.lha and .zip
```

`package.sh` archives `dist/warpos` (the executable, `OpenCMR2-WOS`, the
README from `res/warpos/README` and the licence) in an `OpenCMR2/` drawer.
The MiniGL PPC stack is a requirement and is not packaged.

The default build is `-mcpu=603e` and runs on every WarpOS board. `--g4`
selects `-mcpu=7400` for 7400/7410 boards only, with `-mno-altivec
-mabi=no-altivec`: a WarpOS task's stack is 4-byte aligned and the hunk
loader aligns sections to 8 bytes, so compiler-generated vector spills and
constants would load the wrong data. On the reference A4000 it measures the
same as the default build (19.7 ms a frame at the start line), so the
release uses the default build.

Native only: `ppc-amigaos-gcc` 11 with the MOS2WOS SDK and the PPC MiniGL
tree (`WOS_TOOLCHAIN_BIN`, `MOS2WOS_ROOT`, `WARPOS_MINIGL`). The link is a
relocatable ELF; the build checks the WarpOS contract (no undefined or COMMON
symbols, `_start` at `.text+0`, only `ADDR32`/`ADDR16_LO`/`ADDR16_HA`/`REL24`
relocations) and `tools/warpos/elf2warpos` converts it to the hunk executable.

## Running

Cold boot, then from the game drawer: `Execute OpenCMR2-WOS`. The script sets
the MiniGL variables and starts the game with a 1 MB stack; `minigl_ppc.dll`
starts `minigl_ppc_host` itself.

| Variable | Value | Why |
| --- | --- | --- |
| `MGLPPC_CPEMIT` | 1 | Always on: needed for performance, and presents fail without it. |
| `MGLPPC_CPPRESENT` | 0 | With 1 the picture had wrong colours and diagonal bands. |
| `MINIGL_CTILE`, `MGLPPC_LEASE` | removed | With them presents failed and the host stopped. |
| `MGLPPC_HOSTPRI` | -1 | Host priority the stack is run with. |
| `MGLPPC_NOALTIVEC` | 1 | The stack's release setting. |
| `MGLPPC_TCL6`, `MGLPPC_TCL5` | removed | Compact vertex formats for 2D clients; with both set distant scenery was smeared. TCL6 alone saves under 1 ms a frame and is not proven safe. |

`SetEnv OPENCMR2_QUIT 1` quits a running game (for remote sessions; never
Break a PPC program). `SetEnv OPENCMR2_SHOT 1` writes the next frame to
`PROGDIR:shot.ppm`.

A failed MiniGL run (a present error, a stopped host) can leave the stack
unable to create contexts until the next cold power cycle; a warm reboot does
not reset the Radeon.

## Differences from the 68k port

- **Structure packing.** AmigaOS structures are 2-byte packed; OS headers are
  included between `amiga_os_begin.h` and `amiga_os_end.h`, which apply
  `#pragma pack(2)` on PPC only. The game layout needs no `-malign-int`.
- **Presentation.** 16-bit fullscreen, two buffers, sync off (blit). With
  sync on (flips) `mglSwitchDisplay` fails with `GL_INVALID_OPERATION` once a
  frame is presented while the previous flip is pending. The display is
  locked across `mglSwitchDisplay`. `MGLDispatchTable` is 840 bytes (ABI 5), upstream's and MiniGLWOS's.
- **Input.** No input.device handler (it would be 68k interrupt code): RAWKEY
  IDCMP messages on MiniGL's own window feed a held/latch table, so a key
  pressed and released between two polls is still seen.
- **Audio.** One looped AHI dynamic sample is the ring; a ten-word 68k hook
  stores the play offset; the game task mixes ahead and flushes the written
  cache lines. No mixer process.
- **OS calls.** Every OS call from the PPC is a 68k context switch: ENV:
  switches are polled once a second, nothing calls the OS per frame.
- **Clock.** The game clock is the PPC time base (`wos_clock.c`), measured
  against `DateStamp`; `ReadEClock` costs about 0.4 ms per read from the PPC.
- **Not built:** network play (stand-in providers), the GLES2 renderer.

## Renderer on WarpOS

On the R200 PPC stack a draw call is expensive and a vertex is cheap: a small
draw costs about 17 us, any GL state change between two draws about 10 us
more, and a stage issues 500-700 draws a frame, two thirds of them four
triangles or fewer. The MiniGL renderer therefore does three things on WarpOS
only (`gfx_minigl.cpp`):

- **CPU transform.** Vertices and normals go through world*view on the CPU and
  the GL modelview stays the identity, so a new object matrix is not a GL
  state change.
- **CPU lighting.** Every lit draw is lit by `LightVertex` (the arithmetic of
  `port/d3d_light.h`, with per-draw precomputation). The card's hardware
  lighting works but the stack re-sends light state with every small draw.
- **Texture-sorted buckets.** Depth-tested, depth-writing triangle draws
  (opaque and blended) are collected per texture and raster state and sent as
  one batch each, opaque first, when an order-dependent draw (2D, no depth
  write) or any other GL call comes.

- **CPU back-face culling.** A triangle of a culling draw that faces away is
  dropped while its bucket is built, so its vertices are never lit, stored,
  sent over the bus or seen by the stack. The facing test is made in object
  space against the eye position, so culled vertices are not transformed.
- **View-volume culling.** A triangle whose three vertices lie outside the
  same plane of the view volume (clip space, world*view*projection) is
  dropped too: GL would clip it away after it had cost the stack about a
  microsecond per vertex. At the Finland 1 start line this drops 1,241
  triangles a frame.
- **Point-light reach per draw.** A point or spot light whose range cannot
  reach the draw's bounding sphere is left out of its lighting, so most lit
  vertices take an integer path; the test uses no software square roots.
- **`frexp`/`ldexp` on the IEEE bits.** The game's x87 single-precision
  emulation (`game/X87Single.h`) calls both for every emulated operation;
  newlib's versions go through `finite` and `scalbn` and took 6% of a
  driving frame. `src/platform/warpos/wos_libfix.c` replaces them with
  bit-exact versions (`wos_fastmath.h`, host test `tests/wos_fastmath.cpp`).

Measured on the reference A4000 at the Rally Finland stage 1 start line
(584 draws, 12,477 vertices a frame): 50 ms a frame before, 19.7 ms after
(50 fps standing), 584 batches down to 96 and 4,809 vertices sent. While
driving, frames take 21-30 ms depending on what is in view; the heaviest
seen is off the road in dense forest.

The stack's own profile (its `MGLPPC_PROFILE`/`MGLPPC_AUTOREPORT` DLL) at the
start line: almost no waiting for the card (2 us a frame), 8 submissions, and
about 8.7 ms of its CPU work, most of it per vertex sent (vertex calls
2.6 ms, call handling 2.4 ms, serialization 1.5 ms, command emit 1.5 ms).
The rest of the frame is the port's per-draw work and the game. FastPath V2
(direct vertex buffer) was tried and is slower here (20.8 against 19.7 ms):
its end step packs float colours, divides every texture coordinate by `q`
and computes fog per vertex.

The game's own draw-distance option (Options, Graphics, Advanced graphics
options, draw distance) is the lever for slower scenes: one step below the
default, the start line takes 15.1 ms and the forest case 21.1 ms. The
default stays the original's.

The event poll makes no OS call on WarpOS when nothing is pending: the
window port's list is looked at directly and Ctrl-C is not checked (a PPC
program is quit through its own exit, the close gadget or
`ENV:OPENCMR2_QUIT`).

## Settings

`opencmr2.ini` as in the 68k port, plus:

| Setting | Default | Meaning |
| --- | --- | --- |
| `video.depth` | 16 | Screen depth for the MiniGL context. |
| `video.batch` | 1 | Keep consecutive draws in one batch and transform on the CPU. |
| `video.batch_sort` | 1 | Collect depth-tested draws in per-texture buckets. |
| `video.batch_sort_blended` | 1 | Also sort blended draws that test and write depth. |
| `video.batch_arrays` | 0 | Send buckets as indexed arrays (not faster on the reference machine). |
| `video.cpu_cull` | 1 | Drop back-facing triangles on the CPU before they reach a bucket. |
| `video.cpu_clip` | 1 | Drop triangles wholly outside the view volume on the CPU. |
| `video.cpu_light_all` | 1 | Light every lit draw on the CPU; 0 uses the card's lighting. |
| `video.profile` | 0 | Log where the 3D draw time goes, every 480 frames. |
| `video.frame_stats` | 0 | Log frame, present and draw-call times, every 480 frames. |
| `video.profile` (WarpOS) | 0 | Also logs the audio feed, file I/O and audio API time a frame. |
| `audio.ahi_mode` | 0 | AHI audio mode ID (hexadecimal with `0x`). 0 is the AHI prefs default; set a mode when the default cannot be allocated, e.g. `0x00020002` for Paula fast 14-bit stereo++. |

## Diagnostics

- `SetEnv OPENCMR2_PCPROF 1` while the game runs starts a PC sampler (the
  7410's performance monitor, one sample about every 100 us) and writes
  `PROGDIR:pcsamp.bin` when 65,536 samples are taken: `"PCS1"`, the count,
  the run-time address of `Sys_Log`, then (pc, lr) pairs. Subtract the load
  offset (`Sys_Log` at run time minus its address in
  `build/warpos/opencmr2.debug.elf`) and resolve with `nm`; DWARF 5 line
  information needs a recent symbolizer (`llvm-symbolizer`) on the object
  files. G4 boards only.

## Known limits

- Intro movies play at 5-10 fps with dropped frames.
- A few audio ring underruns per session.

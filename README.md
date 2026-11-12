# OpenCMR2 — AmigaOS / PiStorm port

A native AmigaOS 68k port of **Colin McRae Rally 2.0** (PC, 2000), built on the
[OpenCMR2](https://github.com/pablocpas/OpenCMR2) native port and the
[CMR2Decomp](https://github.com/pablocpas/CMR2Decomp) decompilation. The game
code runs natively on the 68k CPU of a PiStorm accelerator; there is no
emulation of the PC code.

This fork is the Amiga target only. For the Linux, Windows, macOS and Android
versions, see the upstream project and [README.upstream.md](README.upstream.md).

> [!WARNING]
> This repository contains no game assets. You need your own copy of the
> original PC version of Colin McRae Rally 2.0.

## Status

| Area | State |
| --- | --- |
| Boot, menus, language select | Working on target |
| Single stages with keyboard and HUD | Working on target |
| Audio (AHI, menus and in-game) | Working on target |
| Bink intro and country videos | Working on target |
| Saves (save, list, load, overwrite) | Working on target (low priority) |
| Network play (bsdsocket.library) | Working; cross-play with Linux verified |
| Packaging (`dist/amiga/`) | Not started |

Open work and evidence are tracked in [TODO.md](TODO.md).

## Target

- **CPU:** 68040 + FPU build baseline; 68060 / PAL is the acceptance target
- **Hardware:** PiStorm on Raspberry Pi 4 / CM4 / 4B
- **OS:** AmigaOS 3.2
- **Display:** RTG, 640x480 baseline
- **Graphics:** MiniGL 29.1 (`minigl.library`)
- **Audio:** AHI
- **Network:** bsdsocket.library (ENet over the Amiga TCP/IP stack)

## Building

### Amiga

```sh
tools/amiga/build.sh            # incremental
tools/amiga/build.sh --clean    # full rebuild
```

The wrapper runs natively, in Docker, or through `flatpak-spawn --host`. It
requires the MiniGL 29.1 SDK in `SDKs/PiStorm3D/`. The build checks the SDK
version and refuses any other. Details: [docs/AMIGA_PORT.md](docs/AMIGA_PORT.md).

### Linux (host reference and tests)

The host build uses Clang. The Docker image in `tools/host/` provides it:

```sh
docker build -t opencmr2-host -f tools/host/Dockerfile tools/host
docker run --rm -v "$PWD":/src -w /src --user "$(id -u):$(id -g)" -e HOME=/tmp \
    opencmr2-host sh -c 'cmake --preset linux && cmake --build --preset linux && ctest --preset linux'
```

CMake presets require CMake 3.25 or newer.

## Documentation

- [docs/AMIGA_PORT.md](docs/AMIGA_PORT.md): toolchain, build, memory and platform layer
- [docs/NETWORK.md](docs/NETWORK.md): network play on AmigaOS
- [docs/BINK_AMIGA_PLAN.md](docs/BINK_AMIGA_PLAN.md): video playback
- [docs/PHYSICS_PARITY.md](docs/PHYSICS_PARITY.md): physics verification against the original
- [TODO.md](TODO.md): milestones and evidence log

## Approach

The game code in `game/` comes from CMR2Decomp and stays close to the
original. Every change the port needs is marked with a `PORT:` comment. The
Amiga platform layer lives in `src/platform/amiga/` and `src/port/`.

## Licence

GPL-3.0. See [LICENSE](LICENSE). MiniGL is a third-party library with its own
licence, kept with the SDK.

## Credits

- Colin McRae Rally 2.0 by Codemasters
- [CMR2Decomp](https://github.com/pablocpas/CMR2Decomp) and
  [OpenCMR2](https://github.com/pablocpas/OpenCMR2) upstream authors

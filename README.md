<p align="center">
  <img src="docs/logo.jpeg" width="320" alt="OpenCMR2 logo">
</p>

# OpenCMR2 for AmigaOS / PiStorm

A native AmigaOS 3.2 port of [OpenCMR2](https://github.com/pablocpas/OpenCMR2),
the open reimplementation of **Colin McRae Rally 2.0** (PC, 2000). It runs the
original game code natively on a PiStorm-accelerated Amiga, with a MiniGL (or
GLES2) renderer, AHI sound and keyboard controls. There is no emulation.

> [!WARNING]
> This repository contains no game assets. You need your own copy of the
> original PC version of Colin McRae Rally 2.0 to play.

## What works

- The intro and country movies and the menus
- Driving the stages with the keyboard, with the HUD and sound
- Music in the menus and the arcade race
- Saving and loading games
- Network races over TCP/IP with other OpenCMR2 players (tested against the
  Linux version)
- Fullscreen on an RTG screen, or a window on Workbench

## Requirements

- AmigaOS 3.2
- A PiStorm running Emu68 (the game is built for a 68040 with FPU). Tested
  only on a PiStorm32 with Emu68 1.1 beta on a CM4 (4 GB) at stock clock; it
  has not been tested on a real 68040/68060 or other accelerators.
- An RTG card mode with 32-bit colour, 640x480 or larger
- AHI (without AHI the game runs silently)
- For network play: a TCP/IP stack with bsdsocket.library 4 or newer
  (Roadshow, Miami or Genesis)
- `minigl.library` 29.1 or newer in `LIBS:` (not included; version 29.0 does
  not work)
- Optional, for the GLES2 renderer: ogles2.library 1.3.3 or newer
- The original PC installation of Colin McRae Rally 2.0

## Installing

1. Download `OpenCMR2-amiga-<version>.lha` from the
   [releases](https://github.com/mariuszk911/OpenCMR2/releases) page and
   extract it with `lha x`.
2. Make a **copy** of your PC installation on an Amiga drive. Do not use your
   only copy: the Amiga version writes its settings and driver profiles in its
   own format, which the PC version cannot read. Saved games are kept apart
   from PC saves, and PC saves are never changed.
3. Copy `opencmr2`, `opencmr2.info` and the `shaders` drawer into that copy,
   next to the game data.
4. Start the game from its icon, or from a Shell in that drawer:

   ```
   opencmr2
   ```

The first start resets the game's configuration once. This is expected.

## Playing

The game is played with the keyboard; keys are set in the game's own
controller menu. The mouse is not used. Ctrl-C in the Shell, or the window's
close gadget, quits like the menu's Quit.

Joysticks, gamepads and wheels are not supported yet, and there is no force
feedback.

## Settings

`opencmr2.ini`, next to the game, holds the port's own settings. The game's
options are set in its menus.

```ini
[video]
fullscreen = 1
renderer = minigl
cube_faces_per_frame = 1
```

| Setting | Meaning |
| --- | --- |
| `fullscreen` | `1` opens a screen, `0` a window on Workbench. |
| `renderer` | `minigl` (default) or `gles2`. `gles2` needs ogles2.library; the game falls back to MiniGL when GLES2 cannot start. |
| `cube_faces_per_frame` | GLES2 only: how many faces (1-6) of the car's reflection map are redrawn each frame. `6` is the original behaviour and is slow. |

Network settings (`[net]`) and how to host or join a race are described in
[docs/NETWORK.md](docs/NETWORK.md).

## Known differences from the PC version

- MiniGL renderer: no car reflections. The GLES2 renderer draws them, but has
  no distance fade.
- No 3D sound effects (AHI has none).
- Settings, profiles and saved games are not shared with the PC version.

## Reporting problems

The game writes `opencmr2.log` next to itself. Please attach it to any
[issue](https://github.com/mariuszk911/OpenCMR2/issues) you open, together
with your Amiga setup (PiStorm model, CPU, RTG mode, AHI driver).

## Building from source

The port is cross-compiled on Linux with Bebbo's m68k-amigaos GCC and the
PiStorm3D SDK 29.1, which is not included here. See
[docs/AMIGA_PORT.md](docs/AMIGA_PORT.md) for the toolchain, build and
packaging, and [AGENTS.md](AGENTS.md) for the contribution rules. The original
OpenCMR2 README for the Linux, Windows, macOS and Android builds is kept in
[README.upstream.md](README.upstream.md).

## Licence

OpenCMR2 is free software under the GNU General Public License version 3
(see [LICENSE](LICENSE)). Colin McRae Rally 2.0 and its data belong to their owners.
This project is not affiliated with Codemasters.

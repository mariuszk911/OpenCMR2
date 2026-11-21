# AGENTS.md

Read this before changing code. It is the working agreement for the OpenCMR2
AmigaOS/PiStorm port. The full reference is [docs/AMIGA_PORT.md](docs/AMIGA_PORT.md);
the backlog and acceptance criteria are in `docs-internal/TODO.md` (local only,
see "Internal documents").

## Never push to origin/main

`origin` is the upstream project. This checkout is a local fork and must stay
one. Never push to `origin/main`, never force-push it, and never open a pull
request against it. Work on `feature/amigaos-pistorm`; push only to a remote
you own if one is configured.

## Mission

Port OpenCMR2 to AmigaOS 3.2 on PiStorm (68040 build baseline, 68060/PAL
acceptance target) with a native platform layer and a MiniGL renderer, while
keeping the Linux/Windows/macOS/Android builds green.
The first playable milestone is: menus, one drivable stage with keyboard input
and audio, and load/save. Network play, Bink movies and optional visual
enhancements come later.

## Commands

Host builds are the baseline for game logic and file layout:

```sh
cmake --preset linux
cmake --build --preset linux
ctest --preset linux
```

When touching fixed-point, layout or disk helpers, also run:

```sh
cmake --preset linux-x86
cmake --build --preset linux-x86
ctest --preset linux-x86
python3 tools/gen_disk_layouts.py --check
```

Amiga builds go through the wrapper only:

```sh
tools/amiga/build.sh
tools/amiga/build.sh --clean
tools/amiga/build.sh --info
```

The wrapper selects native, Docker, or `flatpak-spawn --host` automatically.
Do not invoke the cross compiler for a project build. Use `--clean` after
changing a struct, compiler flag, force-include or header: incremental builds
on this toolchain do not track them reliably.

## Non-negotiable rules

1. **Fork-first.** Upstream `game/` stays syncable. Amiga behaviour lives in
   fork-owned files behind the existing `port/` interfaces.
2. **Ask before editing `game/`.** A `game/` change is allowed only for a
   proven portability bug, must be requested and approved first, must be the
   smallest possible diff, must carry a `// PORT: AMIGA ...` comment, and must
   be listed in the divergence manifest in `docs/GAME_DIVERGENCE.md`.
3. **Senior-grade code only.** Correct, simple, readable, verified. No
   speculative fixes, no "it compiles so it works", no hidden fallbacks, no
   copy-paste implementations. If you cannot explain and verify it, it is not
   done.
4. **One concern per change.** No drive-by refactors, formatting sweeps or
   unrelated fixes in a port commit.
5. **No undefined behaviour.** No unaligned accesses, no strict-aliasing puns,
   no reading uninitialised memory, no signed overflow except the intentional
   fixed-point arithmetic built with `-fwrapv`.
6. **Endianness is explicit.** Never cast raw file bytes to `short`, `int`,
   `float`, `double` or a struct. Use the audited little-endian helpers and
   add a host test for every new format.
7. **Layout is contractual.** Do not add pointer-sized fields to on-disk
   structures. Add `static_assert`s or offset checks for anything crossing
   the file or compiler-domain boundary.
8. **Floats are deterministic.** Preserve `X87S_*` and `FTOI_*`, do not
   reassociate, do not rely on extended precision. Wrap angles before
   `sin`/`cos`.
9. **Resource ownership is explicit.** Every `Open`/`Alloc`/`Create` has one
   owner and one cleanup path, including every error path.
10. **Check AmigaOS calls.** Handle failure, preserve `IoErr()` context, and
    log enough detail to diagnose it on target.
11. **No hidden performance work.** Profile first. No per-frame allocations,
    logging or file I/O in hot paths. Document measured trade-offs.
12. **No new dependency without approval.** Do not add SDL, ENet, pthreads,
    `std::filesystem`, `<thread>`, `<atomic>` or `<mutex>` to the Amiga
    target.
13. **Tests and evidence are part of the change.** Host tests stay green; new
    portable logic gets a host test; target work gets a boot log, screenshot
    or measured result under `build/`.
14. **Documentation follows the code.** Update `docs-internal/TODO.md` and
    the divergence manifest in the same change. Stale docs are a defect.
15. **Ask before** changing compiler/ABI flags, the target baseline, a
    dependency, a test's enabled state, or a visual fallback that changes the
    original look.

## Internal documents

Everything written for the people working on the port, not for its users,
lives in `docs-internal/`. The directory is in `.gitignore` and must never be
committed or pushed. That covers:

- handoffs between sessions or people;
- plans, investigations and debugging notes;
- status updates, progress reports and evidence logs;
- the backlog (`docs-internal/TODO.md`) and other TODO lists;
- draft patches for upstream.

Tracked files are for users and contributors: `README.md` is the end-user
README (what the port does, requirements, install, settings, known
differences), and `docs/` holds lasting reference (architecture, build,
contracts). Neither may carry dated progress notes, milestone status or links
to `docs-internal/`. Code comments must not point to `docs-internal/` either;
explain the reason in the comment or point to a tracked document.

## Two compilation domains

| Domain | Paths | `-malign-int` | NDK/MiniGL headers |
| --- | --- | --- | --- |
| A — game layout | `game/`, `src/port/disk.cpp`, `src/port/replay_disk.cpp`, `src/diagnostics/observer.cpp`, `src/platform/amiga/main_amiga.cpp` | Yes, after the M0 gate | No |
| B — OS/renderer glue | `src/platform/amiga/`, `src/render/minigl/`, `src/audio/audio_ahi.cpp`, `src/input/input_amiga.cpp`, `src/render/gfx_core.cpp`, executors, stubs | Never | Yes |

- `src/port/*.h` is the ABI. Only fixed-size scalars, opaque handles,
  pointers and structures proven to have the same layout in both domains may
  cross it.
- If a shared type's layout differs, do not cast it. Pass bytes or opaque
  handles, or stop and re-plan.
- Never include `platform/platform.h` (it includes SDL3) from Domain B.
  Shared code reads settings through `Sys_GetOption`/`Sys_GetOptionText`;
  `Gfx_SetWindowMode` belongs in the host SDL executor translation units.
- `main_amiga.cpp` stays free of NDK types and reaches MiniGL/AmigaOS through
  a plain-C facade implemented in Domain B.
- The game must never see SDL, `exec`, `intuition`, AHI or MiniGL headers.

## 68k/libnix rules

- Never use `%f` or `%g` in `printf`/`Sys_Log`; assert on values.
- `*f` libm functions are broken with `-mhard-float`: override through the
  double versions in `amiga_libfix.c`, built with `-fno-builtin`, and verify
  the linker map names `amiga_libfix.o` as the definition.
- Wrap angles before `sin`/`cos`.
- Redirect with `freopen`, never `dup2`; `dup2` shallow-copies descriptors in
  libnix and breaks the exit path.
- AmigaOS allocation is not zeroed; use `calloc` or `memset`.
- `-O2` can miscompile `bool8`/`uint8` returns. Use per-file `-O1` only with
  `objdump` evidence.
- Never add `-malign-int` to OS glue; it grows AmigaOS structures.
- Read environment variables with `GetVar`, not libnix `getenv`.
- Include AmigaOS headers last and `#undef DeleteFile` where needed.
- No exceptions, RTTI, threads, atomics or `std::filesystem` on Amiga.

Details and evidence are in
[docs/AMIGA_PORT.md](docs/AMIGA_PORT.md#libnix-and-gcc-65-gotchas).

## Stop conditions

Stop and report instead of improvising when:

- the M0 layout probe fails: do not start patching individual structs;
- the SDK is missing, not 29.1, or `sizeof(MGLDispatchTable) != 840`;
- a `game/` edit seems necessary but has not been approved;
- a new dependency or a compiler/ABI change seems necessary;
- target evidence cannot be produced (say so; do not claim emulator results
  are target results);
- an upstream-sync conflict falls outside the divergence manifest.

## Before you call it done

- The change has one purpose and a minimal diff.
- Every new branch/error path is handled; no leaked handles, memory or locks.
- No raw multi-byte file reads/writes; endian helpers used.
- No layout-sensitive struct crosses the game/OS boundary unchecked.
- No unaligned access and no reliance on zeroed `malloc`.
- No `%f`/`%g`, no broken `*f` math, no unwrapped trig.
- Host build and tests pass; the Amiga build passes; target smoke evidence is
  attached to the task under `build/`.
- `docs-internal/TODO.md`, `docs/AMIGA_PORT.md` and
  `docs/GAME_DIVERGENCE.md` are updated.
- `git status --short` and `git diff --check` are reviewed. Never commit game
  data, SDK files, build outputs, captures or logs.

## Where to look

- `docs-internal/TODO.md` — milestones, acceptance criteria, evidence (local).
- [docs/AMIGA_PORT.md](docs/AMIGA_PORT.md) — target requirements, build,
  architecture, byte order, known differences,
  libnix gotchas, upstream sync, packaging.
- [docs/GAME_DIVERGENCE.md](docs/GAME_DIVERGENCE.md) — every `game/` change
  and how to re-apply it after a sync.
- `docs-internal/AMIGA_PORT_NOTES.md` — the detailed port notes and evidence
  (local).
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — host architecture.
- [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) — host workflow and repository
  hygiene.
- [docs/README.md](docs/README.md) — documentation index.

<!-- CODEGRAPH_START -->
## CodeGraph

In repositories indexed by CodeGraph (a `.codegraph/` directory exists at the repo root), reach for it BEFORE grep/find or reading files when you need to understand or locate code:

- **MCP tool** (when available): `codegraph_explore` answers most code questions in one call — the relevant symbols' verbatim source plus the call paths between them, including dynamic-dispatch hops grep can't follow. Name a file or symbol in the query to read its current line-numbered source. If it's listed but deferred, load it by name via tool search.
- **Shell** (always works): `codegraph explore "<symbol names or question>"` prints the same output.

If there is no `.codegraph/` directory, skip CodeGraph entirely — indexing is the user's decision.
<!-- CODEGRAPH_END -->

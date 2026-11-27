# Deploying to the real Amiga

How a build gets from the development PC onto the reference Amiga, and how a
run there is started, driven, stopped and checked. It covers both Amiga
targets: the 68k build ([AMIGA_PORT.md](AMIGA_PORT.md)) and the WarpOS build
([WARPOS_PORT.md](WARPOS_PORT.md)). Building and packaging are described
there; this guide starts with the files in `dist/`.

Players do not need any of this: they install the release archive as the
root [README](../README.md) says. This guide is for work that needs the
machine itself: a smoke run, a measurement, a screenshot.

## The chain

The development PC never reaches the Amiga's own drives. A file goes to the
TF Pi first, which serves a folder that the Amiga mounts as `PI0:`; from
there it is copied on the Amiga. A command goes through the `amiga` MCP
server, whose agent runs on the Amiga.

| Part | What it is |
| --- | --- |
| Reference machine | A4000 with a TF4060 (68060) and its TF Pi, a Prometheus with a Radeon 9200, WarpOS (`powerpc.library`) and AHI. Both Amiga builds run on it. |
| TF Pi | The Pi on the accelerator. It serves the `PI0:` share (`/home/mirek/a314shared` on the Pi, volume `PiDisk:`) and is reachable as `mirek@raspberrypi.local`. It stays up when the Amiga is reset. |
| `A314shd` | The Amiga-side daemon through which the Pi starts programs on the Amiga. The boot starts it. |
| `tfdbgd` | The agent of the `amiga` MCP server: it runs commands and programs, reads files, takes screenshots and sends keys. `agent_start` starts it from the PC. |
| Serial console | A Shell that listens on the Amiga's serial port, for when nothing else reaches the machine. |
| Game drawer | `Work:Games/CMR2` on the reference machine: the WarpOS executable, the MiniGL PPC files and the game data. |

The tool names below (`amiga_status`, `amiga_run`, ...) are those of the
`amiga` MCP server. Run one operation at a time.

## After a boot

A reset stops `tfdbgd`; the boot starts `A314shd` again.

1. `amiga_status` shows the state of the chain. After a boot `A314shd` can
   need a minute or two before it accepts the Pi. Until then the status
   says the Amiga-side daemon is not connected, and `agent_start` fails with
   the same message. Ask again. This is not a reason to reboot.
2. `agent_start` starts `tfdbgd` and returns the machine's details (CPU,
   Kickstart version, free memory). Most tools need it.
3. `list_screens`: Workbench should be the front screen. A game screen that
   is still listed belongs to a run that did not exit; see [When a run
   fails](#when-a-run-fails).
4. WarpOS only: the first PPC program of a boot opens `powerpc.library`, and
   after a cold boot that first start has reset the machine once. So make it
   a small one: `amiga_run` with the command `PI0:w0 hello` and a stack of
   1048576. (`C:InitPPC` does the same; its return code 1 is normal.) If the
   machine resets it comes back by itself: `amiga_wait_online`, then back to
   step 2.

`amiga_status` cannot tell a daemon that is still starting from one that is
not running. The serial console can. `serial_write` with `newline: true`
sends an empty line there and the reply is its prompt (`4.System:> `); a
command sent the same way runs, and `serial_read` from the returned cursor
brings its output. `Status` lists the processes, among them `C:A314shd` when
the daemon runs. If it is missing, run `Execute PI0:.tfmcp/Install` on the
Amiga, from Workbench or the serial console: it starts the daemon and adds
it to `S:User-Startup`, which is where the boot starts it.

## Getting a file onto the Amiga

### Over SSH to the share

From the development PC, `mirek@raspberrypi.local` accepts the SSH key that
the MCP server's proxy uses, so a file can be copied straight into the
share:

```sh
scp dist/warpos/opencmr2 mirek@raspberrypi.local:a314shared/cmr2wos.exe
ssh -T -o BatchMode=yes mirek@raspberrypi.local 'cksum a314shared/cmr2wos.exe'
cksum dist/warpos/opencmr2        # must print the same sum
```

A 2.5 MB executable takes about three seconds, and the Amiga sees it at
once as `PI0:cmr2wos.exe`.

### Through the MCP server

`files_write` carries the bytes in the call itself (`base64_data`; an
`offset` above zero appends a chunk), so everything passes through the
conversation. Use it for scripts, settings and small test programs, and not
for the game.

1. Compress the file on the PC with `gzip -9 -n`.
2. Split it into pieces of 18000 bytes. That is 24000 base64 characters, a
   multiple of three, so each piece decodes on its own. Send each piece with
   `files_write` (`path`, `base64_data`, `offset`) and compare the returned
   size with the running total.
3. On the Amiga, `amiga_run` with `Work:GCC-Pack/bin/gzip -d -f
   PI0:<name>.gz`. It returns 2, a warning about the share's timestamps; the
   file is fine and `<name>.gz` is gone. Compare the file's size with the
   original's. If gzip fails with 1 instead, the upload is incomplete.

Files written to the share are executable; no `Protect` is needed.

### The TF Pi's upload page

For a PC without that key, and for large files such as game data:
`http://raspberrypi.local/files`. It needs the Pi's password: ask the
machine's owner and do not store it.

1. `GET /login`, take the `csrf` value from the form; `POST /login` with
   `csrf` and `password`. A reply of 302 means it is accepted.
2. `GET /files`, take the value of `const CSRF = "..."` in the page.
3. `PUT /files/upload?dir=<folder>&name=<file>` with the headers
   `X-CSRF: <CSRF>` and `Content-Type: application/octet-stream` and the
   file as the body. The reply is `{"saved": ...}`. A reply of 409 means the
   file exists: add `&overwrite=1`.

Uploads land in the share at about 1.3 MB/s. For many files, make one LHA
on the PC first (`lha -aqo5`) and unpack it on the Amiga as an `amiga_start`
job: `System:C/lha -q x PI0:<name>.lha <drawer>/`. Check the result with
`List >T:x <drawer> ALL` followed by `Search T:x TOTAL` (the file count),
never by listing every name through the bridge.

Do not fetch large files with the Amiga's own network: transfers on it have
died after a few megabytes with the link stuck.

## Installing a build

### WarpOS

The drawer needs `opencmr2` (the executable), `OpenCMR2-WOS` (the launch
script), `minigl_ppc.dll` and `minigl_ppc_host` next to the game data, and
`minigl.library` in `LIBS:`. `dist/warpos/` holds the first two.

```sh
tools/warpos/build.sh
scp dist/warpos/opencmr2 mirek@raspberrypi.local:a314shared/cmr2wos.exe
```

Then on the Amiga, one `amiga_run` per command:

```
Copy PI0:cmr2wos.exe Work:Games/CMR2/opencmr2
List Work:Games/CMR2/opencmr2 LFORMAT="%n %l"
```

The size must equal the PC file's. When a measurement has to be compared
with the previous build, keep that executable on the share under another
name first.

`OpenCMR2-WOS` is installed the same way whenever `tools/warpos/OpenCMR2-WOS`
changes.

### 68k

`dist/amiga/` (`opencmr2`, `opencmr2.info`, `README`, `shaders/`) and the
release archive of `tools/amiga/package.sh` are installed as the player
README says: into a copy of the game data, next to it. The game needs
`minigl.library` 29.1 or newer in `LIBS:`. The diagnostics of the build in
`dist/amiga-tests/` are copied to the share and run from there like any
other program.

## A run

### Start

Start the WarpOS game with `amiga_start`, which returns at once with a job
number: command `Execute OpenCMR2-WOS`, mode `shell`, directory
`Work:Games/CMR2`. The script sets the MiniGL variables and the stack
itself. Always start it this way, from its drawer: it finds its data, the
MiniGL files and `opencmr2.ini` through `PROGDIR:`. Its output goes to a
file under `PI0:.tfmcp/jobs/`, named in the reply; `amiga_job` with the job
number waits for the program.

A 68k program is started with `amiga_start` (path and `stack`) or, when it
is short, `amiga_run`. `debug_launch` starts one stopped at its first
instruction.

### Drive it

- `screenshot` captures the front screen and saves it under
  `PI0:.tfmcp/screens/`; the reply gives the path. A moving picture that is
  captured during its update comes out in horizontal bands: take another.
- `send_keys` types into `input.device`: text, or key names such as
  `return` and combos such as `ctrl+c`. The game polls the keyboard once per
  frame and holds a key for two frames, so a key that has to stay down
  (accelerating in a stage) needs a dense stream of presses. Whether a
  really held key works is for a person at the machine to confirm.
- `SetEnv OPENCMR2_SHOT 1` (`amiga_run`) makes the game write its next frame
  to `PROGDIR:shot.ppm`.

### Stop

`amiga_run` with `SetEnv OPENCMR2_QUIT 1`. The game notices it within about
a second and exits its own way. It is the only clean stop for a remote
WarpOS session: never send Break or Ctrl-C to a PPC program, and do not kill
its task. After a failed MiniGL run, a cold power cycle comes before the
next result can be trusted. The 68k game quits on Ctrl-C in its Shell or
its window's close gadget.

`OPENCMR2_QUIT` stays in `ENV:`; the launch script deletes it at the next
start.

### Read the results

- `Work:Games/CMR2/opencmr2.log` is the game's log, several hundred
  kilobytes after a session. `amiga_read_file` takes `offset` and `length`:
  take the size with `List Work:Games/CMR2/opencmr2.log LFORMAT="%l"` and
  read from near the end.
- `error.txt` in the drawer says `Program finished normally` after a clean
  exit.
- `frame_stats = 1` and `profile = 1` under `[video]` in `opencmr2.ini` add
  a frame-time line to the log every 480 frames. The settings are described
  in [WARPOS_PORT.md](WARPOS_PORT.md#settings).
- `serial_read` shows the serial output (`kprintf`, Enforcer and MuForce
  hits); `crash_report` decodes the alerts and hits in it.

Keep what a run produced under `build/` (for WarpOS
`build/warpos-evidence/`): the log lines, the screenshots, a measured
number. `build/` is ignored, so nothing of it can be committed by accident.
A number counts as a target result only when it was produced on the machine;
say so plainly when that was not possible.

## When a run fails

- **The game does not start, or hangs before its menu.** Read the start of
  `opencmr2.log` of that run. If the MiniGL context cannot be created, run
  the stack's own example, which is kept in the game drawer as `gears_ppc`
  (it needs `minigl_ppc.dll` next to it). If that cannot open its context
  either, the stack is wedged, not the build.
- **A wedged stack.** A failed MiniGL run (a present error, a host that
  stopped) can leave the stack unable to create contexts. A warm reboot does
  not reset the Radeon; only a cold power cycle clears it.
- **Two hosts.** `minigl_ppc.dll` starts `minigl_ppc_host` from `PROGDIR:`
  itself. A second one started by hand, or the variables `MINIGL_CTILE` and
  `MGLPPC_LEASE` left in `ENV:`, make presents fail. The launch script
  removes both.
- **Stale memory reads.** `mem_read` goes through the 68k and can show old
  values for memory the PPC is writing. Log a counter instead of reading it.
- **A reboot.** `amiga_reboot` and a power cycle need the user's
  confirmation every time. Flush the disk first (`Lock Work: ON`, then
  `Lock Work: OFF`): files written shortly before a remote reboot have come
  back as their old version. The MCP proxy reconnects by itself; then
  `amiga_wait_online` and step 2 of [After a boot](#after-a-boot).

## Rules

- One MCP operation at a time, never in parallel.
- Amiga-side file names of 16 characters or fewer for anything scripted.
- Do not list a large drawer through `amiga_run`: one game drawer is 24 KB
  of names in a single reply. List one directory, or use `files_list` on
  the share.
- The Amiga's clock reads about an hour ahead of the PC's, and the TF Pi
  shows GMT. Compare sizes or checksums, never file times.

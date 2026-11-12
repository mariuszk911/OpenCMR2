# Network play

The original used DirectPlay 4. OpenCMR2 keeps the game's network code and
its DirectPlay model (`src/port/net.h`) and implements it over UDP with
[ENet](http://enet.bespin.org) (`third_party/enet`, MIT): `src/net/`.

## Playing

In the game's network menu choose **Internet / LAN (UDP)**. One player hosts
a game; the others see it in the list and join.

| Where the players are | What to do |
| --- | --- |
| Same local network | Nothing: games are found by broadcast. |
| Tailscale (or another VPN) | Nothing with Tailscale: the online machines of the tailnet are searched. With another VPN, list the host in `peers`. |
| Internet, no VPN | The host forwards UDP port 28620 on its router; the others put the host's address in `peers`. |

Only the host has to be reachable. Settings (`[net]` in `opencmr2.ini`):

| Option | Default | |
| --- | --- | --- |
| `port` | 28620 | UDP port games are hosted on and searched at |
| `peers` | | More hosts to search: names or addresses, `host:port` for another port |
| `tailscale` | auto | Search the online machines of `tailscale status` (`0` turns it off) |
| `broadcast` | 1 | Search the local network |

Play with people you trust. The game's own message handling (from 2000)
trusts what it receives; the network layer checks its protocol, sizes and
which machine speaks for which player, but not the game's messages. A VPN
such as Tailscale also keeps strangers out.

## Design

**Topology.** The host is the session's server: every other machine connects
to it and the host relays messages between them. Only the host needs to be
reachable (a forwarded port, a LAN, a VPN); the cost is one extra hop between
two guests, a few milliseconds on a LAN or tailnet, for car states of 32
bytes.

**Channels.** ENet channel 0 is reliable and ordered (the game's guaranteed
messages and the session protocol); channel 1 is unreliable (car states,
which carry their own sequence number). A retransmission never delays
positions.

**Host migration.** The game expects DirectPlay's: when the host leaves, the
earliest joined machine becomes the host (`NET_SYS_HOST`) and the others
reconnect to it with their players; machines that do not reconnect in time
are removed. Every machine knows the others' addresses from the roster, so
they all elect the same host without talking.

**Search.** Asynchronous, as in the original (`DPENUMSESSIONS_ASYNC`): while
the game lists sessions, a query goes out every second (broadcast, `peers`,
Tailscale) and `Net_EnumSessions` returns what has answered. Queries use the
session port through ENet's intercept hook, so one UDP port does everything.
Full sessions are not listed.

**Service.** One `net::Session`, two service modes chosen at compile time
(`net/net_service.h`). With a thread (Linux, Windows, macOS, Android) a thread
services the socket, as DirectPlay's did: connections stay up while the game
loads a stage without polling. Cooperative (AmigaOS, `NET_SESSION_THREADED=0`)
has no thread and no lock: `Session::Pump()` runs one pass, every public
method pumps first, and `Open(JOIN)` waits in a bounded loop that pumps. The
test suite runs in both modes. Disconnections are noticed within 10 seconds
(5 to 10 seconds of silence).

**Players.** The host numbers machines as they join; machines number their
own players (`machine << 16 | n`), so creating a player needs no round trip.
System messages (players created and destroyed, player data, session
description, host, session lost) are built in DirectPlay's layouts when the
game receives them, with their pointers into the game's buffer, as
DirectPlay did.

**Validation.** Everything read from the network is bounds-checked
(`net_wire.h`). The host refuses a machine that creates, changes, removes or
speaks for a player that is not its own, and drops it. Names are cut to the
game's buffer sizes (100 bytes for player names, which the game copies with
`strcpy`); messages are limited to 256 KB.

## Game payloads and byte order

The game sends its C structures raw. On the wire every payload is in the
original PC layout (i386, little-endian, i386 bitfield order: the first
declared bitfield is the least significant); Linux, Windows and macOS send
and receive it unchanged. A big-endian machine (the Amiga) converts at the
`Session` boundary with `src/net/net_payload.*`: `Send` and `CreatePlayer`/
`SetPlayerData` convert native to wire, delivery to the game and the player
data of system messages convert wire to native, and the application GUID
(`int[4]`) is swapped word by word. The session protocol below is already
field by field little-endian and is not touched. The session instance GUID is
opaque bytes.

A message that is not in the catalogue is not guessed at: a big-endian host
neither sends nor delivers it, and logs it once per type. A little-endian
host passes everything. Catalogue (offsets are the i386 ones; the table in
`net_payload.cpp` is the code, `tests/net_payload_game.cpp` pins it against
the game's own senders and handlers):

| Type | Size | Layout | Conversion on a big-endian host |
| --- | --- | --- | --- |
| 0 chat | 0x101 | type, NUL-terminated text | none |
| 2 / 3 setup (host / joining player) | 0x1c4 | type, pad[3]; bitfield word @4 (gameMode:8, country:4, stage:4, rallyA:2, rallyB:2, rallyC:4, unused:8); stage states[12] @8; i32 @0x14; 100 x i32 rally pairing tables @0x18; bytes[0x14] @0x1a8; i32 @0x1bc, @0x1c0 | bitfield word re-packed field by field, every i32 swapped |
| 6 car class | 2 | type, class | none |
| 7 ready; 0xc, 0xd, 0xe, 0xf, 0x10 | 1 | type | none |
| 8 split, 9 stage time | 8 | type, index, pad[2], i32 time @4 | swap @4 |
| 0xa finish | 12 | type, pad[3], i32 time @4, i32 total @8 | swap @4, @8 |
| 0xb car state | 0x20 | type, pad, then the 0x1e byte `NetStats` @2: 15 words, of which @0x10 (height:8, heightDelta:8), @0x16 (angVelX:8, angVelY:8), @0x18 (angVelZ:8, flagB35:1, flagB54:1, progress:6), @0x1a (node:10, pad:6), @0x1c (steer:7, steerFollow, flagC00, shaking, menuOpen, stage:4, stageBehind) are bitfield words and the rest plain u16 | u16 swapped, bitfield words re-packed |
| 0x11 published leaderboard | 0x108 | type, valid, pad[2], `NetworkLeaderboard` @4 (isLoaded, reserved[3], 32 x { name[4], i32 wins }) | swap the 32 wins @12 + 8 n |
| player data (`Net_CreatePlayer`) | 0x10 | `NetPlayerInfo`: i32 id, u32 flags (integer masks), i32, i32 | swap 4 x u32 |
| player data (`Net_SetPlayerData`) | 0x10 | the same, but the game fills `flags` through the `NetPlayerFlags` bitfields (car:5, bit5, bit6, active, ready, finished, rest:22) | id and the two ints swapped, flags re-packed to the integer |
| application GUID | 16 | `int[4]` | each word swapped |

`NetStats::data[0x14]` is ten u16 words (movement, engine speed, position,
sector, suspension height, two axes, angular velocity); no field of the car
state is a float, and each machine simulates its own car, so the x86 and 68k
float results do not have to agree. The received description is read by the
game as integer masks (`flags & 0x80`), which is why both forms reach it as
the integer. A big-endian build refuses to start the network if its
compiler's bitfield layout is not the one the table models
(`PayloadLayoutSelfTest`).

Known game-side limit, outside the network layer: `NetPlayers_ResetRaceReadyAndTimeState`
clears the car state's node with the mask `& 0xfc00` while the receiver unpacks
it through the bitfield `node`, which a big-endian compiler stores in the other
end of the word. It only affects a stale value after a reset on the Amiga.

## AmigaOS

The Amiga build links ENet 1.3.18's core, unmodified, with
`src/net/enet_bsdsocket.c` in place of `unix.c` (bsdsocket.library: Roadshow,
Miami, Genesis), and `net_api_amiga.cpp` for the entry points. Requirements
and rules:

- A running TCP/IP stack with bsdsocket.library 4 or newer. Without it the
  network menu offers no connection and the log says why (`net: bsdsocket.library
  v4 is not available`).
- Settings are the `[net]` ones above except `tailscale`, which does not
  exist there. A name in `peers` is resolved by the stack's resolver, which
  blocks the game for as long as it takes, once every 30 seconds while the
  session list is open; addresses cost nothing.
- No second task: the game's task services everything. The platform layer
  calls the session's pump from the event poll and from file reads, at most
  every 50 ms, so a stage load keeps answering pings. A silence longer than
  2 s is logged (`net: N ms since the last pump`), and the longest gap is
  logged on exit (`net: stopped (longest time between pumps N ms)`); ENet drops a
  peer after 10 s of silence.
- `Open(JOIN)` waits up to 5 s for the host with the game blocked, as
  DirectPlay did.
- The sockets and the library are closed when the game exits (`AmigaSys_Shutdown`).
- Amiga host crash at race start (fixed 2026-11-12, `src/platform/amiga/amiga_qsort.c`):
  pressing Start sorts the player ids (`NetPlayers_RebuildSortedPlayerIDs`)
  with a comparator that never returns 0. libnix's `qsort` ran off the array
  when the ids were already in order, which is the case only on the host
  (its own id, machine 1, is the smaller one), and its swaps scrambled the
  program's BSS; the AHI driver then called a dead sound hook (Guru 80000004
  in "Emu68 HDMI AHI") or the machine froze. The network code was not
  involved: a trace showed the send of the setup packet completing and the
  game crashing before its next frame.

Target checks (PiStorm, Roadshow 4.106, under `tests/amiga/`): `amiga_enet_test`
(the backend over the loopback), `amiga_net_loopback_test` (two sessions with this
compiler's bitfields) and `amiga_net_peer_test`, which hosts through the real
`Net_*` entry points and trades the catalogue's messages with `tests/net_cross_peer`
on Linux:

    amiga_net_peer_test                       (Amiga)
    build/linux/tests/net_cross_peer 192.168.100.22:28620   (Linux; the Amiga's address)

Broadcast discovery only crosses one subnet; give the Amiga's address as a peer
across subnets (it is routed unicast).

## Protocol

Little-endian, version 1. The search, on the session port, outside ENet:

    query  "CMR2" 1 version nonce:u32 application:guid
    reply  "CMR2" 2 version nonce:u32 description passwordRequired:u8

Sessions, over ENet; the first byte is the opcode:

| Opcode | Direction | Contents |
| --- | --- | --- |
| JOIN | guest to host | version, instance GUID, password |
| RESUME | guest to new host | version, instance GUID, machine |
| ACCEPT | host to guest | description, host and own machine, machines, players |
| REJECT | host to guest | result (DirectPlay's error code) |
| MACHINES | host to all | machines with their addresses |
| PLAYER_ADD, PLAYER_REMOVE, PLAYER_DATA | both ways | a player; the host forwards |
| SESSION_DESC | host to all | description |
| DATA | both ways | origin machine, from, to (0 = all), payload |

## Tests

`tests/net_payload.cpp` simulates a big-endian machine byte for byte (native
images built from the field values the way a m68k compiler lays them out,
wire images the way an i386 one does, by code that shares nothing with the
converter) for every catalogued message, and fuzzes every shape round trip.
`tests/net_payload_game.cpp` drives the game's own senders and receive
handlers, linked from the game, and checks sizes, offsets and bitfield positions
against the wire layout. `net_session` also runs against the cooperative mode
(`net_session_cooperative`) and with a big-endian and a little-endian session in one
process (`CrossEndian`).

`tests/net_session.cpp` runs a host and guests on the loopback: search,
passwords, full sessions, player events, 500 guaranteed messages in order,
unguaranteed messages, messages to all, player data, session description,
buffers too small, garbage datagrams, protocol violations, a guest leaving,
host migration and a host that disappears without a word. It also passes
under ThreadSanitizer and AddressSanitizer.

`net_frontend` runs a real UDP host and guest, receiving into memory above
4 GB on 64-bit builds, and calls the game's notification handlers for joins,
player updates, host options and departures.

`tests/lan_game.py` runs two game processes through the real discovery, join
and ready menus, then checks 500 updates, two cars and movement of the remote
car on both machines. It is registered in CTest when original game data is
supplied. A Windows build can take part through Wine:

```sh
python3 tests/lan_game.py --host-probe build/windows-probe \
  --guest-probe build/linux/tests/stage_probe --data /path/to/game \
  --output build/lan-windows-host
```

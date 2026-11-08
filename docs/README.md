# Documentation

Start with the root [README](../README.md) for requirements and build presets.
These guides describe the maintained workflow and current project scope.

| Guide | Contents |
| --- | --- |
| [Architecture](ARCHITECTURE.md) | Source layout, platform APIs and renderer design. |
| [Development](DEVELOPMENT.md) | Tests, source sync, generated files and repository hygiene. |
| [Baseline capture](BASELINE.md) | Stage capture, replay and profiling. |
| [Original reference](ORIGINAL_REFERENCE.md) | Original + SilentPatch recordings and comparison workflow. |
| [Physics parity](PHYSICS_PARITY.md) | Corrected six-cadence Finland fixture, timing and coverage limits. |
| [Raw-access audit](64bit-raw-access.md) | Generated i386/x86-64 layout review and remaining unresolved accesses. |
| [Network play](NETWORK.md) | LAN, VPN and internet setup, protocol and tests. |
| [Video](VIDEO.md) | Bink decoder scope and validation. |
| [Bink on AmigaOS](BINK_AMIGA_PLAN.md) | The plan for Bink movies on the Amiga target, and the [handoff](BINK_AMIGA_HANDOFF.md) with the current state and the remaining work. |
| [AmigaOS/PiStorm port](AMIGA_PORT.md) | Target requirements, build contract, ABI boundaries and MiniGL mapping. |
| [`game/` divergence cleanup](GAME_DIVERGENCE_CLEANUP.md) | Active plan to reduce the port's edits to `game/`. |
| [Releases and Windows builds](WINDOWS.md) | Release packages and cross-building Windows from Linux. |

Frozen regression inputs and their provenance remain in
[`tests/reference/`](../tests/reference/). Local build outputs and exploratory
captures belong under the ignored `build/` directory.

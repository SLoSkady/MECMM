# MECMM — reverse engineering Frostbite 3 in Mirror's Edge Catalyst

**English** · [Русский](README.ru.md)

A research project that takes apart **Mirror's Edge Catalyst** (PC, Frostbite 3) from the running
process: where the game keeps the player and the camera, how it renders depth, how the engine
describes its own classes, and how its runner-ghost system works. The findings are turned into a
reusable toolkit and a working **proof-of-concept multiplayer mod**: players connect through a small
relay server and see each other as 3D figures in the city, hidden correctly behind walls.

<p align="center">
  <img src="docs/research/screenshots/ghost3.png" width="49%" alt="A remote player behind a box: the hidden part is a silhouette">
  <img src="docs/research/screenshots/depth3.png" width="49%" alt="The game's scene depth buffer, found through D3D11 hooks">
</p>

## Results

| Area | Result |
|---|---|
| Player | Restart-proof pointer path to Faith's transform: `[exe+0x23DA020]+0x700` ([notes](docs/research/02-player-position.md)) |
| Camera | Render view with view and projection matrices: `[exe+0x2563A10]` (+0x630, +0x6F0); world-to-screen math ([notes](docs/research/03-camera.md)) |
| Rendering | Scene depth buffer found through `ID3D11DeviceContext` hooks; remote players drawn as 3D figures occluded by the scene ([notes](docs/research/04-depth-buffer.md)) |
| Engine types | **SDK dumper**: reads Frostbite's reflection data, ~10 000 classes/structs/enums with fields and offsets ([notes](docs/research/05-type-info-sdk.md)) |
| Ghost system | Mapped how the game records and replays runners as animated ghosts (SDK + Frosty blueprints + live objects) ([notes](docs/research/06-replay-ghost-system.md)) |
| Network | UDP protocol, relay server, interpolation, test bot ([notes](docs/research/07-network.md)) |

All notes: [docs/research/](docs/research/README.md).

## Toolkit

PowerShell + C# tools that work on the running game (no installs; the game must be running).
Memory is streamed in chunks, so they work where Cheat Engine runs out of memory on this 12 GB process.

| Tool | Purpose |
|---|---|
| `tools/sdk/sdkdump.ps1` | **SDK dumper**: all engine classes, structs and enums with fields and offsets → `tools/output/sdk/` |
| `tools/sdk/instances.ps1` | Find live objects of an engine class (via GetType() in vtable slot 0) |
| `tools/sdk/inspect.ps1` | Show a live object with every pointer resolved to a class name |
| `tools/sdk/fbhash.ps1` | Decode Frostbite name hashes (event / field ids in blueprints) |
| `tools/memory/heightscan.ps1` | Cheat Engine–style value scan (increased / decreased / unchanged) over all memory |
| `tools/memory/posscan.ps1` | Find x,y,z float triples and filter them by movement |
| `tools/memory/matscan.ps1` | Find 4x4 transform matrices near a point, filter by rotation |
| `tools/memory/memwatch.ps1` | Record values to CSV while the game window is focused |
| `tools/memory/ptrfind.ps1`, `ptrchain.ps1` | One-level and multi-level pointer search back to static `.exe` locations |
| `tools/memory/strscan.ps1`, `findbytes.ps1` | Find strings, pointers or immediates in memory / code |
| `tools/memory/showpos.ps1` | Print the player's position through the found pointer path |
| `tools/disasm/disasm.ps1` | Disassemble live (decrypted) game code (Iced) |

## Proof-of-concept mod

A `dinput8.dll` loaded by the game ([how](docs/research/01-loading-and-overlay.md)) with:
- an ImGui overlay: own position, connection status, players and distances (**F7**), depth debug (**F8**);
- networking: position and heading 30×/s to a relay server, interpolated remote players;
- remote players as 3D figures (body, head, "nose" for the heading, colour per player), occluded by
  the scene and shown as a silhouette behind walls.

It is a demonstrator, not a finished mod: no animations, no collision, no menu. For a complete
multiplayer mod built on the game's own ghost characters see [MECMP](#related-work).

### Build and run

Requirements: Windows 10/11 x64, Mirror's Edge Catalyst (Steam), Visual Studio 2022 with *Desktop
development with C++*, Git.

```bat
git clone --recursive https://github.com/SLoSkady/MECMM.git
cd MECMM
scripts\build.bat                      :: build\Release\dinput8.dll, mecmm-server.exe, mecmm-bot.exe
scripts\install.bat "D:\path\to\Mirrors Edge Catalyst"     :: game closed
scripts\run-server.bat                 :: relay server, UDP 47777
scripts\run-bot.bat                    :: optional: a bot that circles around you
```

Start the game **with the internet disconnected** (EA's servers are gone; online the game hangs after
the intro), reconnect once in game. The mod creates `mecmm.ini` next to the game: server address,
port, nickname, hotkeys. `scripts\uninstall.bat` restores the vanilla game.

## Project structure

```
MECMM/
├── src/                    proof-of-concept mod (C++20) → dinput8.dll
│   ├── core/               entry point, dinput8 proxy, config, network session, logging
│   ├── game/               game data: player position and heading, camera, world-to-screen
│   ├── net/                wire protocol + UDP client (shared with server and bot)
│   ├── overlay/            D3D11 Present hook + ImGui UI
│   └── render/             scene depth discovery, 3D figures of remote players
├── server/                 relay/ (mecmm-server.exe), bot/ (mecmm-bot.exe)
├── tools/                  research toolkit
│   ├── memory/             memory scanners, pointer search, viewers
│   ├── sdk/                SDK dumper, live object finder/inspector, hash decoder
│   ├── disasm/             disassembler for live game code
│   ├── lib/                Iced disassembler library
│   └── output/             generated data (scans, SDK dump; not committed)
├── docs/research/          research notes (7 chapters), screenshots, Cheat Engine table
├── scripts/                build, install / uninstall, run server / bot
└── external/               Dear ImGui, MinHook (git submodules)
```

## How this was made

Developed by SLoSkady using AI tools (Claude Code) and reverse-engineering tools (Cheat Engine,
Frosty Editor): research of the game, task definition, choice of approach, testing and documentation.
Every step, including the dead ends, is described in the [notes](docs/research/README.md).

## Related work

- **[MECMP](https://github.com/CodeNameMeteor/mecmp)** (MIT): an independent, complete multiplayer mod
  for MEC that shows remote players as animated ghosts using the game's replay system. Our findings
  on that system ([chapter 6](docs/research/06-replay-ghost-system.md)) were reached independently and
  agree with its documentation.
- [Frosty Tool Suite](https://github.com/CadeEvs/FrostyToolsuite): used to read the game's blueprints.

## Legal

An **unofficial, non-commercial fan project**, not affiliated with or endorsed by Electronic Arts or
DICE. *Mirror's Edge* is a trademark of Electronic Arts Inc. The repository contains no game files or
assets; generated dumps stay local. The tools need a legally purchased copy and do not bypass its
copy protection.

## License

[MIT](LICENSE) © 2026 SLoSkady.
Third-party: [Dear ImGui](https://github.com/ocornut/imgui) (MIT),
[MinHook](https://github.com/TsudaKageyu/minhook) (BSD-2-Clause),
[Iced](https://github.com/icedland/iced) (MIT).

# MECMM — Mirror's Edge Catalyst Multiplayer Mod

**English** · [Русский](README.ru.md)

An unofficial, work-in-progress multiplayer mod for **Mirror's Edge Catalyst** (PC, Steam).
The goal: see other runners in your game in real time — race them, free-run together, play tag.

> [!WARNING]
> Early development. There is **no multiplayer yet** — the mod currently shows the player's
> live position in an in-game overlay. See [Roadmap](#roadmap).

## Status

| Stage | State |
|---|---|
| Find the player's position in memory (restart-stable) | ✅ Done |
| Mod DLL loads into the game (`dinput8.dll` proxy) | ✅ Done |
| In-game overlay (ImGui) with live coordinates | ✅ Done |
| Networking: relay server + client in the mod | ⏳ Next |
| Remote players drawn as markers ("ghosts") | 🔜 Planned |
| Remote players as 3D characters with animations | 🔜 Planned |
| Game modes: races, co-op free-run, tag | 🔜 Planned |

## Requirements

- Windows 10/11 x64
- Mirror's Edge Catalyst, **Steam** version (other versions are untested)
- To build: Visual Studio 2022 with **Desktop development with C++** (includes CMake), Git

## Installation

1. Build the mod (see [Building](#building)) — or take `dinput8.dll` from a release once available.
2. **Close the game** and run:
   ```bat
   scripts\install.bat "D:\path\to\Mirrors Edge Catalyst"
   ```
   (without an argument the script uses `E:\SteamLibrary\steamapps\common\Mirrors Edge Catalyst`)
3. Start the game. A small **MECMM** window appears in the top-left corner.
   **Insert** shows/hides it.

To remove the mod, close the game and run `scripts\uninstall.bat` — the game is back to vanilla.

> [!NOTE]
> EA shut down the game's online servers in December 2023. If the game hangs on an endless
> loading screen after the intro, start it with the internet **disconnected**, then reconnect
> once you are in the game.

## Building

```bat
git clone --recursive https://github.com/SLoSkady/MECMM.git
cd MECMM
scripts\build.bat
```

The result is `build\Release\dinput8.dll`. Dependencies (ImGui, MinHook) are git submodules
and are fetched automatically.

## How it works

1. The game loads `dinput8.dll` from its own folder. Ours forwards every DirectInput call to
   the real system `dinput8.dll`, so input keeps working, and starts the mod on a separate thread.
2. The mod hooks `IDXGISwapChain::Present` (MinHook) and draws its UI with Dear ImGui on top of
   every frame.
3. The player's position is read through a pointer path found by reverse engineering:
   `[MirrorsEdgeCatalyst.exe + 0x23DA020] + 0x730`. Details in
   [docs/research/findings.md](docs/research/findings.md).

## Project structure

```
MECMM/
├── src/                    mod source code (C++20), built into dinput8.dll
│   ├── core/               entry point, dinput8 proxy, logging
│   ├── game/               access to game data (player position)
│   └── overlay/            D3D11 Present hook + ImGui UI
├── external/               third-party libraries (git submodules)
│   ├── imgui/              Dear ImGui v1.91.5
│   └── minhook/            MinHook v1.3.3
├── scripts/                build.bat, install.bat, uninstall.bat
├── tools/                  reverse-engineering tools (PowerShell)
│   ├── memory/             memory scanners, pointer finder, position viewer
│   ├── disasm/             disassembler for live (decrypted) game code
│   ├── lib/                Iced disassembler library
│   └── output/             raw scan data (not committed)
└── docs/
    └── research/           findings, Cheat Engine tables, screenshots
```

## Research tools

All tools read the running game's memory and need no extra installs.

| Tool | Purpose |
|---|---|
| `tools/memory/showpos.ps1` | Print the player's position via the known pointer path (`-Watch` for live) |
| `tools/memory/heightscan.ps1` | Cheat Engine–style value scan (increased / decreased / unchanged) over **all** memory, streaming — works where CE runs out of memory |
| `tools/memory/posscan.ps1` | Find x,y,z float triples and filter them by movement |
| `tools/memory/memwatch.ps1` | Record values to CSV while the game window is focused |
| `tools/memory/ptrfind.ps1` | Find pointers to an object; marks static ones inside the `.exe` |
| `tools/disasm/disasm.ps1` | Disassemble live game code around an address |

## Roadmap

- [x] Locate the player's position (restart-stable pointer path)
- [x] `dinput8.dll` proxy loader
- [x] ImGui overlay with live coordinates
- [ ] Verify the pointer path in the open world
- [ ] Player rotation and camera (view matrix) for drawing remote players
- [ ] UDP relay server + client (send/receive positions 20–30 times per second, interpolation)
- [ ] Test bot that simulates a remote player
- [ ] Remote players drawn as on-screen markers
- [ ] Remote players as in-world characters, then animations
- [ ] Game modes: races on time-trial routes, co-op free-run, tag
- [ ] Auto-fix for the endless loading screen (offline mode)

## Legal

This is an **unofficial, non-commercial fan project**. It is not affiliated with, endorsed by,
or sponsored by Electronic Arts or DICE. *Mirror's Edge* is a trademark of Electronic Arts Inc.

- The repository contains **no game files or assets** — only original code.
- The mod requires a **legally purchased copy** of the game and does not bypass its copy protection.

## License

[MIT](LICENSE) © 2026 SLoSkady.
Third-party: [Dear ImGui](https://github.com/ocornut/imgui) (MIT),
[MinHook](https://github.com/TsudaKageyu/minhook) (BSD-2-Clause),
[Iced](https://github.com/icedland/iced) (MIT).

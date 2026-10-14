# Research notes

Reverse-engineering notes on **Mirror's Edge Catalyst** (PC, Steam, x64, Frostbite 3).
Addresses are for the Steam build of October 2026; a game update may change them.

| # | Chapter | What it covers |
|---|---|---|
| 1 | [Running code inside the game](01-loading-and-overlay.md) | `dinput8.dll` proxy loader, `Present` hook, ImGui overlay |
| 2 | [The player's position](02-player-position.md) | coordinate system, a restart-proof pointer, the search method, dead ends |
| 3 | [The camera](03-camera.md) | view and projection matrices, world-to-screen math |
| 4 | [Scene depth and occlusion](04-depth-buffer.md) | finding the depth buffer through D3D11 hooks, occluded 3D figures |
| 5 | [Engine type information (SDK)](05-type-info-sdk.md) | Frostbite's reflection data: ~10 000 classes, structs and enums |
| 6 | [Replay / ghost system](06-replay-ghost-system.md) | how the game records and plays back runners as animated ghosts |
| 7 | [Network prototype](07-network.md) | UDP protocol, relay server, interpolation, test bot |

Approach in one line: **data first, then code.** Find a value by watching how it changes, confirm it
from several sides, then find a stable path to it; once the engine's own type information was readable,
names replaced guesswork.

Screenshots of each step are in [screenshots/](screenshots/), a Cheat Engine table from the first
search in [tables/](tables/).

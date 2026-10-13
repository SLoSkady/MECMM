# Research findings

Reverse-engineering notes for Mirror's Edge Catalyst (Steam version, x64, Frostbite 3).
Addresses are for the Steam build installed in October 2026; a game update may change them.

## Environment

| Item | Value |
|---|---|
| Executable | `MirrorsEdgeCatalyst.exe`, image base `0x140000000` |
| Protection | EA DRM, code is encrypted on disk (sections `.ooa`, `.xcode`, `.xtext`). Analyse the **running** process, not the file. |
| Renderer | Direct3D 11 (`d3d11.dll`, `dxgi.dll`) |
| Proxy DLL | `dinput8.dll` is loaded from the game folder (not a KnownDLL) |
| Debugger | Cheat Engine **VEH debugger** works without crashing the game |
| Online | EA servers shut down in Dec 2023: start the game with the internet disconnected to avoid the infinite loading screen after the intro |

## Coordinate system

- World space, **Y is up**, units are metres.
- Object transforms are stored as `LinearTransform` (4x4 float, 0x40 bytes):
  rows `right`, `up`, `forward`, `translation` (translation x,y,z at `+0x30`).
- Two transforms describe the player:
  - **character root** — `up` row is exactly `(0, 1, 0)`, translation at the feet;
  - **camera** — about 1.54 m above the root, rotated with the view.

## Player position

```
[MirrorsEdgeCatalyst.exe + 0x23DA020] + 0x730  ->  float x, y, z   (character root)
```

- The full root transform is at `+0x700` (forward row at `+0x720`); the mod derives the
  heading as `yaw = atan2(forward.x, forward.z)`. The sign/zero direction of yaw is not yet
  verified visually.
- `exe + 0x2577238` holds the same pointer.
- Verified across a game restart in the tutorial area.
- The pointed-to object looks like an array of 0x40-byte transforms (the player's is the
  matrix at `+0x700`, other entities follow), so the index may differ in other levels or
  the open world. **Not yet verified in the open world.**

## Dead ends (so nobody repeats them)

- Searching the "height" with Cheat Engine finds values around 35.4 that react to climbing.
  These live on a **thread stack** (`0x089xxxxx`) and in per-frame render buffers; they are
  not world coordinates and flicker to 0 / garbage.
- Cheat Engine "Unknown initial value" fails with *Failure allocating memory for copy*:
  the game uses ~12 GB. Use the streaming scanners in `tools/memory/` instead.
- Searching contiguous x,y,z triples with the wrong height range finds level geometry
  (box corners), not the player.

## Method that worked

1. Search float triples with a plausible range around the player (`tools/memory/posscan.ps1`).
2. Keep triples that move together with the player (run on flat ground, climb a box).
3. Keep only those that are part of an orthonormal 4x4 matrix with `up = (0,1,0)`.
4. Record the survivors while playing (`memwatch.ps1`) and keep the ones that track 100 %.
5. Cheat Engine "Find out what writes to this address" on a survivor showed a `memcpy`
   from the original transform.
6. `tools/memory/ptrfind.ps1` on the original found static pointers inside the `.exe`.

## Screenshots

- `screenshots/ce1.png` — Cheat Engine out-of-memory on "Unknown initial value".
- `screenshots/ce3.png` — writers to a stack address (dead end).
- `screenshots/ce4.png` — `memcpy` copying the player transform (source = original).

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
| Saves | `Documents\Mirrors Edge Catalyst\settings\PROF_SAVE*` (shared by all versions of the game). A completed save downloaded from the internet (made by another player in 2016) was **not accepted**: the game started from the prologue and overwrote it. To test in the open world, play through the prologue. |

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
- Verified across game restarts, in the tutorial area **and in the open world**.
- The pointed-to object looks like an array of 0x40-byte transforms (the player's is the
  matrix at `+0x700`, other entities follow). The index was the same in both places tested;
  other story missions are not tested yet.

## Camera (render view)

```
[MirrorsEdgeCatalyst.exe + 0x2563A10] -> render view object of the main camera
    +0x630  view matrix        (world -> view), 4x4 float, row-vector convention
    +0x6B0  camera transform   (rows right, up, back, position) = inverse of the view matrix
    +0x6F0  projection matrix  reversed-Z infinite perspective
```

- Verified across game restarts, in the tutorial and in the open world (markers drawn with it
  line up with the world).
- `clip = (x, y, z, 1) * view * projection`; the camera looks down **-Z** in view space,
  so the transform's third row points *backwards* (opposite to the view direction).
- Projection at 1024x768: `[0][0] = 0.9757`, `[1][1] = 1.3009` → vertical FOV 75°, aspect 4:3;
  `[2][3] = -1`, `[3][2] = 0.06` (near plane 0.06 m, no far plane).
- The same object holds more matrices (another view/projection copy at `+0x730`/`+0x770`,
  a combined matrix around `+0x7F0`), not needed so far.
- Found with `tools/memory/matscan.ps1` (transform matrices near the head, filtered by turning
  the camera) and `tools/memory/ptrchain.ps1` (multi-level pointer search).

## Scene depth buffer

Found by hooking `ID3D11DeviceContext::OMSetRenderTargets` (vtable 33) and
`ClearDepthStencilView` (vtable 53) and counting depth-stencil views per frame
(`src/render/depth.cpp`, debug window on F8/Home).

| Buffer (1024x768 output, render quality 50 %) | Format | Cleared to | Meaning |
|---|---|---|---|
| 512x384 (= render resolution) | `R32G8X24_TYPELESS` | 0 | **main scene depth**, reversed-Z, ~50 binds/frame |
| 256x192 | `R32G8X24_TYPELESS` | 0 | downsampled copy (effects) |
| 700x700 | `R16_TYPELESS` | 0 | shadows |
| 1024x768 (= output resolution) | `R32G8X24_TYPELESS` | 1 (or not cleared) | UI/post; empty at Present |

- The scene depth is rendered at **render resolution**, not output resolution, so pick it by
  aspect ratio + "cleared to 0" rather than by size.
- All have `BIND_SHADER_RESOURCE`; read them through an SRV with `R32_FLOAT_X8X24_TYPELESS`.
- The scene depth is still intact at `Present`, so the mod samples it there to occlude the
  3D figures (`depth = 0.06 / view distance`, larger = closer).
- `screenshots/depth3.png` shows the picked buffer in the debug window.

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
- `screenshots/ghost1.png` — the test bot drawn as a marker in the world (tutorial).
- `screenshots/ghost2.png` — the same in the open world, on a rooftop.
- `screenshots/ghost3.png` — 3D figure behind a box: hidden part drawn as a faint silhouette.

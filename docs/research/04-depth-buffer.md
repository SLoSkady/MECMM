# 4. Scene depth and occluded 3D figures

**Goal:** draw remote players as 3D figures that are hidden by walls, without access to the game's
renderer internals.

## Finding the depth buffer

Hooks on `ID3D11DeviceContext::OMSetRenderTargets` (vtable slot 33) and `ClearDepthStencilView`
(slot 53) count, per frame, which depth-stencil views the game binds and clears and to which value
(`src/render/depth.cpp`). A debug window (F8) lists them and previews the picked one.

| Buffer (1024x768 output, render quality 50 %) | Format | Cleared to | Meaning |
|---|---|---|---|
| 512x384 (= render resolution) | `R32G8X24_TYPELESS` | 0 | **scene depth**, reversed-Z, ~50 binds/frame |
| 256x192 | `R32G8X24_TYPELESS` | 0 | downsampled copy (effects) |
| 700x700 | `R16_TYPELESS` | 0 | shadows |
| 1024x768 (= output resolution) | `R32G8X24_TYPELESS` | 1 / not cleared | UI/post, empty at `Present` |

Lessons:
- The scene is rendered at **render resolution**, not output resolution ("render quality" setting),
  so the buffer is chosen by **aspect ratio + cleared to 0** (reversed-Z, like the camera), not by size.
  The first, size-based guess picked the UI buffer and showed a white image.
- All buffers have `BIND_SHADER_RESOURCE`; they are read through an SRV in format
  `R32_FLOAT_X8X24_TYPELESS`.
- The scene depth is still intact at `Present`, so it can be sampled there.

Screenshots: `screenshots/depth2.png` (wrong pick, white), `depth3.png` (the scene depth).

## Drawing occluded figures

`src/render/ghosts.cpp` renders a capsule body, a head and a "nose" (heading) per remote player with
its own shaders, using the game's `view * projection`:

- **Own depth buffer** (D32, cleared to 0, test `GREATER`) so the figure's parts occlude each other.
- In the pixel shader, the scene depth at the same screen position (scaled to the depth buffer's
  resolution with `Load`) is compared with the figure's depth: if the scene is closer
  (`scene > ours * 1.01`, reversed-Z), the pixel is drawn as a faint silhouette, otherwise solid.
- Remote players are drawn **100 ms in the past**, interpolated between received states.

Screenshots: `screenshots/ghost3.png` (figure behind a box), `ghost4.png` (behind a wall).

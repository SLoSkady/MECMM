# 3. The camera: from world to screen

**Goal:** turn a world position into a pixel on screen (to draw other players).

**Result:** the main camera's render view:

```
[MirrorsEdgeCatalyst.exe + 0x2563A10] -> render view
    +0x630  view matrix        world -> view, 4x4 float
    +0x6B0  camera transform   rows right, up, back, position (inverse of the view matrix)
    +0x6F0  projection matrix  reversed-Z, infinite far plane
```

Verified across restarts, in the tutorial and in the open world.

## Math

Matrices are row-major with the **row-vector** convention (DirectX):

```
clip = (x, y, z, 1) * view * projection
ndc  = clip.xy / clip.w
px   = (ndc.x * 0.5 + 0.5) * width,   py = (0.5 - ndc.y * 0.5) * height
```

The camera looks down **-Z** in view space, so the transform's third row points *backwards*.
Check that the view matrix is right: the camera position multiplied by it gives `(0, 0, 0)`.

Projection at 1024x768: `[0][0] = 0.9757`, `[1][1] = 1.3009` → vertical FOV `2·atan(1/1.3009) = 75°`,
aspect `1.3009 / 0.9757 = 4:3`; `[2][3] = -1`, `[3][2] = 0.06` → near plane 0.06 m, no far plane, and
depth `z/w = 0.06 / distance` (1 near, 0 at infinity: **reversed Z**).

The object holds more matrices (another view/projection pair at `+0x730`/`+0x770`, a combined matrix
around `+0x7F0`); not needed.

## Method

1. `tools/memory/matscan.ps1`: find 4x4 transforms whose translation is ~1.5 m above the player
   (48 hits), then keep those whose forward vector **turned** after the camera was rotated (38).
2. `tools/memory/ptrchain.ps1`: a multi-level pointer search (like Cheat Engine's pointer scan, but
   streaming) back from all 22 copies to static locations in the `.exe` → 331 paths; the shortest is
   one step: `[exe+0x2563A10] + 0x6B0`.
3. Dumping that object showed the view and projection matrices next to the transform.

Screenshots: `screenshots/ghost1.png` (tutorial), `ghost2.png` (open world): markers placed with
these matrices stand where the other player is.

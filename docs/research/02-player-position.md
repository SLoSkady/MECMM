# 2. Finding the player's position

**Goal:** a restart-proof way to read where Faith is.

**Result:**

```
[MirrorsEdgeCatalyst.exe + 0x23DA020] + 0x730  ->  float x, y, z   (character root, feet)
[MirrorsEdgeCatalyst.exe + 0x23DA020] + 0x700  ->  4x4 transform of the character root
```

Verified across game restarts, in the tutorial and in the open world. `exe + 0x2577238` holds the
same pointer. The object looks like an array of 0x40-byte transforms (the player's at `+0x700`, other
entities after it); the index was the same everywhere we tested.

## Coordinate system

- World space, **Y is up**, metres.
- Transforms are `LinearTransform` (16 floats, 0x40 bytes): rows `right`, `up`, `forward`, `trans`
  (translation at `+0x30`). The SDK later confirmed the layout ([chapter 5](05-type-info-sdk.md)).
- The player has two transforms: the **character root** (`up` is exactly `(0, 1, 0)`, translation at
  the feet) and the **camera** (~1.54 m higher, rotated with the view).
- Heading: `yaw = atan2(forward.x, forward.z)`.

## Method that worked

1. Search **float triples** (x, y, z in a row) in a plausible range around the player
   (`tools/memory/posscan.ps1`, streams the whole address space instead of snapshotting it).
2. Keep triples that **move with the player**: run on flat ground (y unchanged, x/z changed), climb a
   box (y up). 257 000 candidates → a few thousand.
3. Keep only triples that are the translation of an **orthonormal 4x4 matrix with `up = (0,1,0)`**
   → 72 candidates.
4. Record them for 45 s of play (`memwatch.ps1`) and keep those matching the median every sample
   → ~40 "100 %" copies.
5. Cheat Engine *Find out what writes to this address* on one copy: a `memcpy` from another
   address, the **original**.
6. Search for pointers to the original's object (`ptrfind.ps1`): two of them live **inside the
   `.exe`** (static data), so they exist at the same place after every restart.

## Dead ends

- Cheat Engine *Unknown initial value* fails (*Failure allocating memory for copy*): the game uses
  ~12 GB and CE tries to copy it all. Our scanners stream memory in 16 MB chunks instead.
- A "height" value found with classic increased/decreased scans (35.4 on the floor, 36.4 on a box)
  was not a world coordinate: it lived on a **thread stack** (`0x089xxxxx`) and in per-frame buffers,
  flickering to 0 and garbage. The real world height was ~64.7.
- Searching triples with the wrong height range found level geometry (box corners) instead.

Screenshots: `screenshots/ce1.png` (CE out of memory), `ce3.png` (stack dead end),
`ce4.png` (the `memcpy` that led to the original).

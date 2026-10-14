# 5. Reading the engine's type information (SDK dumper)

**Goal:** instead of guessing memory layouts, read the engine's own description of its classes.

Frostbite keeps reflection data ("TypeInfo") for every engine type in the `.exe`: name, size, base
class, and every field with its name, type and offset. `tools/sdk/sdkdump.ps1` reads it from the
running game and writes ~10 000 types to `tools/output/sdk/`: `classes.txt` (4 119 classes),
`valuetypes.txt` (1 407 structs), `enums.txt` (592 enums), `index.csv`. The output is generated from
the user's own copy and is not committed.

Example output:

```
class PamReplayPlayerComponentData : GameComponentData  // size 0x90, module Pamplona
{
    /* 0x0070 */ BoneNameId[]                     RecordingBones;
    /* 0x0078 */ BoneNameId[]                     TranslationBones;
    /* 0x0080 */ SkeletonAsset                    SkeletonAsset;
    /* 0x0088 */ Float32                          TimeScale;
}
```

## Layout (Mirror's Edge Catalyst)

```
TypeInfo   +0x00 info*  +0x08 next TypeInfo*   (one global linked list, ~10k entries)
info       +0x00 name   +0x08 u16 flags (kind = flags >> 4 & 0x1F)  +0x0A u16 size
           +0x10 module +0x18 u8 alignment  +0x1A u16 field count
  class    +0x20 super TypeInfo*   +0x30 FieldInfo[]
  struct   +0x28 FieldInfo[]
  enum     +0x20 FieldInfo[]  (value: u32 at +0x10)
  array    +0x20 element TypeInfo*  (empty name)
FieldInfo  0x18 bytes: +0x00 name, +0x08 u16 flags, +0x0A u16 offset, +0x10 TypeInfo*
```

Kinds: 0 Void, 1 DbObject, 2 ValueType, 3 Class, 4 Array, 6 String, 7 CString, 8 Enum, 9 FileRef,
10 Boolean, 11–18 Int8..Uint64, 19 Float32, 20 Float64, 21 Guid, 22 SHA1, 23 ResourceRef.
The game's own module is **Pamplona** (MEC's codename; hence the `Pam` prefix).

## How the layout was found

1. A class name (`PamClientReplayEntity`) was found as a string in the `.exe`; exactly one pointer to
   it → the class's info block; one pointer to that → the TypeInfo. Its `+0x08` pointed to another
   class's TypeInfo: a **linked list**.
2. A collector (`tools/sdk/TypeInfo.cs`) checks every 8-byte slot of the image for "pointer to info →
   pointer to a name" and keeps those whose `next` chain is valid for 4+ steps.
3. Every other node at first failed the check: **array types have an empty name**. Allowing empty
   names joined the chain to 9 988 types, and the kind numbers matched Frostbite's basic types exactly
   (`Boolean`, `Float32`, `Guid`, ... appear once each).
4. Field arrays were located per kind by dumping known types (`Vec3` → x, y, z; `LinearTransform` →
   right, up, forward, trans).

## Live objects

- **GetType() is vtable slot 0** and is always `lea rax, [rip+TypeInfo]; ret`. So:
  - `tools/sdk/instances.ps1 -Class X` finds the GetType function returning X's TypeInfo, the vtables
    pointing to it, and every object starting with such a vtable;
  - `tools/sdk/inspect.ps1 -Address A` shows an object with every pointer resolved to the class of
    the object it points to.
- **Name hashes**: blueprint event/field ids are Frostbite string hashes (`h = 5381; h = h*33 ^ c`).
  `tools/sdk/fbhash.ps1` decodes them against all ~16 000 field and type names from the SDK.

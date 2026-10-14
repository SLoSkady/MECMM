# 6. The game's replay / ghost system

**Goal:** show remote players as the real, animated Faith instead of a capsule. MEC already shows
animated "ghosts" of other players' time-trial runs; this chapter maps that system.

## Summary

- A ghost is a **character** (`Characters/Projection/ProjectionCharacter`, the holographic Faith)
  spawned through the normal character spawn system.
- A **replay component** on it plays back **bone poses** (23 bones, translation + rotation each),
  not pre-made animations. A twin **recording component** on the local player records them.
- The online part (EA's backend: upload own runs, download others') is gone, which is why ghosts no
  longer appear. Everything else (model, animation playback, ghost shader, 7 ghost colours) is intact.

## From type names (`tools/memory/strscan.ps1`)

- Recording/playback: `PamReplayRecordingComponent(Data)`, `PamReplayPlayerComponent(Data)`,
  `PamClientReplayEntity`, `PamReplayEntityData`, `PamReplayAsset`, `PamReplayResource`.
- Data format: `NetworkReplayKeyframe`, `NetworkReplayTrackData`.
- Visuals: `PamUseRunnersGhostShader`, `PamGhostInRvColorBurnPass`.
- Dead online part: `PamGatewayBackendGetPlayerGhostsRequestMessage`, `...SetPlayerGhost...`,
  `...DownloadReplay...`.
- A time trial loads extra assets absent in free roam: `UI_Faith3p_Idle_Replay` (third-person Faith
  for replays), `SpawnTemplate_Projection_Replay`, `ReplayMarker`, `ReplayUnavailable`, ...

## From the SDK ([chapter 5](05-type-info-sdk.md))

```
class PamReplayPlayerComponentData : GameComponentData      // playback
    0x70 BoneNameId[] RecordingBones;  0x78 BoneNameId[] TranslationBones;
    0x80 SkeletonAsset SkeletonAsset;  0x88 Float32 TimeScale;
class PamReplayRecordingComponentData : GameComponentData   // recording
    0x70 BoneNameId TrailBone;  0x78 BoneNameId[] RecordingBones;  0x80 BoneNameId[] TranslationBones;
```

## From blueprints (Frosty Editor → EBX to XML)

Assets: `Gameplay/Movement/PF_Replay`, `Gameplay/Social/PF_SkillReplayMarkerWithAutoPlay`,
`Characters/SpawnTemplates/Projection/SpawnTemplate_Projection_Replay`,
`Characters/Projection/FX_SS_Projection_Ghost`, `FX/UX/ProjectionMesh/FX_UX_ProjectionMesh_Trail_M`,
ghost colours `Characters/Customizations/Projection/Ghost/Customizations/Customization_Projection_Ghost_{Black,Blue,Green,Orange,Red,Teal,Yellow}`.

- `CharacterSpawnReferenceObjectData` with blueprint `ProjectionCharacter`, template
  `SpawnTemplate_Projection_Replay` (look `Customization_Projection_RunVis`), Team2, max 1, no auto
  spawn.
- `PF_Replay` wiring, ids decoded with `fbhash.ps1`:
  ```
  CharacterSpawnReference --OnSpawned (0xc2311cde)--> PamReplayEntity     character is ready
  (interface) --StartReplay (0x2734a476) / StopReplay--> PamReplayEntity  start / stop playback
  input triggers --StartPreview / StopPreview--> PamReplayEntity
  PamReplayEntity --OnReplayDone (0x206220f7)--> ...
  exposed: ControllableTransform, Visible, TimeScale of the spawned character
  ```

## Live objects in free roam (`instances.ps1`, `inspect.ps1`)

```
PamClientReplayEntity (+0x28 PamReplayEntityData, +0x50 PamReplayResource = a loaded replay)
  └ PamClientReplayPlayerComponent (+0x10 data, +0x1C8 ClientAntAnimatableEntity, +0x2E8 owner)
       └ ClientAntAnimatableEntity (0x760 bytes): the animated body, idle and not rendered
```
plus one `PamClientReplayRecordingComponent` (on the local player).

## Status and related work

The research stopped at this point: the path to animated remote players is mapped (spawn a
`ProjectionCharacter`, stop its own playback, write streamed poses into its replay component), but not
implemented here.

The independent open-source mod **[MECMP](https://github.com/CodeNameMeteor/mecmp)** (MIT) implements
exactly this approach and documents it in its `docs/ARCHITECTURE.md`: poses are taken from the local
player's recording component (it keeps the last ~15 s for the "create a time trial" feature) and
written into ghosts spawned from the level's ghost spawner. Our findings above were reached
independently and agree with theirs.

Frostbite's own network replication also exists (`GhostManager`, `ServerGhostManager`; here "ghost"
means a replicated network object): the engine runs as client + server even in single player.

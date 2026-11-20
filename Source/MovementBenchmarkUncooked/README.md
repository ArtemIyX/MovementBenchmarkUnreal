# Entity director performance test

`UEntityDirectorPerfTestController` uses the AutomatedPerfTesting Gauntlet controller base. It expects exactly one placed `AEntityDirector` in the loaded level, calls `Start()`, waits for the FLECS worker to publish a complete snapshot, then warms up and captures the movement system.

Run with the editor target so the `MovementBenchmarkUncooked` module is loaded:

```text
UnrealEditor.exe MovementBenchmark.uproject /Game/Maps/MAP_DebugECS_GameThread_1024 -game -traceautostart=0 -gauntlet=EntityDirectorPerfTest -AutomatedPerfTest.DoCSVProfiler -AutomatedPerfTest.TestID=EntityDirectorMovement -EntityDirectorPerfTest.WarmupSeconds=5 -EntityDirectorPerfTest.CaptureSeconds=30
```

The raw CSV contains fixed-step movement work from the `FlecsSimulation` thread under `STAT_EntityDirectorMovementSystem` and accumulated `MovingEntities` samples. CSV rows use game-frame boundaries, so a row can contain work from zero, one, or several worker steps. The sibling `_Summary.csv` reports the simulation thread, average and maximum system time per game frame, average moving-entity samples per game frame, total entities, and average microseconds per moving-entity sample.

`AverageUsPerMovingEntitySample` divides total system time in microseconds by total moving-entity samples. The field is empty when there are no moving samples. `WarmupSeconds`, `CaptureSeconds`, and `StartupTimeoutSeconds` default to 5, 30, and 10 seconds.

Because this controller is in an `UncookedOnly` module, it is intended for editor-target runs and is not included in cooked game builds.

# Entity sync performance test

`UEntitySyncerPerfTestController` expects exactly one placed `AEntityDirector`, `AEntitySyncer`, and `ASimpleSpawner`. The spawner's actor class must implement `IActorTransformSyncInterface`, the actor and entity counts must match, and syncer debug drawing must be disabled. The controller waits for spawned actors and a complete worker snapshot, pairs them in spawn order, and starts warmup once the syncer applies its first snapshot.

Run against the syncer test map with the editor target:

```text
UnrealEditor.exe MovementBenchmark.uproject /Game/Maps/MAP_Syncer_256 -game -traceautostart=0 -gauntlet=EntitySyncerPerfTest -AutomatedPerfTest.DoCSVProfiler -AutomatedPerfTest.TestID=EntitySyncerTransformSync -EntitySyncerPerfTest.WarmupSeconds=5 -EntitySyncerPerfTest.CaptureSeconds=30 -EntitySyncerPerfTest.SpawnTimeoutSeconds=60
```

For the 1,024-actor map:

```text
UnrealEditor.exe MovementBenchmark.uproject /Game/Maps/MAP_Syncer_1024 -game -traceautostart=0 -gauntlet=EntitySyncerPerfTest -AutomatedPerfTest.DoCSVProfiler -AutomatedPerfTest.TestID=EntitySyncerTransformSync1024 -EntitySyncerPerfTest.WarmupSeconds=5 -EntitySyncerPerfTest.CaptureSeconds=30 -EntitySyncerPerfTest.SpawnTimeoutSeconds=60
```

The raw CSV includes `GameThread` timing for `STAT_EntitySyncerSyncSystem`, including actor lookup, transform comparison, and setter calls. `EntitySyncerPerfTest/AppliedSnapshots` counts applied snapshots by game frame. The sibling `_Summary.csv` reports `SpawnedActorCount`, `AverageSyncTimeMs` per game frame, and `AverageMsPerActor` per game frame. Between publications, a game frame can have zero sync work. `WarmupSeconds`, `CaptureSeconds`, `SpawnTimeoutSeconds`, and `StartupTimeoutSeconds` default to 5, 30, 60, and 10 seconds.

For a same-build comparison, run the 1,024-actor capture twice with distinct `TestID` values. Add `-EntitySyncerPerfTest.LegacyLocationSetter` to the legacy run to use `SetWorldLocation` for location-only writes. The default uses the guarded no-physics path. Compare `AverageSyncTimeMs` and `AverageMsPerActor` alongside `AppliedSnapshots`, `MovingEntities`, and `FrameTime`. Compare async results with other captures made at the same worker simulation rate and a similar game-frame rate.

For trace attribution, replace `-traceautostart=0` with `-trace=cpu,frame -tracefile=EntitySyncer.utrace`. In Unreal Insights, look for `EntityDirectorMovementSystem` on `FlecsSimulation` and for `EntitySyncerTransformSyncSystem` and its setter scopes on `GameThread`.

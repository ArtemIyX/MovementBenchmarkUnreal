# Entity director performance test

`UEntityDirectorPerfTestController` uses the AutomatedPerfTesting Gauntlet controller base. It expects exactly one placed `AEntityDirector` in the loaded level, calls `Start()`, checks that the requested entities were created, waits for a warmup, and captures a CSV profiler sample of the movement system.

Run with the editor target so the `MovementBenchmarkUncooked` module is loaded:

```text
UnrealEditor.exe MovementBenchmark.uproject /Game/Maps/MAP_DebugECS_GameThread_1024 -game -gauntlet=EntityDirectorPerfTest -AutomatedPerfTest.DoCSVProfiler -AutomatedPerfTest.TestID=EntityDirectorMovement -EntityDirectorPerfTest.WarmupSeconds=5 -EntityDirectorPerfTest.CaptureSeconds=30
```

The raw CSV is written by AutomatedPerfTesting. It contains per-frame timing for `STAT_EntityDirectorMovementSystem` and the accumulated number of moving entities. After the profiler finishes writing it, the controller writes a one-row sibling file with the `_Summary.csv` suffix. Its columns are `AverageSystemTimeMs`, `MaxSystemTimeMs`, `AverageMovingEntities`, `TotalEntities`, and `AverageUsPerMovingEntity`.

`AverageUsPerMovingEntity` is calculated as average system time divided by average moving entities, converted to microseconds. It is left empty when there were no moving entities to divide by. `WarmupSeconds` and `CaptureSeconds` are optional; their defaults are 5 and 30 seconds. The test exits with a failure code if the director is missing, creates no entities, or CSV capture cannot start.

Because this controller is in an `UncookedOnly` module, it is intended for editor-target runs and is not included in cooked game builds.

# Entity sync performance test

`UEntitySyncerPerfTestController` expects exactly one placed `AEntityDirector`, `AEntitySyncer`, and `ASimpleSpawner`. The spawner's actor class must implement `IActorTransformSyncInterface`; the actor and entity counts must match, and syncer debug drawing must be disabled. The controller waits for spawning to finish, starts the director, assigns the created entity IDs and actors to the syncer, starts syncing, then warms up and captures.

Run against the syncer test map with the editor target:

```text
UnrealEditor.exe MovementBenchmark.uproject /Game/Maps/MAP_Syncer_256 -game -gauntlet=EntitySyncerPerfTest -AutomatedPerfTest.DoCSVProfiler -AutomatedPerfTest.TestID=EntitySyncerTransformSync -EntitySyncerPerfTest.WarmupSeconds=5 -EntitySyncerPerfTest.CaptureSeconds=30 -EntitySyncerPerfTest.SpawnTimeoutSeconds=60
```

For the 1,024-actor map:

```text
UnrealEditor.exe MovementBenchmark.uproject /Game/Maps/MAP_Syncer_1024 -game -gauntlet=EntitySyncerPerfTest -AutomatedPerfTest.DoCSVProfiler -AutomatedPerfTest.TestID=EntitySyncerTransformSync1024 -EntitySyncerPerfTest.WarmupSeconds=5 -EntitySyncerPerfTest.CaptureSeconds=30 -EntitySyncerPerfTest.SpawnTimeoutSeconds=60
```

The raw CSV includes per-frame `STAT_EntitySyncerSyncSystem` timing for the full per-entity sync callback, including actor lookup, transform comparison, and any setter call. After capture, the controller writes a sibling `_Summary.csv` with `SpawnedActorCount`, `AverageSyncTimeMs`, and `AverageMsPerActor`. The per-actor value is the average sync time divided by the number of spawned actors. `WarmupSeconds`, `CaptureSeconds`, and `SpawnTimeoutSeconds` are optional; their defaults are 5, 30, and 60 seconds.

For a same-build comparison, run the 1,024-actor capture twice with distinct `TestID` values. Add `-EntitySyncerPerfTest.LegacyLocationSetter` to the legacy run to use `SetWorldLocation` for location-only writes. The default run uses the guarded no-physics path. Compare `AverageSyncTimeMs` and `AverageMsPerActor` in the summary files. In the raw files, also compare the average `EntityDirectorPerfTest/MovingEntities` and `FrameTime` across the rows with sync timings. The director uses frame delta time, so these columns help check that the captures had comparable movement workloads.

For setter attribution, take a separate capture with `-trace=cpu,frame -tracefile=EntitySyncer.utrace` and open it in Unreal Insights. On the Game Thread, examine `SceneComponentActor_SetLocation`, `SceneComponentActor_SetRotation`, and `SceneComponentActor_SetLocationAndRotation` nested in `EntitySyncerTransformSyncSystem`. Their inclusive times include the eligibility checks and engine calls. The enclosing scope's exclusive time approximates callback work outside the setters. Keep CPU tracing disabled for the primary CSV comparison because recording a scope per write changes the timing.

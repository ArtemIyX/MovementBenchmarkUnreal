# Benchmark Records

## Entity Director ECS movement baseline


Workload: 1,024 FLECS entities with transforms updated by the world subsystem tick on the game thread. This run measures transform updates in the ECS framework.

| AverageSystemTimeMs | MaxSystemTimeMs | AverageMovingEntities | TotalEntities | AverageUsPerMovingEntity |
| ------------------- | --------------- | --------------------- | ------------- | ------------------------ |
| 0.091472            | 0.6034          | 809.656041            | 1024          | 0.112977                 |

`AverageUsPerMovingEntity` is based on the average moving entity count for the capture.

## Entity Syncer actor transform sync

Workload: Spawned actors synchronized from an equal number of FLECS entities on the game thread. Timing covers the full per-entity sync callback, including actor lookup, transform comparison, and setter calls.

| SpawnedActorCount | AverageSyncTimeMs | AverageMsPerActor |
| ----------------- | ----------------- | ----------------- |
| 256               | 0.105884          | 0.000414          |
| 1024              | 0.323275          | 0.000316          |

### Same-build location setter A/B

Both captures used `MAP_Syncer_1024`, 1,024 actors, a 5-second warmup, and a 30-second capture. The legacy run passed `-EntitySyncerPerfTest.LegacyLocationSetter` to use `SetWorldLocation` for location-only writes. The default run used the guarded no-physics setter.

| Location setter | AverageSyncTimeMs | AverageMsPerActor | AverageMovingEntities | AverageFrameTimeMs |
| --------------- | ----------------- | ----------------- | --------------------- | ------------------ |
| Legacy          | 0.388822          | 0.000380          | 809.6                 | 4.2337             |
| No physics      | 0.341009          | 0.000333          | 810.8                 | 4.2320             |

The no-physics path reduced mean full sync callback time by **12.3%**, saving **47.8 µs per frame** in this comparison.

### Same-build class-check A/B

Both captures used `MAP_Syncer_1024`, 1,024 actors, a 5-second warmup, a 30-second capture, and the guarded no-physics setter. The uncached run passed `-EntitySyncerPerfTest.UncachedLocationClassChecks`. The default run cached the expected root class at `BeginPlay` and still checked the current root and live movement state on each write.

| Location class checks | AverageSyncTimeMs | AverageMsPerActor | AverageMovingEntities | AverageFrameTimeMs |
| --------------------- | ----------------- | ----------------- | --------------------- | ------------------ |
| Uncached              | 0.511216          | 0.000499          | 810.7                 | 4.2358             |
| Cached                | 0.496212          | 0.000485          | 811.1                 | 4.2334             |

Cached class checks reduced mean full sync callback time by **2.94%**, saving **15.0 µs per frame** in this comparison.

### Same-build translation-only setter A/B

Both captures used `MAP_Syncer_1024`, 1,024 actors, a 5-second warmup, a 30-second capture, and cached class checks. The translation-only run passed `-EntitySyncerPerfTest.TranslationOnlyLocationSetter`. The default used the engine no-physics setter.

| Location setter   | AverageSyncTimeMs | AverageMsPerActor | AverageMovingEntities | AverageFrameTimeMs |
| ----------------- | ----------------- | ----------------- | --------------------- | ------------------ |
| Engine no physics | 0.515109          | 0.000503          | 810.8                 | 4.2359             |
| Translation only  | 0.539799          | 0.000527          | 810.3                 | 4.2348             |

The translation-only path increased mean full sync callback time by **4.79%**, adding **24.69 µs per frame** in this comparison. The engine no-physics setter remains the default.

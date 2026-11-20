#include "Actors/EntitySyncer.h"

#include "Components/SceneComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/GameInstance.h"
#include "Interfaces/ActorTransformSyncInterface.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Stats/Stats.h"
#include "Subsystems/FlecsGameInstanceSubsystem.h"

CSV_DEFINE_CATEGORY(EntitySyncerPerfTest, true);

namespace
{
	DECLARE_CYCLE_STAT(TEXT("Entity Syncer Transform Sync System"), STAT_EntitySyncerSyncSystem, STATGROUP_Game);
}

AEntitySyncer::AEntitySyncer()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	SceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SceneComponent"));
	SetRootComponent(SceneComponent);
	SceneComponent->SetComponentTickEnabled(false);
}

void AEntitySyncer::SetFreeActors(const TArray<TWeakObjectPtr<AActor>>& InActors)
{
	FreeActors = InActors;
	RebuildEntityActorMap();
}

void AEntitySyncer::SetRun(const FMovementBenchmarkRun& InRun, const TArray<uint64>& InEntityIds)
{
	Run = InRun;
	EntityIds = InEntityIds;
	LastAppliedTick = 0;
	LastReadTick = 0;
	RebuildEntityActorMap();
}

bool AEntitySyncer::Start()
{
	const UWorld* World = GetWorld();
	const UMovementBenchmarkSimulation* Simulation = UMovementBenchmarkSimulation::Get(World);
	if (!Simulation || !World || Run.Epoch == 0 || Simulation->GetEpoch(*World) != Run.Epoch)
	{
		UE_LOG(LogTemp, Error, TEXT("%s cannot start transform syncing without an active FLECS run."), *GetName());
		return false;
	}
	if (EntityActors.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("%s cannot start transform syncing without valid entity/actor pairs."), *GetName());
		return false;
	}

	if (EntityIds.Num() != FreeActors.Num())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("%s received %d entity IDs and %d actors; syncing the first %d pairs."),
			*GetName(),
			EntityIds.Num(),
			FreeActors.Num(),
			FMath::Min(EntityIds.Num(), FreeActors.Num()));
	}

	RebuildEntityActorMap();
	for (int32 Index = 0; Index < FMath::Min(EntityIds.Num(), FreeActors.Num()); ++Index)
	{
		AActor* Actor = FreeActors[Index].Get();
		if (Actor && !Cast<IActorTransformSyncInterface>(Actor))
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("%s cannot sync actor %s because it does not implement IActorTransformSyncInterface."),
				*GetName(),
				*Actor->GetName());
		}
	}

	bSyncActive = true;
	SetActorTickEnabled(true);
	return true;
}

void AEntitySyncer::BeginPlay()
{
	Super::BeginPlay();
	if (UGameInstance* Instance = GetGameInstance())
	{
		if (auto* Flecs = Instance->GetSubsystem<UFlecsGameInstanceSubsystem>())
		{
			Flecs->OnWorldAboutToDetach.AddUObject(this, &AEntitySyncer::OnWorldAboutToDetach);
		}
	}
	SetActorTickEnabled(bDrawDebug || bSyncActive);
}

void AEntitySyncer::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UWorld* World = GetWorld();
	const UMovementBenchmarkSimulation* Simulation = UMovementBenchmarkSimulation::Get(World);
	const uint64 Epoch = Simulation && World ? Simulation->GetEpoch(*World) : 0;
	if (bSyncActive && Epoch != Run.Epoch)
	{
		ResetSync();
		SetActorTickEnabled(bDrawDebug);
	}

	if (Epoch != DebugEpoch)
	{
		DebugEpoch = Epoch;
		LastDebugTick = 0;
		DebugPoses.Reset();
	}
	if (Epoch == 0)
	{
		return;
	}

	const auto Publication = Simulation->GetPublication(*World);
	if (!Publication)
	{
		return;
	}

	ApplyPublication(*Publication);
	if (IsActorBeingDestroyed())
	{
		return;
	}

	if (bDrawDebug)
	{
		if (Publication->SimulationTick > LastDebugTick)
		{
			LastDebugTick = Publication->SimulationTick;
			UMovementBenchmarkSimulation::ReadDebugPoses(*Publication, DebugPoses);
		}
		for (const FMovementBenchmarkPose& Pose : DebugPoses)
		{
			DrawDebugBox(World, Pose.Location, DebugBoxExtent.GetAbs(), Pose.Rotation.Quaternion(),
				DebugBoxColor, false, FMath::Max(DebugBoxLifetime, 0.0f), 0, FMath::Max(DebugBoxThickness, 0.0f));
		}
	}
}

void AEntitySyncer::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UGameInstance* Instance = GetGameInstance())
	{
		if (auto* Flecs = Instance->GetSubsystem<UFlecsGameInstanceSubsystem>())
		{
			Flecs->OnWorldAboutToDetach.RemoveAll(this);
		}
	}
	ResetSync();
	DebugPoses.Reset();
	SetActorTickEnabled(false);
	Super::EndPlay(EndPlayReason);
}

void AEntitySyncer::OnWorldAboutToDetach(UWorld& World)
{
	if (GetWorld() == &World)
	{
		ResetSync();
		DebugEpoch = 0;
		LastDebugTick = 0;
		DebugPoses.Reset();
		SetActorTickEnabled(bDrawDebug);
	}
}

void AEntitySyncer::ResetSync()
{
	bSyncActive = false;
	Run = {};
	EntityActors.Reset();
	EntityIds.Reset();
	FreeActors.Reset();
	LastAppliedTick = 0;
	LastReadTick = 0;
	Poses.Reset();
}

void AEntitySyncer::ApplyPublication(const FFlecsSimulationDataPublication& Publication)
{
	if (!bSyncActive || Publication.LocalAttachmentEpoch != Run.Epoch || Publication.SimulationTick <= LastReadTick)
	{
		return;
	}
	LastReadTick = Publication.SimulationTick;
	if (!UMovementBenchmarkSimulation::ReadRun(Publication, Run, Poses))
	{
		return;
	}

	const uint64 ApplyingRunId = Run.Id;
	for (const FMovementBenchmarkPose& Pose : Poses)
	{
		ApplyPose(Pose);
		if (!bSyncActive || Run.Id != ApplyingRunId || IsActorBeingDestroyed())
		{
			return;
		}
	}
	LastAppliedTick = Publication.SimulationTick;
	CSV_CUSTOM_STAT(EntitySyncerPerfTest, AppliedSnapshots, 1, ECsvCustomStatOp::Accumulate);
}

void AEntitySyncer::ApplyPose(const FMovementBenchmarkPose& Transform)
{
	check(IsInGameThread());
	CSV_SCOPED_TIMING_STAT(EntitySyncerPerfTest, STAT_EntitySyncerSyncSystem);
	SCOPE_CYCLE_COUNTER(STAT_EntitySyncerSyncSystem);
	TRACE_CPUPROFILER_EVENT_SCOPE(EntitySyncerTransformSyncSystem);

	AActor* Actor;
	IActorTransformSyncInterface* TransformSync;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(EntitySyncerResolveActor);
		TWeakObjectPtr<AActor>* ActorReference = EntityActors.Find(Transform.EntityId);
		Actor = ActorReference ? ActorReference->Get() : nullptr;
		TransformSync = Actor ? Cast<IActorTransformSyncInterface>(Actor) : nullptr;
		if (!Actor || !TransformSync)
		{
			return;
		}
	}

	bool bLocationChanged;
	bool bRotationChanged;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(EntitySyncerCompareTransform);
		bLocationChanged = !Actor->GetActorLocation().Equals(Transform.Location, KINDA_SMALL_NUMBER);
		bRotationChanged = !Actor->GetActorRotation().Equals(Transform.Rotation, KINDA_SMALL_NUMBER);
	}

	if (bLocationChanged && bRotationChanged)
	{
		TransformSync->SetActorLocRot_Optimized(Transform.Location, Transform.Rotation);
	}
	else if (bLocationChanged)
	{
		TransformSync->SetActorLoc_Optimized(Transform.Location);
	}
	else if (bRotationChanged)
	{
		TransformSync->SetActorRot_Optimized(Transform.Rotation);
	}
}

void AEntitySyncer::RebuildEntityActorMap()
{
	EntityActors.Reset();
	const int32 PairCount = FMath::Min(EntityIds.Num(), FreeActors.Num());
	EntityActors.Reserve(PairCount);

	for (int32 Index = 0; Index < PairCount; ++Index)
	{
		AActor* Actor = FreeActors[Index].Get();
		if (!Actor)
		{
			continue;
		}

		if (Cast<IActorTransformSyncInterface>(Actor))
		{
			EntityActors.Add(EntityIds[Index], FreeActors[Index]);
		}
	}
}

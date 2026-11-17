// Fill out your copyright notice in the Description page of Project Settings.


#include "Actors/EntitySyncer.h"

#include "DrawDebugHelpers.h"
#include "Interfaces/ActorTransformSyncInterface.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Stats/Stats.h"
#include "Subsystems/FlecsSubsystem.h"

CSV_DEFINE_CATEGORY(EntitySyncerPerfTest, true);

namespace
{
	DECLARE_CYCLE_STAT(TEXT("Entity Syncer Transform Sync System"), STAT_EntitySyncerSyncSystem, STATGROUP_Game);

	FName MakeSyncSystemName(const AEntitySyncer& Syncer)
	{
		return FName(*FString::Printf(TEXT("EntitySyncerSync_%s"), *Syncer.GetFName().ToString()));
	}
}

// Sets default values
AEntitySyncer::AEntitySyncer()
{
	PrimaryActorTick.bCanEverTick = false;
	SceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SceneComponent"));
	SetRootComponent(SceneComponent);
	SceneComponent->SetComponentTickEnabled(false);
}


void AEntitySyncer::SetFreeActors(const TArray<TWeakObjectPtr<AActor>>& InActors)
{
	FreeActors = InActors;
	RebuildEntityActorMap();
}

void AEntitySyncer::SetEntityIds(const TArray<uint64>& InEntityIds)
{
	EntityIds = InEntityIds;
	RebuildEntityActorMap();
}

bool AEntitySyncer::Start()
{
	if (!CachedEcsWorld)
	{
		UE_LOG(LogTemp, Error, TEXT("%s cannot start transform syncing without a FLECS world."), *GetName());
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

	UWorld* World = GetWorld();
	UFlecsSubsystem* FlecsSubsystem = World ? World->GetSubsystem<UFlecsSubsystem>() : nullptr;
	if (!FlecsSubsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("%s cannot register transform syncing without a FLECS subsystem."), *GetName());
		return false;
	}

	if (!SyncSystemName.IsNone())
	{
		FlecsSubsystem->UnregisterSystem(SyncSystemName);
	}
	SyncSystemName = MakeSyncSystemName(*this);

	const TWeakObjectPtr<AEntitySyncer> WeakSyncer(this);
	const flecs::entity SyncSystem = FlecsSubsystem->RegisterOnUpdateSystem<
		const FECSTransformComponent,
		const FECSTransformMovementComponent>(
		SyncSystemName,
		[WeakSyncer](
		flecs::entity Entity,
		const FECSTransformComponent& Transform,
		const FECSTransformMovementComponent&)
		{
			CSV_SCOPED_TIMING_STAT(EntitySyncerPerfTest, STAT_EntitySyncerSyncSystem);
			SCOPE_CYCLE_COUNTER(STAT_EntitySyncerSyncSystem);
			TRACE_CPUPROFILER_EVENT_SCOPE(EntitySyncerTransformSyncSystem);

			AActor* Actor;
			IActorTransformSyncInterface* TransformSync;
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(EntitySyncerResolveActor);
				AEntitySyncer* Syncer = WeakSyncer.Get();
				if (!Syncer)
				{
					return;
				}

				TWeakObjectPtr<AActor>* ActorReference = Syncer->EntityActors.Find(static_cast<uint64>(Entity.id()));
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
		});

	if (!SyncSystem.is_valid())
	{
		UE_LOG(LogTemp, Error, TEXT("%s failed to register its FLECS transform sync system."), *GetName());
		FlecsSubsystem->UnregisterSystem(SyncSystemName);
		SyncSystemName = NAME_None;
		return false;
	}

	return true;
}

void AEntitySyncer::BeginPlay()
{
	Super::BeginPlay();

	if (UWorld* World = GetWorld())
	{
		if (UFlecsSubsystem* FlecsSubsystem = World->GetSubsystem<UFlecsSubsystem>())
		{
			CachedEcsWorld = FlecsSubsystem->GetEcsWorld();
		}
	}

	if (!CachedEcsWorld)
	{
		UE_LOG(LogTemp, Error, TEXT("%s could not retrieve the FLECS world."), *GetName());
		return;
	}

	if (bDrawDebug)
	{
		RegisterDebugSystem();
	}
}

void AEntitySyncer::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (!DebugSystemName.IsNone() || !SyncSystemName.IsNone())
	{
		if (UWorld* World = GetWorld())
		{
			if (UFlecsSubsystem* FlecsSubsystem = World->GetSubsystem<UFlecsSubsystem>())
			{
				if (!DebugSystemName.IsNone())
				{
					FlecsSubsystem->UnregisterSystem(DebugSystemName);
				}
				if (!SyncSystemName.IsNone())
				{
					FlecsSubsystem->UnregisterSystem(SyncSystemName);
				}
			}
		}

		DebugSystemName = NAME_None;
		SyncSystemName = NAME_None;
	}

	CachedEcsWorld = nullptr;
	Super::EndPlay(EndPlayReason);
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

void AEntitySyncer::RegisterDebugSystem()
{
	UWorld* World = GetWorld();
	UFlecsSubsystem* FlecsSubsystem = World ? World->GetSubsystem<UFlecsSubsystem>() : nullptr;
	if (!CachedEcsWorld || !FlecsSubsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("%s could not register its FLECS debug system."), *GetName());
		return;
	}

	DebugSystemName = FName(*FString::Printf(
		TEXT("EntitySyncerDebug_%s"),
		*GetFName().ToString()));

	const FVector BoxExtent = DebugBoxExtent.GetAbs();
	const FColor BoxColor = DebugBoxColor;
	const float BoxLifetime = FMath::Max(DebugBoxLifetime, 0.0f);
	const float BoxThickness = FMath::Max(DebugBoxThickness, 0.0f);
	const TWeakObjectPtr<UWorld> WeakWorld(World);

	const flecs::entity DebugSystem = FlecsSubsystem->RegisterOnUpdateSystem<const FECSTransformComponent>(
		DebugSystemName,
		[WeakWorld, BoxExtent, BoxColor, BoxLifetime, BoxThickness](const FECSTransformComponent& Transform)
		{
			if (UWorld* DebugWorld = WeakWorld.Get())
			{
				DrawDebugBox(
					DebugWorld,
					Transform.Location,
					BoxExtent,
					Transform.Rotation.Quaternion(),
					BoxColor,
					false,
					BoxLifetime,
					0,
					BoxThickness);
			}
		});

	if (!DebugSystem.is_valid())
	{
		UE_LOG(LogTemp, Error, TEXT("%s failed to register its FLECS debug system."), *GetName());
		FlecsSubsystem->UnregisterSystem(DebugSystemName);
		DebugSystemName = NAME_None;
	}
}

// Fill out your copyright notice in the Description page of Project Settings.


#include "Actors/EntityDirector.h"

#include "Engine/GameInstance.h"
#include "ProfilingDebugging/CsvProfiler.h"

#include "Stats/Stats.h"
#include "Subsystems/FlecsGameInstanceSubsystem.h"
#include "Subsystems/FlecsSubsystem.h"

CSV_DEFINE_CATEGORY(EntityDirectorPerfTest, true);

namespace
{
	const FName EntityDirectoryMovementSystemName(TEXT("EntityDirectoryMovementSystem"));
	DECLARE_CYCLE_STAT(
		TEXT("Entity Director Movement System"),
		STAT_EntityDirectorMovementSystem,
		STATGROUP_Game);
}

AEntityDirector::AEntityDirector()
{
	PrimaryActorTick.bCanEverTick = false;
	SceneComponent = CreateDefaultSubobject<USceneComponent>("SceneComponent");
	SetRootComponent(SceneComponent);
	SceneComponent->SetComponentTickEnabled(false);
}

void AEntityDirector::Start()
{
	CreateEntities();
	if (CreatedEntityIds.IsEmpty())
	{
		return;
	}

	RegisterMovementSystem();
}

void AEntityDirector::BeginPlay()
{
	Super::BeginPlay();
}

void AEntityDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UFlecsSubsystem* FlecsSubsystem = World->GetSubsystem<UFlecsSubsystem>())
		{
			FlecsSubsystem->UnregisterSystem(EntityDirectoryMovementSystemName);
		}
	}

	DestroyCreatedEntities();

	Super::EndPlay(EndPlayReason);
}

void AEntityDirector::CreateEntities()
{
	UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	UFlecsSubsystem* FlecsSubsystem = World ? World->GetSubsystem<UFlecsSubsystem>() : nullptr;
	UFlecsGameInstanceSubsystem* FlecsGameInstanceSubsystem = GameInstance
		                                                          ? GameInstance->GetSubsystem<
			                                                          UFlecsGameInstanceSubsystem>()
		                                                          : nullptr;
	flecs::world* TargetEcsWorld = FlecsSubsystem ? FlecsSubsystem->GetEcsWorld() : nullptr;
	const flecs::entity ActiveWorldScope = FlecsGameInstanceSubsystem
		                                       ? FlecsGameInstanceSubsystem->GetActiveWorldScope()
		                                       : flecs::entity();

	if (!TargetEcsWorld || !ActiveWorldScope.is_valid() ||
		ActiveWorldScope.world().c_ptr() != TargetEcsWorld->c_ptr())
	{
		UE_LOG(LogTemp, Warning, TEXT("%s could not access the active FLECS world scope."), *GetName());
		return;
	}

	const int32 EntityCount = FMath::Max(EntitiesToCreate, 0);
	CreatedEntityIds.Reserve(EntityCount);

	const FVector InitialLocation = GetActorLocation();
	FECSTransformComponent Transform;
	Transform.Location = InitialLocation;
	Transform.Rotation = GetActorRotation();

	FECSTransformMovementComponent Movement;
	Movement.Speed = FMath::Max(Speed, KINDA_SMALL_NUMBER);
	Movement.TargetLocation = InitialLocation;
	Movement.WaitDurationSeconds = FMath::Max(WaitDurationSeconds, 0.0f);

	for (int32 Index = 0; Index < EntityCount; ++Index)
	{
		flecs::entity Entity = TargetEcsWorld->entity().child_of(ActiveWorldScope.id());
		Entity = Entity.set<FECSTransformComponent>(Transform).set<FECSTransformMovementComponent>(Movement);

		if (Entity.is_alive())
		{
			CreatedEntityIds.Add(static_cast<uint64>(Entity.id()));
		}
	}
}

void AEntityDirector::RegisterMovementSystem()
{
	UWorld* World = GetWorld();
	UFlecsSubsystem* FlecsSubsystem = World ? World->GetSubsystem<UFlecsSubsystem>() : nullptr;
	flecs::world* TargetEcsWorld = FlecsSubsystem ? FlecsSubsystem->GetEcsWorld() : nullptr;
	if (!FlecsSubsystem || !TargetEcsWorld)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s could not register its FLECS movement system."), *GetName());
		DestroyCreatedEntities();
		return;
	}

	const float MaxTargetOffset = FMath::Max(TargetOffsetRange, 0.0f);
	const flecs::entity MovementSystem = FlecsSubsystem->RegisterOnUpdateSystem<
		FECSTransformComponent,
		FECSTransformMovementComponent>(
		EntityDirectoryMovementSystemName,
		[TargetEcsWorld, MaxTargetOffset](FECSTransformComponent& Transform, FECSTransformMovementComponent& Movement)
		{
			CSV_CUSTOM_STAT(
				EntityDirectorPerfTest,
				MovingEntities,
				Movement.bIsMoving ? 1 : 0,
				ECsvCustomStatOp::Accumulate);
			CSV_SCOPED_TIMING_STAT(EntityDirectorPerfTest, STAT_EntityDirectorMovementSystem);
			SCOPE_CYCLE_COUNTER(STAT_EntityDirectorMovementSystem);
			TRACE_CPUPROFILER_EVENT_SCOPE(EntityDirectorMovementSystem);

			const float DeltaSeconds = static_cast<float>(TargetEcsWorld->delta_time());
			if (DeltaSeconds <= 0.0f)
			{
				return;
			}

			float MovementDeltaSeconds = DeltaSeconds;
			if (Movement.RemainingWaitSeconds > 0.0f)
			{
				const float WaitSeconds = Movement.RemainingWaitSeconds;
				Movement.RemainingWaitSeconds = FMath::Max(
					WaitSeconds - DeltaSeconds,
					0.0f);
				if (Movement.RemainingWaitSeconds > 0.0f)
				{
					return;
				}

				MovementDeltaSeconds = FMath::Max(DeltaSeconds - WaitSeconds, 0.0f);
				if (MovementDeltaSeconds <= 0.0f)
				{
					return;
				}
			}

			if (!Movement.bIsMoving && !Movement.bHasTargetLocation)
			{
				Movement.TargetLocation = Transform.Location;
				Movement.TargetLocation.X += FMath::FRandRange(-MaxTargetOffset, MaxTargetOffset);
				Movement.TargetLocation.Y += FMath::FRandRange(-MaxTargetOffset, MaxTargetOffset);
				Movement.bHasTargetLocation = true;
				Movement.bIsMoving = true;

				const FVector Direction = Movement.TargetLocation - Transform.Location;
				if (!Direction.IsNearlyZero())
				{
					Transform.Rotation = Direction.Rotation();
					Transform.Rotation.Pitch = 0.0f;
					Transform.Rotation.Roll = 0.0f;
				}
			}

			if (!Movement.bIsMoving || !Movement.bHasTargetLocation)
			{
				return;
			}

			Transform.Location = FMath::VInterpConstantTo(
				Transform.Location,
				Movement.TargetLocation,
				MovementDeltaSeconds,
				FMath::Max(Movement.Speed, KINDA_SMALL_NUMBER));

			if (Transform.Location.Equals(Movement.TargetLocation, KINDA_SMALL_NUMBER))
			{
				Transform.Location = Movement.TargetLocation;
				Movement.bIsMoving = false;
				Movement.bHasTargetLocation = false;
				Movement.RemainingWaitSeconds = Movement.WaitDurationSeconds;
			}
		});

	if (!MovementSystem.is_valid())
	{
		UE_LOG(LogTemp, Warning, TEXT("%s failed to register its FLECS movement system."), *GetName());
		FlecsSubsystem->UnregisterSystem(EntityDirectoryMovementSystemName);
		DestroyCreatedEntities();
	}
}

void AEntityDirector::DestroyCreatedEntities()
{
	UWorld* World = GetWorld();
	UFlecsSubsystem* FlecsSubsystem = World ? World->GetSubsystem<UFlecsSubsystem>() : nullptr;
	flecs::world* TargetEcsWorld = FlecsSubsystem ? FlecsSubsystem->GetEcsWorld() : nullptr;
	if (TargetEcsWorld)
	{
		for (const uint64 EntityId : CreatedEntityIds)
		{
			const ecs_entity_t FlecsEntityId = static_cast<ecs_entity_t>(EntityId);
			if (TargetEcsWorld->is_alive(FlecsEntityId))
			{
				flecs::entity(TargetEcsWorld->c_ptr(), FlecsEntityId).destruct();
			}
		}
	}

	CreatedEntityIds.Empty();
}

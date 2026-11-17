// Fill out your copyright notice in the Description page of Project Settings.


#include "MovementBenchmark/Public/Actors/SimpleSpawner.h"
#include "GameFramework/GameStateBase.h"

// Sets default values
ASimpleSpawner::ASimpleSpawner()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	SceneComponent = CreateDefaultSubobject<USceneComponent>("SceneComponent");
	SetRootComponent(SceneComponent);
	SceneComponent->SetComponentTickEnabled(false);
	ActorToSpawn = AActor::StaticClass();
}

// Called when the game starts or when spawned
void ASimpleSpawner::BeginPlay()
{
	Super::BeginPlay();
	SpawnedActors.Reset();
	RemainingActorsToSpawn = 0;
	RequestedActorsToSpawn = 0;
	bSpawningStarted = false;
	bHasCompletedSpawning = false;
}

AActor* ASimpleSpawner::SpawnSingleActor()
{
	UWorld* World = GetWorld();
	if (!World || !ActorToSpawn)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = World->GetGameState();
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FTransform SpawnTransform = GetActorTransform();
	return World->SpawnActor(ActorToSpawn, &SpawnTransform, SpawnParameters);
}

void ASimpleSpawner::SpawnActors()
{
	if (bHasCompletedSpawning)
	{
		return;
	}

	if (!bSpawningStarted)
	{
		RequestedActorsToSpawn = FMath::Max(ActorsToSpawn, 0);
		RemainingActorsToSpawn = RequestedActorsToSpawn;
		SpawnedActors.Reset();
		bSpawningStarted = true;
	}

	if (RemainingActorsToSpawn <= 0)
	{
		CompleteSpawning();
		return;
	}

	const int32 SpawnCount = FMath::Min(RemainingActorsToSpawn, FMath::Max(MaxActorsPerFrame, 1));
	for (int32 Index = 0; Index < SpawnCount; ++Index)
	{
		if (AActor* SpawnedActor = SpawnSingleActor())
		{
			SpawnedActors.Add(SpawnedActor);
		}
	}

	RemainingActorsToSpawn -= SpawnCount;
	if (RemainingActorsToSpawn <= 0)
	{
		CompleteSpawning();
		return;
	}

	SetActorTickEnabled(true);
}

void ASimpleSpawner::CompleteSpawning()
{
	if (bHasCompletedSpawning)
	{
		return;
	}

	bHasCompletedSpawning = true;
	SetActorTickEnabled(false);

	if (SpawnedActors.Num() == RequestedActorsToSpawn)
	{
		OnAllActorsSpawned.Broadcast();
		return;
	}

	UE_LOG(
		LogTemp,
		Error,
		TEXT("%s completed spawning with %d of %d actors."),
		*GetName(),
		SpawnedActors.Num(),
		RequestedActorsToSpawn);
}

// Called every frame
void ASimpleSpawner::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	SpawnActors();
}

#include "Actors/EntityDirector.h"

#include "Components/SceneComponent.h"
#include "Engine/GameInstance.h"
#include "Subsystems/MovementBenchmarkSimulation.h"
#include "Subsystems/FlecsGameInstanceSubsystem.h"

AEntityDirector::AEntityDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	SceneComponent = CreateDefaultSubobject<USceneComponent>("SceneComponent");
	SetRootComponent(SceneComponent);
	SceneComponent->SetComponentTickEnabled(false);
}

void AEntityDirector::BeginPlay()
{
	Super::BeginPlay();
	if (UGameInstance* Instance = GetGameInstance())
	{
		if (auto* Flecs = Instance->GetSubsystem<UFlecsGameInstanceSubsystem>())
		{
			Flecs->OnWorldAboutToDetach.AddUObject(this, &AEntityDirector::OnWorldAboutToDetach);
		}
	}
}

void AEntityDirector::Start()
{
	if (Run.Id != 0)
	{
		return;
	}

	UWorld* World = GetWorld();
	UMovementBenchmarkSimulation* Simulation = UMovementBenchmarkSimulation::Get(World);
	bStartFailed = !Simulation || !World || !Simulation->StartRun(
		*World, EntitiesToCreate, GetActorLocation(), GetActorRotation(),
		Speed, TargetOffsetRange, WaitDurationSeconds, Run);
	if (bStartFailed)
	{
		UE_LOG(LogTemp, Error, TEXT("%s could not queue a FLECS benchmark run for %d entities."), *GetName(), EntitiesToCreate);
		return;
	}
	SetActorTickEnabled(true);
}

bool AEntityDirector::IsRunActive() const
{
	const UWorld* World = GetWorld();
	const UMovementBenchmarkSimulation* Simulation = UMovementBenchmarkSimulation::Get(World);
	return Run.Id != 0 && Simulation && World && Simulation->GetEpoch(*World) == Run.Epoch;
}

void AEntityDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!IsRunActive())
	{
		bStartFailed = true;
		ResetRun();
		return;
	}

	const UWorld& World = *GetWorld();
	const auto Publication = UMovementBenchmarkSimulation::Get(&World)->GetPublication(World);
	if (!Publication || Publication->SimulationTick == LastStartupTick)
	{
		return;
	}
	LastStartupTick = Publication->SimulationTick;
	TArray<FMovementBenchmarkPose> Poses;
	if (!UMovementBenchmarkSimulation::ReadRun(*Publication, Run, Poses))
	{
		return;
	}

	CreatedEntityIds.Reserve(Run.EntityCount);
	for (const FMovementBenchmarkPose& Pose : Poses)
	{
		CreatedEntityIds.Add(Pose.EntityId);
	}
	SetActorTickEnabled(false);
}

void AEntityDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UGameInstance* Instance = GetGameInstance())
	{
		if (auto* Flecs = Instance->GetSubsystem<UFlecsGameInstanceSubsystem>())
		{
			Flecs->OnWorldAboutToDetach.RemoveAll(this);
		}
	}
	if (UMovementBenchmarkSimulation* Simulation = UMovementBenchmarkSimulation::Get(GetWorld()))
	{
		Simulation->StopRun(Run);
	}
	ResetRun();
	Super::EndPlay(EndPlayReason);
}

void AEntityDirector::OnWorldAboutToDetach(UWorld& World)
{
	if (GetWorld() == &World)
	{
		bStartFailed = Run.Id != 0;
		ResetRun();
	}
}

void AEntityDirector::ResetRun()
{
	Run = {};
	CreatedEntityIds.Reset();
	LastStartupTick = 0;
	SetActorTickEnabled(false);
}

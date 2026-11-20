#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MovementBenchmarkSimulation.generated.h"

class UFlecsGameInstanceSubsystem;
struct FFlecsSimulationDataPublication;

struct FMovementBenchmarkRun
{
	uint64 Epoch = 0;
	uint64 Id = 0;
	int32 EntityCount = 0;
};

struct FMovementBenchmarkPose
{
	uint64 RunId;
	uint64 EntityId;
	FVector Location;
	FRotator Rotation;
	int32 SpawnIndex;
	int32 EntityCount;
};

UCLASS()
class MOVEMENTBENCHMARK_API UMovementBenchmarkSimulation : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	static UMovementBenchmarkSimulation* Get(const UWorld* World);

	bool StartRun(const UWorld& World, int32 Count, const FVector& Location, const FRotator& Rotation,
		float Speed, float TargetOffsetRange, float WaitSeconds, FMovementBenchmarkRun& OutRun);
	void StopRun(const FMovementBenchmarkRun& Run);
	uint64 GetEpoch(const UWorld& World) const;
	static bool ReadRun(const FFlecsSimulationDataPublication& Publication, const FMovementBenchmarkRun& Run,
		TArray<FMovementBenchmarkPose>& OutPoses);
	static void ReadDebugPoses(const FFlecsSimulationDataPublication& Publication,
		TArray<FMovementBenchmarkPose>& OutPoses);
	TSharedPtr<const FFlecsSimulationDataPublication, ESPMode::ThreadSafe> GetPublication(const UWorld& World) const;

private:
	bool RetryCleanup(float DeltaTime);
	UPROPERTY(Transient)
	TObjectPtr<UFlecsGameInstanceSubsystem> Flecs;
	TArray<FMovementBenchmarkRun> PendingCleanup;
	FTSTicker::FDelegateHandle CleanupTicker;
	uint64 NextRunId = 0;
	bool bRegistered = false;
};

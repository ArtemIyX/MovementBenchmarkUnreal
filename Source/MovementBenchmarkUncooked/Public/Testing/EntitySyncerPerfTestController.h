#pragma once

#include "AutomatedPerfTestControllerBase.h"
#include "EntitySyncerPerfTestController.generated.h"

class AEntityDirector;
class AEntitySyncer;
class ASimpleSpawner;

UCLASS()
class MOVEMENTBENCHMARKUNCOOKED_API UEntitySyncerPerfTestController : public UAutomatedPerfTestControllerBase
{
	GENERATED_BODY()

protected:
	virtual void OnInit() override;
	virtual void OnTick(float TimeDelta) override;
public:
	virtual void SetupTest() override;
	virtual void RunTest() override;

protected:
	UFUNCTION()
	void HandleAllActorsSpawned();

private:
	static void WriteSummaryCsv(const FString& RawCaptureFilename, int32 InSpawnedActorCount);
	void FailTest(const FString& Message);
	void RemoveSpawningCallback();

	enum class ETestPhase : uint8
	{
		WaitingForSpawn,
		WaitingForWarmup,
		WaitingForCaptureStart,
		Capturing,
		Finished
	};

	TWeakObjectPtr<AEntityDirector> EntityDirector;
	TWeakObjectPtr<AEntitySyncer> EntitySyncer;
	TWeakObjectPtr<ASimpleSpawner> SimpleSpawner;
	ETestPhase Phase = ETestPhase::Finished;
	float WarmupSeconds = 5.0f;
	float CaptureSeconds = 30.0f;
	float SpawnTimeoutSeconds = 60.0f;
	float CaptureStartTimeoutSeconds = 10.0f;
	float PhaseElapsedSeconds = 0.0f;
	int32 SpawnedActorCount = 0;
};

#pragma once

#include "AutomatedPerfTestControllerBase.h"
#include "EntityDirectorPerfTestController.generated.h"

class AEntityDirector;

UCLASS()
class MOVEMENTBENCHMARKUNCOOKED_API UEntityDirectorPerfTestController : public UAutomatedPerfTestControllerBase
{
	GENERATED_BODY()

protected:
	virtual void OnInit() override;
	virtual void OnTick(float TimeDelta) override;
public:
	virtual void SetupTest() override;
	virtual void RunTest() override;

private:
	static void WriteSummaryCsv(const FString& RawCaptureFilename, int32 InTotalEntityCount);

	enum class ETestPhase : uint8
	{
		WaitingForWarmup,
		WaitingForCaptureStart,
		Capturing,
		Finished
	};

	TWeakObjectPtr<AEntityDirector> EntityDirector;
	ETestPhase Phase = ETestPhase::Finished;
	float WarmupSeconds = 5.0f;
	float CaptureSeconds = 30.0f;
	float CaptureStartTimeoutSeconds = 10.0f;
	float PhaseElapsedSeconds = 0.0f;
	int32 TotalEntityCount = 0;
};

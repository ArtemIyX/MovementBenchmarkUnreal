#include "Testing/EntityDirectorPerfTestController.h"

#include "Actors/EntityDirector.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Serialization/Csv/CsvParser.h"

void UEntityDirectorPerfTestController::OnInit()
{
	Super::OnInit();

	FParse::Value(FCommandLine::Get(), TEXT("EntityDirectorPerfTest.WarmupSeconds="), WarmupSeconds);
	FParse::Value(FCommandLine::Get(), TEXT("EntityDirectorPerfTest.CaptureSeconds="), CaptureSeconds);
	FParse::Value(FCommandLine::Get(), TEXT("EntityDirectorPerfTest.StartupTimeoutSeconds="), StartupTimeoutSeconds);

	WarmupSeconds = FMath::Max(WarmupSeconds, 0.0f);
	CaptureSeconds = FMath::Max(CaptureSeconds, 0.1f);
	StartupTimeoutSeconds = FMath::Max(StartupTimeoutSeconds, 0.1f);
}

void UEntityDirectorPerfTestController::SetupTest()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("Entity director performance test has no active world."));
		EndTestFailure();
		return;
	}

	AEntityDirector* FoundDirector = nullptr;
	for (TActorIterator<AEntityDirector> It(World); It; ++It)
	{
		if (FoundDirector)
		{
			UE_LOG(LogTemp, Error, TEXT("Entity director performance test expects exactly one AEntityDirector in the level."));
			EndTestFailure();
			return;
		}

		FoundDirector = *It;
	}

	if (!FoundDirector)
	{
		UE_LOG(LogTemp, Error, TEXT("Entity director performance test could not find an AEntityDirector in the level."));
		EndTestFailure();
		return;
	}

	if (!RequestsCSVProfiler())
	{
		UE_LOG(LogTemp, Error, TEXT("Entity director performance test requires -AutomatedPerfTest.DoCSVProfiler."));
		EndTestFailure();
		return;
	}

	EntityDirector = FoundDirector;
	ExpectedEntityCount = FoundDirector->EntitiesToCreate;
	if (ExpectedEntityCount <= 0)
	{
		UE_LOG(LogTemp, Error, TEXT("The entity director benchmark requires a positive entity count."));
		EndTestFailure();
		return;
	}
	FoundDirector->Start();
	Phase = ETestPhase::WaitingForEntities;
	PhaseElapsedSeconds = 0.0f;
}

void UEntityDirectorPerfTestController::RunTest()
{
	AEntityDirector* Director = EntityDirector.Get();
	if (!Director || !Director->IsRunActive() || Director->HasStartFailed())
	{
		UE_LOG(LogTemp, Error, TEXT("Entity director was destroyed before the performance capture started."));
		Phase = ETestPhase::Finished;
		EndTestFailure();
		return;
	}

	// The engine queues BeginCapture; IsCapturing can still be false until a later frame.
	TryStartCSVProfiler();
	UE_LOG(LogTemp, Display, TEXT("Waiting for the CSV profiler to start capturing."));
	Phase = ETestPhase::WaitingForCaptureStart;
	PhaseElapsedSeconds = 0.0f;
}

void UEntityDirectorPerfTestController::OnTick(const float TimeDelta)
{
	Super::OnTick(TimeDelta);

	if (Phase == ETestPhase::Finished)
	{
		return;
	}

	AEntityDirector* Director = EntityDirector.Get();
	if (!Director || !Director->IsRunActive() || Director->HasStartFailed())
	{
		UE_LOG(LogTemp, Error, TEXT("The entity director's FLECS run failed to start or is no longer active."));
		Phase = ETestPhase::Finished;
		EndTestFailure();
		return;
	}

	PhaseElapsedSeconds += TimeDelta;
	if (Phase == ETestPhase::WaitingForEntities)
	{
		if (Director->GetCreatedEntityCount() == ExpectedEntityCount)
		{
			TotalEntityCount = ExpectedEntityCount;
			Phase = ETestPhase::WaitingForWarmup;
			PhaseElapsedSeconds = 0.0f;
			UE_LOG(LogTemp, Display, TEXT("FLECS worker published %d entities. Warming up for %.1f seconds."),
				TotalEntityCount, WarmupSeconds);
		}
		else if (PhaseElapsedSeconds >= StartupTimeoutSeconds)
		{
			UE_LOG(LogTemp, Error, TEXT("No complete FLECS publication for %d entities within %.1f seconds."),
				ExpectedEntityCount, StartupTimeoutSeconds);
			Phase = ETestPhase::Finished;
			EndTestFailure();
		}
	}
	else if (Phase == ETestPhase::WaitingForWarmup && PhaseElapsedSeconds >= WarmupSeconds)
	{
		RunTest();
	}
	else if (Phase == ETestPhase::WaitingForCaptureStart)
	{
		FCsvProfiler* CsvProfiler = FCsvProfiler::Get();
		if (CsvProfiler && CsvProfiler->IsCapturing())
		{
			MarkProfilingStart();
			UE_LOG(LogTemp, Display, TEXT("Capturing EntityDirectorMovementSystem for %.1f seconds."), CaptureSeconds);
			Phase = ETestPhase::Capturing;
			PhaseElapsedSeconds = 0.0f;
		}
		else if (PhaseElapsedSeconds >= CaptureStartTimeoutSeconds)
		{
			UE_LOG(LogTemp, Error, TEXT("CSV profiler did not enter capture state within %.1f seconds."), CaptureStartTimeoutSeconds);
			Phase = ETestPhase::Finished;
			EndTestFailure();
		}
	}
	else if (Phase == ETestPhase::Capturing && PhaseElapsedSeconds >= CaptureSeconds)
	{
		MarkProfilingEnd();
		FCsvProfiler* CsvProfiler = FCsvProfiler::Get();
		if (CsvProfiler)
		{
			const int32 CapturedEntityCount = TotalEntityCount;
			CsvProfiler->OnCSVProfileFinished().AddLambda(
				[CapturedEntityCount](const FString& RawCaptureFilename)
				{
					UEntityDirectorPerfTestController::WriteSummaryCsv(
						RawCaptureFilename,
						CapturedEntityCount);
				});
		}

		if (!TryStopCSVProfiler())
		{
			UE_LOG(LogTemp, Error, TEXT("Automated Perf Testing could not stop the CSV profiler."));
			Phase = ETestPhase::Finished;
			EndTestFailure();
			return;
		}

		UE_LOG(LogTemp, Display, TEXT("Entity director performance capture completed."));
		Phase = ETestPhase::Finished;
		TeardownTest(true);
	}
}

void UEntityDirectorPerfTestController::WriteSummaryCsv(
	const FString& RawCaptureFilename,
	const int32 InTotalEntityCount)
{
	FString RawCsvContents;
	if (!FFileHelper::LoadFileToString(RawCsvContents, *RawCaptureFilename))
	{
		UE_LOG(LogTemp, Warning, TEXT("Could not read profiler CSV '%s'; summary CSV was not created."), *RawCaptureFilename);
		return;
	}

	const FCsvParser CsvParser(RawCsvContents);
	const FCsvParser::FRows& Rows = CsvParser.GetRows();
	int32 HeaderRowIndex = INDEX_NONE;
	int32 FooterRowIndex = Rows.Num();
	for (int32 RowIndex = 0; RowIndex < Rows.Num(); ++RowIndex)
	{
		const TArray<const TCHAR*>& Row = Rows[RowIndex];
		if (Row.IsEmpty() || !FString(Row[0]).Equals(TEXT("EVENTS"), ESearchCase::CaseSensitive))
		{
			continue;
		}

		if (HeaderRowIndex == INDEX_NONE)
		{
			HeaderRowIndex = RowIndex;
		}
		else
		{
			FooterRowIndex = RowIndex;
			break;
		}
	}

	FString AverageSystemTimeMs;
	FString MaxSystemTimeMs;
	FString AverageMovingEntities;
	FString AverageUsPerMovingEntity;
	int32 TimingSamples = 0;
	int32 MovingEntitySamples = 0;

	if (HeaderRowIndex != INDEX_NONE)
	{
		const TArray<const TCHAR*>& Header = Rows[HeaderRowIndex];
		int32 SystemTimingColumn = INDEX_NONE;
		int32 MovingEntitiesColumn = INDEX_NONE;
		for (int32 ColumnIndex = 1; ColumnIndex < Header.Num(); ++ColumnIndex)
		{
			const FString ColumnName(Header[ColumnIndex]);
			if (ColumnName.Contains(TEXT("/FlecsSimulation/")) &&
				ColumnName.EndsWith(TEXT("/STAT_EntityDirectorMovementSystem")))
			{
				SystemTimingColumn = ColumnIndex;
			}
			else if (ColumnName.EndsWith(TEXT("/MovingEntities")))
			{
				MovingEntitiesColumn = ColumnIndex;
			}
		}

		double TotalSystemTimeMs = 0.0;
		double MaximumSystemTimeMs = 0.0;
		double TotalMovingEntities = 0.0;
		for (int32 RowIndex = HeaderRowIndex + 1; RowIndex < FooterRowIndex; ++RowIndex)
		{
			const TArray<const TCHAR*>& Row = Rows[RowIndex];
			if (SystemTimingColumn != INDEX_NONE && Row.IsValidIndex(SystemTimingColumn))
			{
				const double FrameSystemTimeMs = FCString::Atod(Row[SystemTimingColumn]);
				TotalSystemTimeMs += FrameSystemTimeMs;
				MaximumSystemTimeMs = FMath::Max(MaximumSystemTimeMs, FrameSystemTimeMs);
				++TimingSamples;
			}

			if (MovingEntitiesColumn != INDEX_NONE && Row.IsValidIndex(MovingEntitiesColumn))
			{
				TotalMovingEntities += FCString::Atod(Row[MovingEntitiesColumn]);
				++MovingEntitySamples;
			}
		}

		if (TimingSamples > 0)
		{
			const double AverageSystemTime = TotalSystemTimeMs / TimingSamples;
			AverageSystemTimeMs = FString::SanitizeFloat(AverageSystemTime);
			MaxSystemTimeMs = FString::SanitizeFloat(MaximumSystemTimeMs);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Profiler CSV has no STAT_EntityDirectorMovementSystem FlecsSimulation samples."));
		}

		if (MovingEntitySamples > 0)
		{
			const double AverageMovingCount = TotalMovingEntities / MovingEntitySamples;
			AverageMovingEntities = FString::SanitizeFloat(AverageMovingCount);
			if (TimingSamples > 0 && AverageMovingCount > 0.0)
			{
				const double AverageMicrosecondsPerMovingEntity =
					TotalSystemTimeMs / TotalMovingEntities * 1000.0;
				AverageUsPerMovingEntity = FString::SanitizeFloat(AverageMicrosecondsPerMovingEntity);
			}
			else if (AverageMovingCount <= 0.0)
			{
				UE_LOG(LogTemp, Warning, TEXT("Average microseconds per moving entity is undefined because no entities moved during capture."));
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Profiler CSV has no EntityDirectorPerfTest/MovingEntities samples."));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Profiler CSV has no frame header; timing and moving-entity metrics are unavailable."));
	}

	const FString SummaryCsv = FString::Printf(
		TEXT("SimulationThread,AverageSystemTimeMsPerGameFrame,MaxSystemTimeMsPerGameFrame,AverageMovingEntitySamplesPerGameFrame,TotalEntities,AverageUsPerMovingEntitySample\r\nFlecsSimulation,%s,%s,%s,%d,%s\r\n"),
		*AverageSystemTimeMs,
		*MaxSystemTimeMs,
		*AverageMovingEntities,
		InTotalEntityCount,
		*AverageUsPerMovingEntity);
	const FString SummaryFilename = FPaths::GetBaseFilename(RawCaptureFilename) + TEXT("_Summary.csv");
	const FString SummaryPath = FPaths::Combine(FPaths::GetPath(RawCaptureFilename), SummaryFilename);
	if (FFileHelper::SaveStringToFile(SummaryCsv, *SummaryPath))
	{
		UE_LOG(LogTemp, Display, TEXT("Entity director summary CSV saved to '%s'."), *SummaryPath);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Could not write entity director summary CSV '%s'."), *SummaryPath);
	}
}

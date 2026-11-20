#include "Testing/EntitySyncerPerfTestController.h"

#include "Actors/EntityDirector.h"
#include "Actors/EntitySyncer.h"
#include "Actors/SimpleSpawner.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Interfaces/ActorTransformSyncInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Serialization/Csv/CsvParser.h"

namespace
{
	template <typename TActor>
	TActor* FindUniqueActor(UWorld* World, const TCHAR* ActorTypeName)
	{
		TActor* FoundActor = nullptr;
		for (TActorIterator<TActor> It(World); It; ++It)
		{
			if (FoundActor)
			{
				UE_LOG(LogTemp, Error, TEXT("Entity sync performance test expects exactly one %s in the level."), ActorTypeName);
				return nullptr;
			}

			FoundActor = *It;
		}

		if (!FoundActor)
		{
			UE_LOG(LogTemp, Error, TEXT("Entity sync performance test could not find a %s in the level."), ActorTypeName);
		}
		return FoundActor;
	}
}

void UEntitySyncerPerfTestController::OnInit()
{
	Super::OnInit();

	FParse::Value(FCommandLine::Get(), TEXT("EntitySyncerPerfTest.WarmupSeconds="), WarmupSeconds);
	FParse::Value(FCommandLine::Get(), TEXT("EntitySyncerPerfTest.CaptureSeconds="), CaptureSeconds);
	FParse::Value(FCommandLine::Get(), TEXT("EntitySyncerPerfTest.SpawnTimeoutSeconds="), SpawnTimeoutSeconds);
	FParse::Value(FCommandLine::Get(), TEXT("EntitySyncerPerfTest.StartupTimeoutSeconds="), StartupTimeoutSeconds);

	WarmupSeconds = FMath::Max(WarmupSeconds, 0.0f);
	CaptureSeconds = FMath::Max(CaptureSeconds, 0.1f);
	SpawnTimeoutSeconds = FMath::Max(SpawnTimeoutSeconds, 0.1f);
	StartupTimeoutSeconds = FMath::Max(StartupTimeoutSeconds, 0.1f);
}

void UEntitySyncerPerfTestController::SetupTest()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		FailTest(TEXT("Entity sync performance test has no active world."));
		return;
	}

	AEntityDirector* FoundDirector = FindUniqueActor<AEntityDirector>(World, TEXT("AEntityDirector"));
	AEntitySyncer* FoundSyncer = FindUniqueActor<AEntitySyncer>(World, TEXT("AEntitySyncer"));
	ASimpleSpawner* FoundSpawner = FindUniqueActor<ASimpleSpawner>(World, TEXT("ASimpleSpawner"));
	if (!FoundDirector || !FoundSyncer || !FoundSpawner)
	{
		EndTestFailure();
		return;
	}

	if (!RequestsCSVProfiler())
	{
		FailTest(TEXT("Entity sync performance test requires -AutomatedPerfTest.DoCSVProfiler."));
		return;
	}

	const int32 ExpectedEntityCount = FMath::Max(FoundDirector->EntitiesToCreate, 0);
	const int32 ExpectedActorCount = FMath::Max(FoundSpawner->ActorsToSpawn, 0);
	if (ExpectedEntityCount <= 0 || ExpectedActorCount != ExpectedEntityCount)
	{
		FailTest(FString::Printf(
			TEXT("Entity director requests %d entities while the spawner requests %d actors; counts must match and be positive."),
			ExpectedEntityCount,
			ExpectedActorCount));
		return;
	}
	AActor* SpawnClassDefault = FoundSpawner->ActorToSpawn
		? FoundSpawner->ActorToSpawn->GetDefaultObject<AActor>()
		: nullptr;
	if (!SpawnClassDefault || !Cast<IActorTransformSyncInterface>(SpawnClassDefault))
	{
		FailTest(TEXT("The spawner's actor class must implement IActorTransformSyncInterface."));
		return;
	}

	if (FoundSyncer->IsDebugDrawingEnabled())
	{
		FailTest(TEXT("Entity sync performance test requires debug drawing to be disabled on AEntitySyncer."));
		return;
	}

	EntityDirector = FoundDirector;
	EntitySyncer = FoundSyncer;
	SimpleSpawner = FoundSpawner;
	FoundSpawner->OnAllActorsSpawned.AddDynamic(this, &UEntitySyncerPerfTestController::HandleAllActorsSpawned);
	Phase = ETestPhase::WaitingForSpawn;
	PhaseElapsedSeconds = 0.0f;

	UE_LOG(
		LogTemp,
		Display,
		TEXT("Entity sync performance test is spawning %d actors."),
		ExpectedActorCount);
	FoundSpawner->SpawnActors();
}

void UEntitySyncerPerfTestController::HandleAllActorsSpawned()
{
	RemoveSpawningCallback();
	if (Phase != ETestPhase::WaitingForSpawn)
	{
		return;
	}

	AEntityDirector* Director = EntityDirector.Get();
	AEntitySyncer* Syncer = EntitySyncer.Get();
	ASimpleSpawner* Spawner = SimpleSpawner.Get();
	if (!Director || !Syncer || !Spawner)
	{
		FailTest(TEXT("A required actor was destroyed before spawning completed."));
		return;
	}

	const int32 ExpectedActorCount = FMath::Max(Spawner->ActorsToSpawn, 0);
	if (Spawner->SpawnedActors.Num() != ExpectedActorCount)
	{
		FailTest(FString::Printf(
			TEXT("Spawner created %d of %d requested actors."),
			Spawner->SpawnedActors.Num(),
			ExpectedActorCount));
		return;
	}

	TArray<TWeakObjectPtr<AActor>> ActorsToSync;
	ActorsToSync.Reserve(Spawner->SpawnedActors.Num());
	for (const TObjectPtr<AActor>& SpawnedActor : Spawner->SpawnedActors)
	{
		AActor* Actor = SpawnedActor.Get();
		if (!Actor || !Cast<IActorTransformSyncInterface>(Actor))
		{
			FailTest(TEXT("Every spawned actor must implement IActorTransformSyncInterface."));
			return;
		}
		ActorsToSync.Add(Actor);
	}

	Syncer->SetFreeActors(ActorsToSync);
	SpawnedActorCount = ActorsToSync.Num();
	Director->Start();
	Phase = ETestPhase::WaitingForEntities;
	PhaseElapsedSeconds = 0.0f;
}

void UEntitySyncerPerfTestController::RunTest()
{
	if (!EntityDirector.IsValid() || !EntitySyncer.IsValid())
	{
		FailTest(TEXT("Entity director or syncer was destroyed before the performance capture started."));
		return;
	}

	TryStartCSVProfiler();
	UE_LOG(LogTemp, Display, TEXT("Waiting for the CSV profiler to start capturing entity transform syncing."));
	Phase = ETestPhase::WaitingForCaptureStart;
	PhaseElapsedSeconds = 0.0f;
}

void UEntitySyncerPerfTestController::OnTick(const float TimeDelta)
{
	Super::OnTick(TimeDelta);
	if (Phase == ETestPhase::Finished)
	{
		return;
	}

	AEntityDirector* Director = EntityDirector.Get();
	AEntitySyncer* Syncer = EntitySyncer.Get();
	if (!Director || !Syncer ||
		(Phase == ETestPhase::WaitingForSpawn && !SimpleSpawner.IsValid()))
	{
		FailTest(TEXT("A required actor was destroyed while the entity sync performance test was running."));
		return;
	}
	if (Phase != ETestPhase::WaitingForSpawn && (!Director->IsRunActive() || Director->HasStartFailed()))
	{
		FailTest(TEXT("The FLECS run failed to start or is no longer active."));
		return;
	}

	PhaseElapsedSeconds += TimeDelta;
	if (Phase == ETestPhase::WaitingForSpawn && PhaseElapsedSeconds >= SpawnTimeoutSeconds)
	{
		FailTest(FString::Printf(TEXT("Spawner did not finish within %.1f seconds."), SpawnTimeoutSeconds));
	}
	else if (Phase == ETestPhase::WaitingForEntities)
	{
		if (Director->GetCreatedEntityCount() == SpawnedActorCount)
		{
			Syncer->SetRun(Director->GetRun(), Director->GetCreatedEntityIds());
			if (!Syncer->Start())
			{
				FailTest(TEXT("Could not start applying FLECS snapshots to spawned actors."));
				return;
			}
			Phase = ETestPhase::WaitingForFirstSync;
			PhaseElapsedSeconds = 0.0f;
		}
		else if (PhaseElapsedSeconds >= StartupTimeoutSeconds)
		{
			FailTest(FString::Printf(TEXT("No complete FLECS publication for %d entities within %.1f seconds."),
				SpawnedActorCount, StartupTimeoutSeconds));
		}
	}
	else if (Phase == ETestPhase::WaitingForFirstSync)
	{
		if (Syncer->HasAppliedSnapshot())
		{
			Phase = ETestPhase::WaitingForWarmup;
			PhaseElapsedSeconds = 0.0f;
			UE_LOG(LogTemp, Display, TEXT("Applied the first worker snapshot to %d actors. Warming up for %.1f seconds."),
				SpawnedActorCount, WarmupSeconds);
		}
		else if (PhaseElapsedSeconds >= StartupTimeoutSeconds)
		{
			FailTest(FString::Printf(TEXT("The syncer did not apply a FLECS snapshot within %.1f seconds."), StartupTimeoutSeconds));
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
			UE_LOG(LogTemp, Display, TEXT("Capturing entity transform syncing for %.1f seconds."), CaptureSeconds);
			Phase = ETestPhase::Capturing;
			PhaseElapsedSeconds = 0.0f;
		}
		else if (PhaseElapsedSeconds >= CaptureStartTimeoutSeconds)
		{
			FailTest(FString::Printf(
				TEXT("CSV profiler did not enter capture state within %.1f seconds."),
				CaptureStartTimeoutSeconds));
		}
	}
	else if (Phase == ETestPhase::Capturing && PhaseElapsedSeconds >= CaptureSeconds)
	{
		MarkProfilingEnd();
		FCsvProfiler* CsvProfiler = FCsvProfiler::Get();
		if (!CsvProfiler)
		{
			FailTest(TEXT("CSV profiler became unavailable before capture could be finalized."));
			return;
		}

		const int32 CapturedActorCount = SpawnedActorCount;
		CsvProfiler->OnCSVProfileFinished().AddLambda(
			[CapturedActorCount](const FString& RawCaptureFilename)
			{
				UEntitySyncerPerfTestController::WriteSummaryCsv(
					RawCaptureFilename,
					CapturedActorCount);
			});

		if (!TryStopCSVProfiler())
		{
			FailTest(TEXT("Automated Perf Testing could not stop the CSV profiler."));
			return;
		}

		UE_LOG(LogTemp, Display, TEXT("Entity sync performance capture completed."));
		Phase = ETestPhase::Finished;
		TeardownTest(true);
	}
}

void UEntitySyncerPerfTestController::FailTest(const FString& Message)
{
	UE_LOG(LogTemp, Error, TEXT("%s"), *Message);
	RemoveSpawningCallback();
	Phase = ETestPhase::Finished;
	EndTestFailure();
}

void UEntitySyncerPerfTestController::RemoveSpawningCallback()
{
	if (ASimpleSpawner* Spawner = SimpleSpawner.Get())
	{
		Spawner->OnAllActorsSpawned.RemoveDynamic(this, &UEntitySyncerPerfTestController::HandleAllActorsSpawned);
	}
}

void UEntitySyncerPerfTestController::WriteSummaryCsv(
	const FString& RawCaptureFilename,
	const int32 InSpawnedActorCount)
{
	FString RawCsvContents;
	if (!FFileHelper::LoadFileToString(RawCsvContents, *RawCaptureFilename))
	{
		UE_LOG(LogTemp, Warning, TEXT("Could not read profiler CSV '%s'; sync summary CSV was not created."), *RawCaptureFilename);
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

	FString AverageSyncTimeMs;
	FString AverageMsPerActor;
	int32 TimingSamples = 0;
	double TotalSyncTimeMs = 0.0;
	if (HeaderRowIndex != INDEX_NONE)
	{
		const TArray<const TCHAR*>& Header = Rows[HeaderRowIndex];
		int32 SyncTimingColumn = INDEX_NONE;
		for (int32 ColumnIndex = 1; ColumnIndex < Header.Num(); ++ColumnIndex)
		{
			const FString ColumnName(Header[ColumnIndex]);
			if (ColumnName.Contains(TEXT("/GameThread/")) &&
				ColumnName.EndsWith(TEXT("/STAT_EntitySyncerSyncSystem")))
			{
				SyncTimingColumn = ColumnIndex;
				break;
			}
		}

		if (SyncTimingColumn != INDEX_NONE)
		{
			for (int32 RowIndex = HeaderRowIndex + 1; RowIndex < FooterRowIndex; ++RowIndex)
			{
				const TArray<const TCHAR*>& Row = Rows[RowIndex];
				if (!Row.IsValidIndex(SyncTimingColumn))
				{
					continue;
				}

				const FString Sample(Row[SyncTimingColumn]);
				if (Sample.IsEmpty())
				{
					continue;
				}

				TotalSyncTimeMs += FCString::Atod(*Sample);
				++TimingSamples;
			}
		}
	}

	if (TimingSamples > 0)
	{
		const double AverageTime = TotalSyncTimeMs / TimingSamples;
		AverageSyncTimeMs = FString::SanitizeFloat(AverageTime);
		if (InSpawnedActorCount > 0)
		{
			AverageMsPerActor = FString::SanitizeFloat(AverageTime / InSpawnedActorCount);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Profiler CSV has no STAT_EntitySyncerSyncSystem Game Thread samples."));
	}

	const FString SummaryCsv = FString::Printf(
		TEXT("SpawnedActorCount,AverageSyncTimeMs,AverageMsPerActor\r\n%d,%s,%s\r\n"),
		InSpawnedActorCount,
		*AverageSyncTimeMs,
		*AverageMsPerActor);
	const FString SummaryFilename = FPaths::GetBaseFilename(RawCaptureFilename) + TEXT("_Summary.csv");
	const FString SummaryPath = FPaths::Combine(FPaths::GetPath(RawCaptureFilename), SummaryFilename);
	if (FFileHelper::SaveStringToFile(SummaryCsv, *SummaryPath))
	{
		UE_LOG(LogTemp, Display, TEXT("Entity sync summary CSV saved to '%s'."), *SummaryPath);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Could not write entity sync summary CSV '%s'."), *SummaryPath);
	}
}

#include "Subsystems/MovementBenchmarkSimulation.h"

#include "Actors/EntityDirector.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Stats/Stats.h"
#include "Subsystems/FlecsGameInstanceSubsystem.h"
#include <type_traits>

#if WITH_DEV_AUTOMATION_TESTS
#include "Actors/Actor_SceneComponent.h"
#include "Actors/EntitySyncer.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#endif

CSV_DEFINE_CATEGORY(EntityDirectorPerfTest, true);

namespace
{
	const FName MovementName(TEXT("MovementBenchmark.Movement"));
	const FName PublisherName(TEXT("MovementBenchmark.Poses"));
	constexpr uint32 PoseType = 0x4D42504F; // MBPO
	constexpr int32 PosesPerChunk = 128;
	static_assert(std::is_trivially_copyable_v<FMovementBenchmarkPose>);
	static_assert(sizeof(FMovementBenchmarkPose) * PosesPerChunk <= FlecsMaxDataPublicationItemBytes);
	DECLARE_CYCLE_STAT(TEXT("Entity Director Movement System"), STAT_EntityDirectorMovementSystem, STATGROUP_Game);

	struct FRunEntity
	{
		uint64 RunId;
		int32 SpawnIndex;
		int32 EntityCount;
		float MaxTargetOffset;
	};

	std::string MakeRunName(uint64 RunId)
	{
		return "MovementBenchmarkRun_" + std::to_string(RunId);
	}

	void MoveEntity(flecs::iter& Iter, size_t, FECSTransformComponent& Transform,
		FECSTransformMovementComponent& Movement, const FRunEntity& Run)
	{
		CSV_CUSTOM_STAT(EntityDirectorPerfTest, MovingEntities, Movement.bIsMoving ? 1 : 0, ECsvCustomStatOp::Accumulate);
		CSV_SCOPED_TIMING_STAT(EntityDirectorPerfTest, STAT_EntityDirectorMovementSystem);
		SCOPE_CYCLE_COUNTER(STAT_EntityDirectorMovementSystem);
		TRACE_CPUPROFILER_EVENT_SCOPE(EntityDirectorMovementSystem);
		const float DeltaSeconds = static_cast<float>(Iter.delta_time());
		if (DeltaSeconds <= 0.0f)
		{
			return;
		}

		const float MaxTargetOffset = Run.MaxTargetOffset;
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
	}

	void PublishPoses(flecs::world& World, FFlecsSimulationDataBuilder& Builder)
	{
		TArray<FMovementBenchmarkPose, TInlineAllocator<PosesPerChunk>> Chunk;
		bool bAccepted = true;
		auto Flush = [&]
		{
			const auto Bytes = MakeArrayView(reinterpret_cast<const uint8*>(Chunk.GetData()),
				Chunk.Num() * static_cast<int32>(sizeof(FMovementBenchmarkPose)));
			bAccepted = Builder.TryAdd(PoseType, Bytes) == EFlecsDataPublicationAddResult::Accepted;
			Chunk.Reset();
		};
		World.each([&](flecs::entity Entity, const FRunEntity& Run, const FECSTransformComponent& Transform)
		{
			if (!bAccepted)
			{
				return;
			}
			Chunk.Add({Run.RunId, Entity.id(), Transform.Location, Transform.Rotation, Run.SpawnIndex, Run.EntityCount});
			if (Chunk.Num() == PosesPerChunk)
			{
				Flush();
			}
		});
		if (!Chunk.IsEmpty())
		{
			Flush();
		}
	}

	template <typename Visitor>
	bool VisitPoses(const FFlecsSimulationDataPublication& Publication, Visitor&& Visit)
	{
		for (const FFlecsSimulationDataItem& Item : Publication.Items)
		{
			if (Item.TypeId != PoseType)
			{
				continue;
			}
			if (Item.Payload.Num() % sizeof(FMovementBenchmarkPose) != 0)
			{
				return false;
			}
			for (int32 Offset = 0; Offset < Item.Payload.Num(); Offset += sizeof(FMovementBenchmarkPose))
			{
				FMovementBenchmarkPose Pose;
				FMemory::Memcpy(&Pose, Item.Payload.GetData() + Offset, sizeof(Pose));
				Visit(Pose);
			}
		}
		return true;
	}
}

void UMovementBenchmarkSimulation::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Flecs = Collection.InitializeDependency<UFlecsGameInstanceSubsystem>();
	if (Flecs)
	{
		const bool bMovementRegistered = Flecs->RegisterPersistentWorkerCommand(MovementName, [](flecs::world& World)
		{
			World.system<FECSTransformComponent, FECSTransformMovementComponent, const FRunEntity>("MovementBenchmarkMovement")
				.kind(flecs::OnUpdate).each(MoveEntity);
		});
		bRegistered = bMovementRegistered && Flecs->RegisterPersistentDataPublisher(
			Flecs->GetActiveEpoch(), PublisherName, PublishPoses) == EFlecsWorkerCommandResult::Accepted;
	}
	if (!bRegistered)
	{
		UE_LOG(LogTemp, Error, TEXT("Could not register MovementBenchmark worker systems."));
	}
}

void UMovementBenchmarkSimulation::Deinitialize()
{
	if (CleanupTicker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(CleanupTicker);
		CleanupTicker.Reset();
	}
	PendingCleanup.Reset();
	Flecs = nullptr;
	Super::Deinitialize();
}

UMovementBenchmarkSimulation* UMovementBenchmarkSimulation::Get(const UWorld* World)
{
	UGameInstance* Instance = World ? World->GetGameInstance() : nullptr;
	return Instance ? Instance->GetSubsystem<UMovementBenchmarkSimulation>() : nullptr;
}

uint64 UMovementBenchmarkSimulation::GetEpoch(const UWorld& World) const
{
	return Flecs && Flecs->IsWorldReady(World) ? Flecs->GetActiveEpoch() : 0;
}

bool UMovementBenchmarkSimulation::StartRun(const UWorld& World, int32 Count, const FVector& Location,
	const FRotator& Rotation, float Speed, float TargetOffsetRange, float WaitSeconds, FMovementBenchmarkRun& OutRun)
{
	const uint64 Epoch = GetEpoch(World);
	if (!bRegistered || Epoch == 0 || Count <= 0 ||
		static_cast<uint64>(Count) * sizeof(FMovementBenchmarkPose) > FlecsMaxDataPublicationBytes)
	{
		return false;
	}

	const FMovementBenchmarkRun Run{Epoch, ++NextRunId, Count};
	const FECSTransformComponent Transform{Location, Rotation};
	FECSTransformMovementComponent Movement;
	Movement.Speed = FMath::Max(Speed, KINDA_SMALL_NUMBER);
	Movement.TargetLocation = Location;
	Movement.WaitDurationSeconds = FMath::Max(WaitSeconds, 0.0f);
	const float MaxOffset = FMath::Max(TargetOffsetRange, 0.0f);
	const auto Result = Flecs->SubmitWorkerCommand(Epoch, [Run, Transform, Movement, MaxOffset](flecs::world& WorkerWorld)
	{
		const std::string Name = MakeRunName(Run.Id);
		const flecs::entity Parent = WorkerWorld.entity(Name.c_str());
		for (int32 Index = 0; Index < Run.EntityCount; ++Index)
		{
			WorkerWorld.entity().child_of(Parent).set(Transform).set(Movement)
				.set<FRunEntity>({Run.Id, Index, Run.EntityCount, MaxOffset});
		}
	});
	if (Result != EFlecsWorkerCommandResult::Accepted)
	{
		return false;
	}

	OutRun = Run;
	return true;
}

void UMovementBenchmarkSimulation::StopRun(const FMovementBenchmarkRun& Run)
{
	if (Run.Id == 0)
	{
		return;
	}
	PendingCleanup.Add(Run);
	if (RetryCleanup(0.0f) && !CleanupTicker.IsValid())
	{
		CleanupTicker = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateWeakLambda(this, [this](float DeltaTime)
			{
				const bool bNeedsRetry = RetryCleanup(DeltaTime);
				if (!bNeedsRetry)
				{
					CleanupTicker.Reset();
				}
				return bNeedsRetry;
			}));
	}
}

bool UMovementBenchmarkSimulation::RetryCleanup(float)
{
	const uint64 Epoch = Flecs ? Flecs->GetActiveEpoch() : 0;
	for (int32 Index = PendingCleanup.Num() - 1; Index >= 0; --Index)
	{
		const FMovementBenchmarkRun Run = PendingCleanup[Index];
		if (Run.Epoch == Epoch)
		{
			const auto Result = Flecs->SubmitWorkerCommand(Epoch, [RunId = Run.Id](flecs::world& World)
			{
				const flecs::entity Parent = World.lookup(MakeRunName(RunId).c_str());
				if (Parent.is_alive())
				{
					Parent.destruct();
				}
			});
			if (Result == EFlecsWorkerCommandResult::QueueFull)
			{
				continue;
			}
		}
		PendingCleanup.RemoveAtSwap(Index, 1, EAllowShrinking::No);
	}
	return !PendingCleanup.IsEmpty();
}

TSharedPtr<const FFlecsSimulationDataPublication, ESPMode::ThreadSafe>
UMovementBenchmarkSimulation::GetPublication(const UWorld& World) const
{
	const uint64 Epoch = GetEpoch(World);
	auto Publication = Epoch != 0 ? Flecs->GetDataPublication() : nullptr;
	return Publication && Publication->LocalAttachmentEpoch == Epoch ? Publication : nullptr;
}

bool UMovementBenchmarkSimulation::ReadRun(const FFlecsSimulationDataPublication& Publication,
	const FMovementBenchmarkRun& Run, TArray<FMovementBenchmarkPose>& OutPoses)
{
	OutPoses.Reset();
	if (Run.Id == 0 || Run.EntityCount <= 0 || Publication.LocalAttachmentEpoch != Run.Epoch)
	{
		return false;
	}
	OutPoses.SetNumZeroed(Run.EntityCount);
	int32 Found = 0;
	bool bValid = true;
	const bool bPayloadValid = VisitPoses(Publication, [&](const FMovementBenchmarkPose& Pose)
	{
		if (Pose.RunId != Run.Id)
		{
			return;
		}
		if (Pose.EntityCount != Run.EntityCount || Pose.EntityId == 0 ||
			!OutPoses.IsValidIndex(Pose.SpawnIndex) || OutPoses[Pose.SpawnIndex].EntityId != 0)
		{
			bValid = false;
			return;
		}
		OutPoses[Pose.SpawnIndex] = Pose;
		++Found;
	});
	if (!bPayloadValid || !bValid || Found != Run.EntityCount)
	{
		OutPoses.Reset();
		return false;
	}
	return true;
}

void UMovementBenchmarkSimulation::ReadDebugPoses(const FFlecsSimulationDataPublication& Publication,
	TArray<FMovementBenchmarkPose>& OutPoses)
{
	OutPoses.Reset();
	if (!VisitPoses(Publication, [&](const FMovementBenchmarkPose& Pose) { OutPoses.Add(Pose); }))
	{
		OutPoses.Reset();
	}
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMovementBenchmarkPublicationTest, "MovementBenchmark.Flecs.TransformPublication",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMovementBenchmarkPublicationTest::RunTest(const FString&)
{
	const FMovementBenchmarkRun Run{3, 10, 2};
	const FMovementBenchmarkPose First{Run.Id, 11, FVector::ZeroVector, FRotator::ZeroRotator, 0, 2};
	const FMovementBenchmarkPose Second{Run.Id, 12, FVector::OneVector, FRotator::ZeroRotator, 1, 2};
	const FMovementBenchmarkPose Other{99, 13, FVector::ZeroVector, FRotator::ZeroRotator, 0, 1};
	FFlecsSimulationDataPublication Publication;
	Publication.LocalAttachmentEpoch = Run.Epoch;
	TArray<FMovementBenchmarkPose> OutPoses;
	auto AddChunk = [&Publication](const FMovementBenchmarkPose& Pose)
	{
		auto& Item = Publication.Items.AddDefaulted_GetRef();
		Item.TypeId = PoseType;
		Item.Payload.Append(reinterpret_cast<const uint8*>(&Pose), sizeof(Pose));
	};

	AddChunk(Second);
	AddChunk(Other);
	TestFalse(TEXT("A partial run cannot become ready"), UMovementBenchmarkSimulation::ReadRun(Publication, Run, OutPoses));
	TestTrue(TEXT("Partial results are cleared"), OutPoses.IsEmpty());

	AddChunk(First);
	if (TestTrue(TEXT("A complete run is accepted"), UMovementBenchmarkSimulation::ReadRun(Publication, Run, OutPoses)))
	{
		TestEqual(TEXT("First entity is indexed by spawn order"), OutPoses[0].EntityId, First.EntityId);
		TestEqual(TEXT("Second entity is indexed by spawn order"), OutPoses[1].EntityId, Second.EntityId);
	}

	--Publication.LocalAttachmentEpoch;
	TestFalse(TEXT("A stale epoch is rejected"), UMovementBenchmarkSimulation::ReadRun(Publication, Run, OutPoses));
	++Publication.LocalAttachmentEpoch;

	AddChunk(First);
	TestFalse(TEXT("Duplicate spawn indices are rejected"), UMovementBenchmarkSimulation::ReadRun(Publication, Run, OutPoses));
	Publication.Items.Pop();
	Publication.Items.Last().Payload.Add(0);
	TestFalse(TEXT("Malformed chunks are rejected"), UMovementBenchmarkSimulation::ReadRun(Publication, Run, OutPoses));

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Test world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	auto* Syncer = World->SpawnActor<AEntitySyncer>();
	auto* FirstActor = World->SpawnActor<AActor_SceneComponent>();
	auto* SecondActor = World->SpawnActor<AActor_SceneComponent>();
	if (!TestNotNull(TEXT("Syncer"), Syncer) || !TestNotNull(TEXT("First actor"), FirstActor) ||
		!TestNotNull(TEXT("Second actor"), SecondActor))
	{
		return false;
	}

	Syncer->SetFreeActors({FirstActor, SecondActor});
	Syncer->SetRun(Run, {First.EntityId, Second.EntityId});
	Syncer->bSyncActive = true;
	Publication.Items.Reset();
	Publication.SimulationTick = 1;
	AddChunk(First);
	AddChunk(Second);
	Syncer->ApplyPublication(Publication);
	TestTrue(TEXT("A complete snapshot is applied"), Syncer->HasAppliedSnapshot());
	TestEqual(TEXT("Published location is applied"), SecondActor->GetActorLocation(), Second.Location);

	const FVector ChangedLocation(20.0, 30.0, 40.0);
	SecondActor->SetActorLoc_Optimized(ChangedLocation);
	Syncer->ApplyPublication(Publication);
	TestEqual(TEXT("The same simulation tick is skipped"), SecondActor->GetActorLocation(), ChangedLocation);

	TestTrue(TEXT("The first mapped actor is destroyed"), FirstActor->Destroy());
	++Publication.SimulationTick;
	Syncer->ApplyPublication(Publication);
	TestEqual(TEXT("A destroyed mapping does not prevent subsequent actors from updating"),
		SecondActor->GetActorLocation(), Second.Location);

	Syncer->OnWorldAboutToDetach(*World);
	TestFalse(TEXT("Detach resets snapshot readiness"), Syncer->HasAppliedSnapshot());
	SecondActor->SetActorLoc_Optimized(ChangedLocation);
	++Publication.SimulationTick;
	Syncer->ApplyPublication(Publication);
	TestEqual(TEXT("A detached run cannot write actor transforms"), SecondActor->GetActorLocation(), ChangedLocation);
	return true;
}
#endif

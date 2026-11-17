// Fill out your copyright notice in the Description page of Project Settings.


#include "Actors/Actor_SceneComponent.h"

#include "Components/SceneComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
	bool HasBareRootClasses(const AActor_SceneComponent& Actor, const USceneComponent& Root)
	{
		return Actor.GetClass() == AActor_SceneComponent::StaticClass()
			&& Root.GetClass() == USceneComponent::StaticClass();
	}

	bool HasBareRootMovementState(const AActor_SceneComponent& Actor, const USceneComponent& Root)
	{
		// The no-physics setter skips mobility checks, overlap updates, and teleport semantics.
		return Actor.IsActorInitialized()
			&& Root.IsRegistered()
			&& !Root.IsBeingDestroyed()
			&& Root.Mobility == EComponentMobility::Movable
			&& Root.GetAttachParent() == nullptr
			&& Root.GetAttachChildren().IsEmpty()
			&& Actor.GetComponents().Num() == 1
			&& !Root.GetShouldUpdatePhysicsVolume()
			&& !Root.CanEverAffectNavigation()
			&& !Root.IsDeferringMovementUpdates();
	}

	bool CanUseBareRootTransformPath(const AActor_SceneComponent& Actor, const USceneComponent& Root)
	{
		return HasBareRootClasses(Actor, Root) && HasBareRootMovementState(Actor, Root);
	}
}

AActor_SceneComponent::AActor_SceneComponent()
{
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bAllowTickOnDedicatedServer = false;
	SceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SceneComponent"));
	SceneComponent->bNeverNeedsRenderUpdate = true;
	SceneComponent->SetComponentTickEnabled(false);
	SceneComponent->SetVisibility(false);
	
	SetRootComponent(SceneComponent);
	
}

void AActor_SceneComponent::BeginPlay()
{
	static const bool bLegacyLocationRequested = FParse::Param(
		FCommandLine::Get(), TEXT("EntitySyncerPerfTest.LegacyLocationSetter"));
	static const bool bUncachedLocationClassesRequested = FParse::Param(
		FCommandLine::Get(), TEXT("EntitySyncerPerfTest.UncachedLocationClassChecks"));
	static const bool bTranslationOnlyLocationRequested = FParse::Param(
		FCommandLine::Get(), TEXT("EntitySyncerPerfTest.TranslationOnlyLocationSetter"));
	bUseLegacyLocationSetter = bLegacyLocationRequested;
	bUseUncachedLocationClassChecks = bUncachedLocationClassesRequested;
	bUseTranslationOnlyLocationSetter = bTranslationOnlyLocationRequested;
	if (!bUseUncachedLocationClassChecks && GetClass() == AActor_SceneComponent::StaticClass())
	{
		CachedLocationRootClass = USceneComponent::StaticClass();
	}
	Super::BeginPlay();
}

void AActor_SceneComponent::SetActorLoc_Optimized(const FVector& InLocation)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SceneComponentActor_SetLocation);
	if (USceneComponent* Root = GetRootComponent())
	{
		bool bUseNoPhysics;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(SceneComponentActor_CheckLocationEligibility);
			bUseNoPhysics = !bUseLegacyLocationSetter
				&& (bUseUncachedLocationClassChecks
					? HasBareRootClasses(*this, *Root)
					: Root->GetClass() == CachedLocationRootClass)
				&& HasBareRootMovementState(*this, *Root);
		}

		if (bUseNoPhysics)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(SceneComponentActor_SetLocationNoPhysics);
			if (bUseTranslationOnlyLocationSetter)
			{
				// The guarded root is unattached, so its relative location is its world location.
				// The low-level write must be followed by the same propagation as the engine setter.
				Root->SetRelativeLocation_Direct(InLocation);
				Root->UpdateComponentToWorld(EUpdateTransformFlags::SkipPhysicsUpdate);
			}
			else
			{
				Root->SetWorldLocationAndRotationNoPhysics(InLocation, Root->GetRelativeRotation());
			}
			return;
		}

		Root->SetWorldLocation(InLocation, /*bSweep*/ false, nullptr, ETeleportType::TeleportPhysics);
	}
}

void AActor_SceneComponent::SetActorRot_Optimized(const FRotator& InRotation)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SceneComponentActor_SetRotation);
	if (USceneComponent* Root = GetRootComponent())
	{
		if (CanUseBareRootTransformPath(*this, *Root))
		{
			Root->SetWorldLocationAndRotationNoPhysics(Root->GetComponentLocation(), InRotation);
			return;
		}

		Root->SetWorldRotation(InRotation, /*bSweep*/ false, nullptr, ETeleportType::TeleportPhysics);
	}
}

void AActor_SceneComponent::SetActorLocRot_Optimized(const FVector& InLocation, const FRotator& InRotation)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SceneComponentActor_SetLocationAndRotation);
	if (USceneComponent* Root = GetRootComponent())
	{
		if (CanUseBareRootTransformPath(*this, *Root))
		{
			Root->SetWorldLocationAndRotationNoPhysics(InLocation, InRotation);
			return;
		}

		Root->SetWorldLocationAndRotation(InLocation, InRotation, /*bSweep*/ false, nullptr, ETeleportType::TeleportPhysics);
	}
}


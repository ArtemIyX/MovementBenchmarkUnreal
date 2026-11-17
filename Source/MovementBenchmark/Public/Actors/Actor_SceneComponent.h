// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interfaces//ActorTransformSyncInterface.h"
#include "Actor_SceneComponent.generated.h"

UCLASS()
class MOVEMENTBENCHMARK_API AActor_SceneComponent : public AActor, public IActorTransformSyncInterface
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AActor_SceneComponent();

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Components")
	USceneComponent* SceneComponent;

public:
	virtual void SetActorLoc_Optimized(const FVector& InLocation) override;
	virtual void SetActorRot_Optimized(const FRotator& InRotation) override;
	virtual void SetActorLocRot_Optimized(const FVector& InLocation, const FRotator& InRotation) override;

private:
	// Cache the expected native class, so each write can still check the current root.
	const UClass* CachedLocationRootClass = nullptr;
	bool bUseLegacyLocationSetter = false;
	bool bUseUncachedLocationClassChecks = true;
	bool bUseTranslationOnlyLocationSetter = false;
};

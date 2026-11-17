// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SimpleSpawner.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAllActorsSpawnedSignature);

UCLASS()
class MOVEMENTBENCHMARK_API ASimpleSpawner : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ASimpleSpawner();


protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneComponent;

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, NoClear, Category="Settings", meta=(ExposeOnSpawn=true))
	TSubclassOf<AActor> ActorToSpawn;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Settings", meta=(ExposeOnSpawn=true))
	int32 ActorsToSpawn = 1024;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Settings", meta=(ExposeOnSpawn=true))
	int32 MaxActorsPerFrame = 32;

public:
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="State")
	TArray<TObjectPtr<AActor>> SpawnedActors;

	UPROPERTY(BlueprintAssignable, Category="Events")
	FOnAllActorsSpawnedSignature OnAllActorsSpawned;

protected:
	int32 RemainingActorsToSpawn = 0;
	int32 RequestedActorsToSpawn = 0;
	bool bSpawningStarted = false;
	bool bHasCompletedSpawning = false;

	virtual void BeginPlay() override;
	virtual AActor* SpawnSingleActor();

	virtual void CompleteSpawning();

public:
	virtual void SpawnActors();
	virtual void Tick(float DeltaTime) override;
};

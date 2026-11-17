// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EntityDirector.generated.h"

struct FECSTransformComponent
{
	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
};

struct FECSTransformMovementComponent
{
	float Speed = 100.0f;
	bool bIsMoving = false;
	bool bHasTargetLocation = false;
	FVector TargetLocation = FVector::ZeroVector;
	float RemainingWaitSeconds = 0.0f;
	float WaitDurationSeconds = 1.0f;
};

UCLASS(Blueprintable, BlueprintType)
class MOVEMENTBENCHMARK_API AEntityDirector : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AEntityDirector();

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Components")
	TObjectPtr<USceneComponent> SceneComponent;
	
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Settings", meta=(ClampMin="0"))
	int32 EntitiesToCreate = 1024;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Settings", meta=(ClampMin="0.01"))
	float Speed = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Settings", meta=(ClampMin="0.0"))
	float TargetOffsetRange = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Settings", meta=(ClampMin="0.0"))
	float WaitDurationSeconds = 1.0f;


public:
	UFUNCTION(BlueprintCallable, Category="EntityDirector|Public")
	void Start();

	UFUNCTION(BlueprintPure, Category="EntityDirector|Public")
	int32 GetCreatedEntityCount() const { return CreatedEntityIds.Num(); }

	const TArray<uint64>& GetCreatedEntityIds() const { return CreatedEntityIds; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void CreateEntities();
	void RegisterMovementSystem();
	void DestroyCreatedEntities();

	TArray<uint64> CreatedEntityIds;
};

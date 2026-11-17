// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EntityDirector.h"
#include "GameFramework/Actor.h"
#include "EntitySyncer.generated.h"

namespace flecs
{
	struct world;
}

UCLASS(BlueprintType, Blueprintable)
class MOVEMENTBENCHMARK_API AEntitySyncer : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AEntitySyncer();

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Components")
	TObjectPtr<USceneComponent> SceneComponent;
protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Debug")
	bool bDrawDebug = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Debug", meta=(ClampMin="0.0"))
	FVector DebugBoxExtent = FVector(50.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Debug")
	FColor DebugBoxColor = FColor::Green;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Debug", meta=(ClampMin="0.0"))
	float DebugBoxLifetime = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Debug", meta=(ClampMin="0.0"))
	float DebugBoxThickness = 1.0f;

public:
	void SetFreeActors(const TArray<TWeakObjectPtr<AActor>>& InActors);
	void SetEntityIds(const TArray<uint64>& InEntityIds);
	bool Start();
	bool IsDebugDrawingEnabled() const { return bDrawDebug; }
private:
	flecs::world* CachedEcsWorld = nullptr;
	FName DebugSystemName;
	FName SyncSystemName;
	TArray<uint64> EntityIds;
	TArray<TWeakObjectPtr<AActor>> FreeActors;
	TMap<uint64, TWeakObjectPtr<AActor>> EntityActors;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void RegisterDebugSystem();
	void RebuildEntityActorMap();
};

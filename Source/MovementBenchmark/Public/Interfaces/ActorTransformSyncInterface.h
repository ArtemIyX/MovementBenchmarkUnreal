#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ActorTransformSyncInterface.generated.h"

UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UActorTransformSyncInterface : public UInterface
{
	GENERATED_BODY()
};

/** Native contract for actors that support optimized transform synchronization. */
class MOVEMENTBENCHMARK_API IActorTransformSyncInterface
{
	GENERATED_BODY()

public:
	virtual void SetActorLoc_Optimized(const FVector& InLocation) = 0;
	virtual void SetActorRot_Optimized(const FRotator& InRotation) = 0;
	virtual void SetActorLocRot_Optimized(const FVector& InLocation, const FRotator& InRotation) = 0;
};

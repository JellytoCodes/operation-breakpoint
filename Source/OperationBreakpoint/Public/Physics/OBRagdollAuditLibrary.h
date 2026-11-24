#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "OBRagdollAuditLibrary.generated.h"
class UPhysicsAsset;
class USkeletalMesh;

/** Read-only inspection of stored ragdoll bodies, joints and collision pairs. */
UCLASS()
class OPERATIONBREAKPOINT_API UOBRagdollAuditLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="OB|Physics|Audit")
    static FString DescribePhysicsAsset(UPhysicsAsset* Asset, USkeletalMesh* Mesh);
};

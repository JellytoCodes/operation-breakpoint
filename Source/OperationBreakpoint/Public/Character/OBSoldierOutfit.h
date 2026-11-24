#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "OBSoldierOutfit.generated.h"

class USkeletalMesh;
class UMaterialInterface;

USTRUCT(BlueprintType)
struct FOBSoldierOutfitPart
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USkeletalMesh> Mesh;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<TObjectPtr<UMaterialInterface>> Materials;
    // Rigid accessories can follow a bone with an authored offset in mesh reference space.
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName RigidBone;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector RefPoseOffset=FVector::ZeroVector;
};

// Uses the existing soldier skeleton and animations; only the visible kit changes.
UCLASS(BlueprintType)
class OPERATIONBREAKPOINT_API UOBSoldierOutfit : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FOBSoldierOutfitPart> Parts;
};

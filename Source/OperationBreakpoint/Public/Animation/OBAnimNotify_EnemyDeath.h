#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "OBAnimNotify_EnemyDeath.generated.h"

UENUM(BlueprintType)
enum class EOBEnemyDeathNotifyType : uint8
{
    DropWeapon,
    BeginDeathPhysics
};

UCLASS(meta = (DisplayName = "OB Enemy Death"))
class OPERATIONBREAKPOINT_API UOBAnimNotify_EnemyDeath : public UAnimNotify
{
    GENERATED_BODY()

public:
    virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
        const FAnimNotifyEventReference& EventReference) override;

    UPROPERTY(EditAnywhere, Category = "OB|Death")
    EOBEnemyDeathNotifyType NotifyType = EOBEnemyDeathNotifyType::DropWeapon;
};
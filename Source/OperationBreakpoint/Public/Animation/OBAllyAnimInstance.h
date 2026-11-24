#pragma once
#include "CoreMinimal.h"
#include "Animation/OBWeaponGripAnimInstance.h"
#include "OBAllyAnimInstance.generated.h"
UCLASS(Blueprintable)
class OPERATIONBREAKPOINT_API UOBAllyAnimInstance : public UOBWeaponGripAnimInstance
{
    GENERATED_BODY()
public:
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;
    UPROPERTY(BlueprintReadOnly, Category="OB|Ally|Animation") float GroundSpeed=0.f;
    UPROPERTY(BlueprintReadOnly, Category="OB|Ally|Animation") bool bIsMoving=false;
    UPROPERTY(BlueprintReadOnly, Category="OB|Ally|Animation") bool bIsCrouched=false;
};

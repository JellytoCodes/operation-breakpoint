#pragma once

#include "CoreMinimal.h"
#include "Animation/OBWeaponGripAnimInstance.h"
#include "OBPlayerAnimInstance.generated.h"

class AOBPlayerCharacter;
class UOBPlayerSplineComponent;

UENUM(BlueprintType)
enum class EOBPlayerAnimState : uint8
{
    Shooting,
    RailMoving
};

UCLASS(Blueprintable)
class OPERATIONBREAKPOINT_API UOBPlayerAnimInstance : public UOBWeaponGripAnimInstance
{
    GENERATED_BODY()

public:
    virtual void NativeInitializeAnimation() override;
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;

protected:
    UPROPERTY(BlueprintReadOnly, Category = "OB|Animation")
    EOBPlayerAnimState AnimState = EOBPlayerAnimState::Shooting;

    UPROPERTY(BlueprintReadOnly, Category = "OB|Animation")
    bool bIsRailMoving = false;

private:
    void CacheOwnerReferences();

    UPROPERTY(Transient)
    TObjectPtr<AOBPlayerCharacter> PlayerCharacter;

    UPROPERTY(Transient)
    TObjectPtr<UOBPlayerSplineComponent> PlayerSplineComponent;
};

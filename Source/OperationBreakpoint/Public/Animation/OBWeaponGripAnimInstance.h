#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "OBWeaponGripAnimInstance.generated.h"

// Preserves the existing AnimBP graph/montages, then corrects the final body pose in its native proxy.
UCLASS(Abstract, Blueprintable)
class OPERATIONBREAKPOINT_API UOBWeaponGripAnimInstance : public UAnimInstance
{
    GENERATED_BODY()
protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
};

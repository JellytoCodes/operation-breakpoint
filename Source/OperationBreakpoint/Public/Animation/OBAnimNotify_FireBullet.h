#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "OBAnimNotify_FireBullet.generated.h"

UCLASS(meta = (DisplayName = "OB Fire Bullet"))
class OPERATIONBREAKPOINT_API UOBAnimNotify_FireBullet : public UAnimNotify
{
    GENERATED_BODY()

public:
    virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
};
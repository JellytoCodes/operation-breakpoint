#include "Animation/OBAllyAnimInstance.h"
#include "GameFramework/Character.h"
void UOBAllyAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);
    const auto* Character=Cast<ACharacter>(TryGetPawnOwner());
    GroundSpeed=Character ? Character->GetVelocity().Size2D() : 0.f;
    bIsMoving=GroundSpeed>3.f; bIsCrouched=Character && Character->bIsCrouched;
}

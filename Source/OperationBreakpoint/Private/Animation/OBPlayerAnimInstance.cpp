#include "Animation/OBPlayerAnimInstance.h"

#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"

void UOBPlayerAnimInstance::NativeInitializeAnimation()
{
    Super::NativeInitializeAnimation();
    CacheOwnerReferences();
}

void UOBPlayerAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);

    if (!IsValid(PlayerCharacter) || !IsValid(PlayerSplineComponent)) CacheOwnerReferences();
    if (!IsValid(PlayerSplineComponent)) return;

    bIsRailMoving = PlayerSplineComponent->IsMoving();
    AnimState = bIsRailMoving ? EOBPlayerAnimState::RailMoving : EOBPlayerAnimState::Shooting;
}

void UOBPlayerAnimInstance::CacheOwnerReferences()
{
    PlayerCharacter = Cast<AOBPlayerCharacter>(TryGetPawnOwner());
    PlayerSplineComponent = PlayerCharacter ? PlayerCharacter->GetPlayerSplineComponent() : nullptr;
}
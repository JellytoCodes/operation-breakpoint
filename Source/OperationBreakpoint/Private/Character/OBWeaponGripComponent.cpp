#include "Character/OBWeaponGripComponent.h"
#include "Character/OBCharacter.h"
#include "Character/OBEnemyCharacter.h"
#include "Components/SkeletalMeshComponent.h"

UOBWeaponGripComponent::UOBWeaponGripComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

USkeletalMeshComponent* UOBWeaponGripComponent::GetWeapon() const
{
    const auto* Character = Cast<AOBCharacter>(GetOwner());
    if (IsUsingRPGGrip()) return CastChecked<AOBEnemyCharacter>(GetOwner())->RPGWeaponMesh;
    return Character ? Character->GetWeaponMesh() : nullptr;
}

bool UOBWeaponGripComponent::IsUsingRPGGrip() const
{
    const auto* Enemy=Cast<AOBEnemyCharacter>(GetOwner());
    return Enemy && Enemy->IsRPGTrooper() && Enemy->RPGWeaponMesh && Enemy->RPGWeaponMesh->GetSkeletalMeshAsset();
}

const FTransform& UOBWeaponGripComponent::GetWeaponAttachment() const
{ return IsUsingRPGGrip() ? RPGWeaponInRightHand : WeaponInRightHand; }
const FTransform& UOBWeaponGripComponent::GetSupportHand() const
{ return IsUsingRPGGrip() ? RPGLeftHandInWeapon : LeftHandInWeapon; }
const TMap<FName, FRotator>& UOBWeaponGripComponent::GetFingerOffsets() const
{ return IsUsingRPGGrip() ? RPGFingerPoseOffsets : FingerPoseOffsets; }

void UOBWeaponGripComponent::ApplyWeaponAttachment()
{
    auto* Character = Cast<AOBCharacter>(GetOwner());
    auto* Weapon = GetWeapon();
    if (const auto* Enemy = Cast<AOBEnemyCharacter>(Character))
        if (Enemy->GetCombatState() == EOBEnemyCombatState::Dead) return;
    if (!bEnabled || (IsUsingRPGGrip() && !bRPGEnabled) || !Character || !Weapon || !Weapon->GetSkeletalMeshAsset()
        || Weapon->IsSimulatingPhysics() || !Character->GetMesh()->DoesSocketExist(TEXT("hand_r"))) return;
    Weapon->AttachToComponent(Character->GetMesh(), FAttachmentTransformRules::KeepRelativeTransform, TEXT("hand_r"));
    Weapon->SetRelativeTransform(GetWeaponAttachment());
}

bool UOBWeaponGripComponent::ShouldApplyGrip() const
{
    const auto* Character = Cast<AOBCharacter>(GetOwner());
    const auto* Weapon = GetWeapon();
    if (!bEnabled || (IsUsingRPGGrip() && !bRPGEnabled) || bGripReleased || !Character || !Weapon || !Weapon->GetSkeletalMeshAsset()
        || Weapon->GetAttachParent() != Character->GetMesh() || Weapon->GetAttachSocketName() != TEXT("hand_r")
        || Weapon->IsSimulatingPhysics() || Weapon->bHiddenInGame || !Weapon->IsVisible()
        || Character->GetMesh()->IsSimulatingPhysics()) return false;
    if (const auto* Enemy = Cast<AOBEnemyCharacter>(Character))
        return Enemy->GetCombatState() != EOBEnemyCombatState::Dead
            && Enemy->GetCombatState() != EOBEnemyCombatState::Reloading;
    return true;
}

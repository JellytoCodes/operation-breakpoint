#include "Character/OBHostageCharacter.h"

#include "AIController.h"
#include "Animation/AnimSequence.h"
#include "Character/OBWeaponGripComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"

AOBHostageCharacter::AOBHostageCharacter()
{
    CombatVisualScale=1.f;
    ConfigureHostage(false);
}

void AOBHostageCharacter::SetEncounterCombatant(bool bCombatant)
{
    // The class's role is invariant, including calls through an AOBEnemyCharacter* in the spawner.
    Super::SetEncounterCombatant(false);
}

void AOBHostageCharacter::ConfigureHostage(bool bApplyPose)
{
    AutoPossessAI = EAutoPossessAI::Disabled;
    AIControllerClass = nullptr;
    SetEncounterCombatant(false);
    GetCharacterMovement()->StopMovementImmediately();
    GetCharacterMovement()->DisableMovement();
    GetWeaponGripComponent()->bEnabled = false;
    GetWeaponGripComponent()->SetGripReleased(true);

    // Use the base accessor deliberately: the public virtual accessor reports that this character is unarmed.
    if (auto* InheritedWeapon = AOBEnemyCharacter::GetWeaponMesh())
    {
        InheritedWeapon->SetSkeletalMeshAsset(nullptr);
        InheritedWeapon->SetVisibility(false, true);
        InheritedWeapon->SetHiddenInGame(true, true);
        InheritedWeapon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
    if (RPGLauncherMesh)
    {
        RPGLauncherMesh->SetStaticMesh(nullptr);
        RPGLauncherMesh->SetVisibility(false, true);
        RPGLauncherMesh->SetHiddenInGame(true, true);
        RPGLauncherMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
    if (RPGWeaponMesh)
    {
        RPGWeaponMesh->SetSkeletalMeshAsset(nullptr);
        RPGWeaponMesh->SetVisibility(false,true);
        RPGWeaponMesh->SetHiddenInGame(true,true);
    }
    // Also handle an incorrectly inherited/assigned AI on a Blueprint or a placed actor.
    if (auto* AI = Cast<AAIController>(GetController()))
    {
        AI->UnPossess();
        AI->Destroy();
    }
    if (bApplyPose)
    {
        GetMesh()->SetAnimationMode(EAnimationMode::AnimationSingleNode);
        GetMesh()->SetAnimation(IdleAnimation);
        if (IdleAnimation) GetMesh()->PlayAnimation(IdleAnimation, true);

        // Imported hostage skeletons carry a 100x bone scale while their visible bodies
        // are already centimetre-sized. Keep shot boxes and their offsets in mesh cm.
        // Read the authored component defaults so construction/BeginPlay never divide twice.
        TInlineComponentArray<UBoxComponent*> Boxes(this);
        for (UBoxComponent* Box : Boxes)
        {
            const auto* AuthoredBox = Cast<UBoxComponent>(Box->GetArchetype());
            if (!AuthoredBox || Box->GetAttachParent() != GetMesh()
                || !GetMesh()->DoesSocketExist(Box->GetAttachSocketName())) continue;
            const FVector BoneScale = GetMesh()->GetSocketTransform(
                Box->GetAttachSocketName(), RTS_Component).GetScale3D();
            const FVector InverseScale = FTransform::GetSafeScaleReciprocal(BoneScale);
            Box->SetRelativeScale3D(AuthoredBox->GetRelativeScale3D() * InverseScale);
            Box->SetRelativeLocation(AuthoredBox->GetRelativeLocation() * InverseScale);
        }
    }
}

void AOBHostageCharacter::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    ConfigureHostage(true);
}

void AOBHostageCharacter::BeginPlay()
{
    Super::BeginPlay();
    ConfigureHostage(true);
    SettleOnFloor();
}

void AOBHostageCharacter::SettleOnFloor()
{
    // Spawn collision adjustment can lift the standing capsule above the deck.
    // Hostages intentionally have no movement/gravity, so settle them once after
    // that adjustment rather than leaving them suspended for the entire encounter.
    const FVector Center=GetActorLocation();
    FHitResult Floor;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(OBHostageFloor),false,this);
    if (GetWorld()->LineTraceSingleByObjectType(Floor,Center,Center-FVector(0,0,500),
        FCollisionObjectQueryParams(ECC_WorldStatic),Query) && GetCharacterMovement()->IsWalkable(Floor))
    {
        FVector Grounded=Center;
        Grounded.Z=Floor.ImpactPoint.Z+GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2.f;
        SetActorLocation(Grounded,false,nullptr,ETeleportType::TeleportPhysics);
    }
}

#include "Character/OBEnemyCharacter.h"

#include "Animation/OBEnemyAnimInstance.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "World/OBBulletActor.h"
#include "World/OBRPGRocketActor.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBActionComponent.h"
#include "Stage/OBStageFlowComponent.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "AI/OBAIController.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/DamageEvents.h"
#include "Engine/Engine.h"
#include "DrawDebugHelpers.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Particles/ParticleSystem.h"
#include "Math/RotationMatrix.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Chaos/ChaosEngineInterface.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogOBEnemy, Log, All);

namespace OBHitZone
{
    const FName Head(TEXT("HitZone.Head"));
    const FName Body(TEXT("HitZone.Body"));
    const FName Arm(TEXT("HitZone.Arm"));
    const FName Leg(TEXT("HitZone.Leg"));
}

AOBEnemyCharacter::AOBEnemyCharacter()
{
	PrimaryActorTick.bCanEverTick = false;
    InitializeEnemyAudio();
    GetCharacterMovement()->GetNavAgentPropertiesRef().bCanCrouch = true;

    SurroundingAimOffsets = {
        FVector(0.0f, -150.0f, 100.0f), FVector(0.0f, -150.0f, 0.0f), FVector(0.0f, -150.0f, -100.0f),
        FVector(0.0f, 150.0f, 100.0f), FVector(0.0f, 150.0f, 0.0f), FVector(0.0f, 150.0f, -100.0f)
    };

    WeaponMesh =CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WeaponMesh"));
    WeaponMesh->SetupAttachment(GetMesh(), TEXT("WeaponSocket"));
    WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    WeaponMesh->SetGenerateOverlapEvents(false);

    RPGLauncherMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RPGLauncherMesh"));
    RPGLauncherMesh->SetupAttachment(GetMesh(), TEXT("WeaponSocket"));
    RPGLauncherMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    RPGLauncherMesh->SetCanEverAffectNavigation(false);
    RPGLauncherMesh->SetGenerateOverlapEvents(false);
    RPGLauncherMesh->SetRelativeRotation(FRotator(90, 0, 0));
    RPGLauncherMesh->SetRelativeScale3D(FVector(.12f, .12f, .85f));
    RPGLauncherMesh->SetVisibility(false);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> LauncherTube(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    RPGLauncherMesh->SetStaticMesh(LauncherTube.Object);
    RPGRocketClass = AOBRPGRocketActor::StaticClass();
    RPGWeaponMesh=CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("RPGWeaponMesh"));
    RPGWeaponMesh->SetupAttachment(GetMesh(),TEXT("hand_r"));
    RPGWeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    RPGWeaponMesh->SetGenerateOverlapEvents(false);
    RPGWeaponMesh->SetCanEverAffectNavigation(false);
    RPGWeaponMesh->SetVisibility(false);

    CreateHitBox(TEXT("HeadHitBox"), TEXT("head"), OBHitZone::Head, FVector(12.0f, 10.0f, 12.0f));
    CreateHitBox(TEXT("BodyHitBox"), TEXT("spine_02"), OBHitZone::Body, FVector(24.0f, 18.0f, 16.0f));
    CreateHitBox(TEXT("LeftUpperArmHitBox"), TEXT("upperarm_l"), OBHitZone::Arm, FVector(14.0f, 6.0f, 6.0f), FVector(14.0f, 0.0f, 0.0f));
    CreateHitBox(TEXT("LeftLowerArmHitBox"), TEXT("lowerarm_l"), OBHitZone::Arm, FVector(13.5f, 5.0f, 5.0f), FVector(13.5f, 0.0f, 0.0f));
    // Manny's right arm and left leg bones point along negative local X.
    CreateHitBox(TEXT("RightUpperArmHitBox"), TEXT("upperarm_r"), OBHitZone::Arm, FVector(14.0f, 6.0f, 6.0f), FVector(-14.0f, 0.0f, 0.0f));
    CreateHitBox(TEXT("RightLowerArmHitBox"), TEXT("lowerarm_r"), OBHitZone::Arm, FVector(13.5f, 5.0f, 5.0f), FVector(-13.5f, 0.0f, 0.0f));
    CreateHitBox(TEXT("LeftUpperLegHitBox"), TEXT("thigh_l"), OBHitZone::Leg, FVector(22.0f, 9.0f, 9.0f), FVector(-22.0f, 0.0f, 0.0f));
    CreateHitBox(TEXT("LeftLowerLegHitBox"), TEXT("calf_l"), OBHitZone::Leg, FVector(21.0f, 7.0f, 7.0f), FVector(-21.0f, 0.0f, 0.0f));
    CreateHitBox(TEXT("RightUpperLegHitBox"), TEXT("thigh_r"), OBHitZone::Leg, FVector(22.0f, 9.0f, 9.0f), FVector(22.0f, 0.0f, 0.0f));
    CreateHitBox(TEXT("RightLowerLegHitBox"), TEXT("calf_r"), OBHitZone::Leg, FVector(21.0f, 7.0f, 7.0f), FVector(21.0f, 0.0f, 0.0f));
}

UBoxComponent* AOBEnemyCharacter::CreateHitBox(FName Name, FName BoneName, FName ZoneTag,
    const FVector& Extent, const FVector& Offset)
{
    UBoxComponent* HitBox = CreateDefaultSubobject<UBoxComponent>(Name);
    HitBox->SetupAttachment(GetMesh(), BoneName);
    HitBox->SetBoxExtent(Extent);
    HitBox->SetRelativeLocation(Offset);
    HitBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    HitBox->SetCollisionObjectType(OBCombatCollision::PlayerShotTarget);
    HitBox->SetCollisionResponseToAllChannels(ECR_Ignore);
    HitBox->SetCollisionResponseToChannel(HitTraceChannel, ECR_Block);
    HitBox->SetGenerateOverlapEvents(false);
    HitBox->SetCanEverAffectNavigation(false);
    HitBox->SetSimulatePhysics(false);
    HitBox->ComponentTags.Add(ZoneTag);
    HitBox->bEditableWhenInherited = true;
    HitBoxes.Add(HitBox);
    HitBoxBones.Add(Name, BoneName);
    return HitBox;
}

void AOBEnemyCharacter::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    ConfigureHitBoxes();
    UpdateArchetypeVisuals();
    AttachEnemyAudioOrigin();
}

void AOBEnemyCharacter::BeginPlay()
{
    // Socket-attached weapons and shot boxes grow with the mesh, while the
    // authored feet origin and navigation capsule keep their placement.
    GetMesh()->SetRelativeScale3D(GetMesh()->GetRelativeScale3D()*CombatVisualScale);
    ConfigureEnemyArchetype(EnemyArchetype);
    CurrentHealth = FMath::Max(1.0f, MaxHealth);
    ConfigureHitBoxes();
    Super::BeginPlay();

    for (UBoxComponent* HitBox : HitBoxes)
    {
        if (!IsValid(HitBox)) continue;
        if (GetMesh()->DoesSocketExist(HitBox->GetAttachSocketName())) continue;

        HitBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        UE_LOG(LogOBEnemy, Warning, TEXT("%s | HitBox=%s disabled: missing bone/socket '%s'"),
            *GetName(), *HitBox->GetName(), *HitBox->GetAttachSocketName().ToString());
    }
}

void AOBEnemyCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    StopEnemyAudio();
    StopCombatPattern();
    StopArchetypeAttack();
    Super::EndPlay(EndPlayReason);
}

void AOBEnemyCharacter::UpdateArchetypeVisuals()
{
    const bool bHasRPGModel=RPGWeaponMesh && RPGWeaponMesh->GetSkeletalMeshAsset();
    if (RPGLauncherMesh) RPGLauncherMesh->SetVisibility(IsRPGTrooper() && !bHasRPGModel, true);
    if (RPGWeaponMesh)
    {
        RPGWeaponMesh->SetVisibility(IsRPGTrooper() && bHasRPGModel,true);
        if (IsRPGTrooper() && bHasRPGModel && RPGWeaponShotReloadAnimation)
        {
            RPGWeaponMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
            RPGWeaponMesh->SetAnimation(RPGWeaponShotReloadAnimation);
            RPGWeaponMesh->SetPosition(0.f,false);RPGWeaponMesh->Stop();
        }
    }
    if (!WeaponMesh) return;
    if (IsRPGTrooper())
    {
        if (!bRPGVisualsApplied) bSavedRifleVisibility = WeaponMesh->IsVisible();
        WeaponMesh->SetVisibility(false, true);
        bRPGVisualsApplied = true;
    }
    else if (bRPGVisualsApplied)
    {
        WeaponMesh->SetVisibility(bSavedRifleVisibility, true);
        bRPGVisualsApplied = false;
    }
}

UMeshComponent* AOBEnemyCharacter::GetRPGVisualMesh() const
{
    return RPGWeaponMesh && RPGWeaponMesh->GetSkeletalMeshAsset()
        ? static_cast<UMeshComponent*>(RPGWeaponMesh.Get()) : static_cast<UMeshComponent*>(RPGLauncherMesh.Get());
}

void AOBEnemyCharacter::ConfigureEnemyArchetype(EOBEnemyArchetype InArchetype)
{
    const bool bUnchanged = bArchetypeConfigured && EnemyArchetype == InArchetype;
    EnemyArchetype = InArchetype;
    DirectAimMinPercent=DirectAimMaxPercent=IsRPGTrooper() ? 25 : 2;
    UpdateArchetypeVisuals();
    if (bUnchanged) return; // Repeated preparation must not heal an enemy in combat.
    StopArchetypeAttack();
    if (IsRPGTrooper())
    {
        if (!bSavedRifleDefaults)
        {
            SavedRifleMaxHealth = MaxHealth;
            bSavedRifleDefaults = true;
        }
        MaxHealth = 150.f;
        CurrentHealth = MaxHealth;
    }
    else if (bSavedRifleDefaults)
    {
        MaxHealth = SavedRifleMaxHealth;
        CurrentHealth = MaxHealth;
    }
    bArchetypeConfigured = true;
}

bool AOBEnemyCharacter::CanRPGAttack(bool bRequireCombatPhase) const
{
    if (!IsRPGTrooper() || bDead || CombatState == EOBEnemyCombatState::Dead || !bEncounterCombatant || !CanBeDamaged()) return false;
    const auto* AI = Cast<AOBAIController>(GetController());
    if (!AI || !AI->HasReachedStagingPoint()) return false;
    if (const auto* Player = Cast<AOBPlayerCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0)))
    {
        const auto* Stage = Player->GetStageFlowComponent();
        if (Stage && Stage->IsStageActive() && Stage->GetPhase() != EOBStagePhase::Combat
            && (bRequireCombatPhase || Stage->GetPhase() != EOBStagePhase::Travel)) return false;
        if (Player->GetActionComponent()->GetActionState() != EOBActionState::Action) return false;
    }
    return true;
}

bool AOBEnemyCharacter::StartArchetypeAttack()
{
    if (!IsRPGTrooper()) return false;
    StartRPGAttack();
    return true;
}

void AOBEnemyCharacter::StartRPGAttack()
{
    // ActionComponent's arrival callback may precede StageFlow's callback on this same delegate.
    // The staged/action-ready enemy may arm its warning during Travel; an actual launch still requires Combat.
    if (bRPGAttackActive || !CanRPGAttack(false)) return;
    // This archetype owns its cadence; it never starts the shared rifle StateTree.
    if (auto* AI = Cast<AOBAIController>(GetController())) AI->StopStateTree();
    bRPGAttackActive = true;
    SetCombatState(EOBEnemyCombatState::PreparingAttack);
    GetWorldTimerManager().SetTimer(RPGAttackTimer, this, &ThisClass::FireRPGRound,
        FMath::Max(.01f, RPGFirstAttackDelay), false);
}

void AOBEnemyCharacter::StopArchetypeAttack()
{
    const bool bWasActive = bRPGAttackActive;
    bRPGAttackActive = bRPGLaunchAuthorized = false;
    if (GetWorld())
    {
        GetWorldTimerManager().ClearTimer(RPGAttackTimer);
        GetWorldTimerManager().ClearTimer(RPGRecoveryTimer);
        GetWorldTimerManager().ClearTimer(RPGLegacyReloadTimer);
    }
    if (bWasActive && !bDead) SetCombatState(EOBEnemyCombatState::Idle);
    if (bWasActive && RPGWeaponMesh) { RPGWeaponMesh->Stop();RPGWeaponMesh->SetPosition(0.f,false); }
}

void AOBEnemyCharacter::FireRPGRound()
{
    if (!bRPGAttackActive || !CanRPGAttack()) { StopArchetypeAttack(); return; }
    SetCombatState(EOBEnemyCombatState::PreparingAttack);
    SetCombatState(EOBEnemyCombatState::Attacking);
    if (RPGWeaponMesh && RPGWeaponMesh->GetSkeletalMeshAsset() && RPGWeaponShotReloadAnimation)
        RPGWeaponMesh->PlayAnimation(RPGWeaponShotReloadAnimation,false);
    bRPGLaunchAuthorized = true;
    FireBullet();
    bRPGLaunchAuthorized = false;
    GetWorldTimerManager().SetTimer(RPGLegacyReloadTimer,FTimerDelegate::CreateLambda([this]() {
        if (bRPGAttackActive && CanRPGAttack()) SetCombatState(EOBEnemyCombatState::Reloading);
    }),.35f,false);
    GetWorldTimerManager().SetTimer(RPGRecoveryTimer, this, &ThisClass::RecoverRPGAim, .6f, false);
    GetWorldTimerManager().SetTimer(RPGAttackTimer, this, &ThisClass::FireRPGRound,
        FMath::Max(.01f, RPGAttackInterval), false);
}

void AOBEnemyCharacter::RecoverRPGAim()
{
    if (bRPGAttackActive && CanRPGAttack()) SetCombatState(EOBEnemyCombatState::PreparingAttack);
    else StopArchetypeAttack();
}

void AOBEnemyCharacter::ConfigureHitBoxes()
{
    HitTraceChannel = OBCombatCollision::PlayerShotTarget;
    GetCapsuleComponent()->SetCollisionResponseToChannel(HitTraceChannel, ECR_Ignore);

    // Includes modular character meshes so they cannot intercept a shot before a hit box.
    TInlineComponentArray<USkeletalMeshComponent*> CharacterMeshes;
    GetComponents(CharacterMeshes);
    for (USkeletalMeshComponent* CharacterMesh : CharacterMeshes)
    {
        CharacterMesh->SetCollisionResponseToChannel(HitTraceChannel, ECR_Ignore);
        // Capsule handles locomotion. Animated limbs must not trap other pawns in a doorway.
        CharacterMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
        CharacterMesh->SetCanEverAffectNavigation(false);
    }

    for (UBoxComponent* HitBox : HitBoxes)
    {
        if (!IsValid(HitBox)) continue;
        if (const FName* BoneName = HitBoxBones.Find(HitBox->GetFName()))
        {
            HitBox->AttachToComponent(GetMesh(), FAttachmentTransformRules::KeepRelativeTransform, *BoneName);
        }
        HitBox->SetCollisionEnabled(bDead ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryOnly);
        HitBox->SetCollisionObjectType(OBCombatCollision::PlayerShotTarget);
        HitBox->SetCollisionResponseToAllChannels(ECR_Ignore);
        HitBox->SetCollisionResponseToChannel(HitTraceChannel, ECR_Block);
        HitBox->SetGenerateOverlapEvents(false);
        HitBox->SetCanEverAffectNavigation(false);
    }
}

float AOBEnemyCharacter::GetHitBoxDamageMultiplier(const UBoxComponent* HitBox) const
{
    if (HitBox->ComponentHasTag(OBHitZone::Head)) return HeadDamageMultiplier;
    if (HitBox->ComponentHasTag(OBHitZone::Body)) return BodyDamageMultiplier;
    if (HitBox->ComponentHasTag(OBHitZone::Arm)) return ArmDamageMultiplier;
    if (HitBox->ComponentHasTag(OBHitZone::Leg)) return LegDamageMultiplier;
    return 0.0f;
}

float AOBEnemyCharacter::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
    if (bDead || CurrentHealth <= 0.0f || !CanBeDamaged() || !FMath::IsFinite(DamageAmount) || DamageAmount <= 0.0f) return 0.0f;

    const UBoxComponent* HitBox = nullptr;
    float ScaledDamage = DamageAmount;
    if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
    {
        const FPointDamageEvent& PointDamage = static_cast<const FPointDamageEvent&>(DamageEvent);
        HitBox = Cast<UBoxComponent>(PointDamage.HitInfo.GetComponent());
        if (!HitBox || !HitBoxes.Contains(HitBox))
        {
            return 0.0f;
        }
        float Multiplier=GetHitBoxDamageMultiplier(HitBox);
        const auto* Trainee=EventInstigator ? Cast<AOBPlayerCharacter>(EventInstigator->GetPawn()) : nullptr;
        if (Trainee && Trainee->GetStageFlowComponent()->IsStageActive())
            Multiplier=HitBox->ComponentHasTag(OBHitZone::Head) ? 1.f : HitBox->ComponentHasTag(OBHitZone::Body) ? .5f : .33f;
        ScaledDamage *= Multiplier;
    }

    if (!FMath::IsFinite(ScaledDamage) || ScaledDamage <= 0.0f) return 0.0f;
    const float AppliedDamage = Super::TakeDamage(ScaledDamage, DamageEvent, EventInstigator, DamageCauser);
    if (!FMath::IsFinite(AppliedDamage) || AppliedDamage <= 0.0f) return 0.0f;

    const float ActualDamage=FMath::Min(CurrentHealth,AppliedDamage);
    CurrentHealth = FMath::Max(0.0f, CurrentHealth - AppliedDamage);
    // Credit capped HP loss before Die() can close the encounter and finalize its score.
    if (auto* Trainee=EventInstigator ? Cast<AOBPlayerCharacter>(EventInstigator->GetPawn()) : nullptr)
    {
        FName Zone=NAME_None;
        if (HitBox) for (FName Tag:{OBHitZone::Head,OBHitZone::Body,OBHitZone::Arm,OBHitZone::Leg})
            if (HitBox->ComponentHasTag(Tag)) { Zone=Tag;break; }
        Trainee->GetStageFlowComponent()->RecordEnemyDamage(this,Zone,ActualDamage,CurrentHealth<=0,EventInstigator);
    }
    UE_LOG(LogOBEnemy, Display, TEXT("%s | HitBox=%s | Damage=%.1f | Health=%.1f/%.1f"),
        *GetName(), *GetNameSafe(HitBox), AppliedDamage, CurrentHealth, MaxHealth);

    if (HitBox)
    {
        PlayHitFeedback(static_cast<const FPointDamageEvent&>(DamageEvent), HitBox, AppliedDamage);
    }

    if (CurrentHealth <= 0.0f) Die();

    return AppliedDamage;
}

void AOBEnemyCharacter::PlayHitFeedback(const FPointDamageEvent& PointDamage, const UBoxComponent* HitBox, float AppliedDamage)
{
    // Keep a local snapshot so Blueprint callbacks cannot change the current notification through reentrant damage.
    FOBEnemyHitFeedback Feedback;
    Feedback.HitBoxName = HitBox->GetFName();
    for (FName ZoneTag : {OBHitZone::Head, OBHitZone::Body, OBHitZone::Arm, OBHitZone::Leg})
    {
        if (HitBox->ComponentHasTag(ZoneTag))
        {
            Feedback.HitZoneTag = ZoneTag;
            break;
        }
    }
    Feedback.ImpactPoint = PointDamage.HitInfo.ImpactPoint;
    Feedback.ImpactNormal = PointDamage.HitInfo.ImpactNormal.GetSafeNormal();
    if (Feedback.ImpactNormal.IsNearlyZero())
    {
        Feedback.ImpactNormal = (-FVector(PointDamage.ShotDirection)).GetSafeNormal();
        if (Feedback.ImpactNormal.IsNearlyZero()) Feedback.ImpactNormal = FVector::UpVector;
    }
    Feedback.Damage = AppliedDamage;
    Feedback.RemainingHealth = CurrentHealth;
    Feedback.bFatal = CurrentHealth <= 0.0f;
    LastHitFeedback = Feedback;

    if (GetNetMode() != NM_DedicatedServer)
    {
        const FVector EffectLocation = Feedback.ImpactPoint + Feedback.ImpactNormal * HitEffectSurfaceOffset;
        const FQuat SurfaceRotation = FRotationMatrix::MakeFromZ(Feedback.ImpactNormal).ToQuat();
        const FRotator EffectRotation = (SurfaceRotation * HitEffectRotationOffset.Quaternion()).Rotator();
        if (HitNiagaraEffect)
        {
            UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, HitNiagaraEffect, EffectLocation, EffectRotation, HitEffectScale);
        }
        else if (HitParticleEffect)
        {
            UGameplayStatics::SpawnEmitterAtLocation(this, HitParticleEffect, EffectLocation, EffectRotation, HitEffectScale);
        }

#if ENABLE_DRAW_DEBUG
        if (bShowHitDebug)
        {
            const FColor Color = Feedback.HitZoneTag == OBHitZone::Head ? FColor::Red
                : Feedback.HitZoneTag == OBHitZone::Body ? FColor::Yellow
                : Feedback.HitZoneTag == OBHitZone::Arm ? FColor::Green : FColor::Cyan;
            DrawDebugBox(GetWorld(), HitBox->GetComponentLocation(), HitBox->GetScaledBoxExtent(),
                HitBox->GetComponentQuat(), Color, false, HitDebugDuration, 0, 1.5f);
            DrawDebugSphere(GetWorld(), Feedback.ImpactPoint, 3.0f, 8, Color, false, HitDebugDuration);
            DrawDebugLine(GetWorld(), Feedback.ImpactPoint, Feedback.ImpactPoint + Feedback.ImpactNormal * 15.0f,
                Color, false, HitDebugDuration, 0, 1.5f);
            if (GEngine)
            {
                GEngine->AddOnScreenDebugMessage(INDEX_NONE, HitDebugDuration, Color,
                    FString::Printf(TEXT("%s | %s | Damage %.1f | HP %.1f/%.1f%s"), *GetName(),
                        *Feedback.HitBoxName.ToString(), AppliedDamage, CurrentHealth, MaxHealth,
                        Feedback.bFatal ? TEXT(" | KILL") : TEXT("")));
            }
        }
#endif
    }

    OnHitConfirmed.Broadcast(Feedback);
}

void AOBEnemyCharacter::SetEncounterCombatant(bool bCombatant)
{
    bEncounterCombatant=bCombatant;
    if (!bCombatant)
    {
        StopEnemyAudio();
        StopArchetypeAttack();
        if (auto* AI=Cast<AOBAIController>(GetController())) AI->StopStateTree();
        GetCharacterMovement()->StopMovementImmediately();
        if (!bDead) SetCombatState(EOBEnemyCombatState::Idle);
    }
}

void AOBEnemyCharacter::SetCombatState(EOBEnemyCombatState NewState)
{
    if (NewState == EOBEnemyCombatState::Idle || NewState == EOBEnemyCombatState::Dead)
        StopArchetypeAttack();
    if (!bEncounterCombatant && NewState!=EOBEnemyCombatState::Idle && NewState!=EOBEnemyCombatState::Dead) return;
    if (bDead && NewState != EOBEnemyCombatState::Dead) return;
    if (CombatState == NewState) return;

    UOBEnemyAnimInstance* AnimInstance = Cast<UOBEnemyAnimInstance>(GetMesh()->GetAnimInstance());

    if (CombatState == EOBEnemyCombatState::Attacking && AnimInstance)
    {
        AnimInstance->StopAttackMontage(0.15f);
    }

    CombatState = NewState;
    if (NewState==EOBEnemyCombatState::Reloading) PlayEnemyReloadSound();
    if (NewState == EOBEnemyCombatState::InCover || (bPatternCombatActive && CombatPosture!=EOBEnemyPosture::Stand)) Crouch();
    else if (NewState != EOBEnemyCombatState::Dead) UnCrouch();

    if (CombatState == EOBEnemyCombatState::Attacking && AnimInstance && !bPatternCombatActive)
    {
        AnimInstance->PlayAttackMontage();
    }

    UE_LOG(
        LogOBEnemy,
        Display,
        TEXT("%s | CombatState=%s"),
        *GetName(),
        *UEnum::GetValueAsString(CombatState)
    );
}

FVector AOBEnemyCharacter::GetDirectAimTarget(const ACharacter& Player) const
{
    // Cover lowers the actual capsule. RPG must aim inside that capsule rather than at the old standing chest height.
    // Preserve the rifle's authored aim point and miss offsets.
    const float TargetHeight = IsRPGTrooper()
        ? FMath::Clamp(AimHeightOffset, 0.f, Player.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * .5f)
        : AimHeightOffset;
    return Player.GetActorLocation() + FVector::UpVector * TargetHeight;
}

FVector AOBEnemyCharacter::SelectShotAimLocation(const ACharacter& Player, int32 ProbabilityRoll, int32 SurroundingIndex)
{
    const FVector PlayerAimLocation = GetDirectAimTarget(Player);
    const FRotator PlayerYaw(0.0f, Player.GetActorRotation().Yaw, 0.0f);
    SurroundingAimPoints.Reset(SurroundingAimOffsets.Num());
    for (const FVector& Offset : SurroundingAimOffsets)
    {
        SurroundingAimPoints.Add(PlayerAimLocation + PlayerYaw.RotateVector(Offset));
    }

    const int32 Minimum=FMath::Clamp(FMath::Min(DirectAimMinPercent,DirectAimMaxPercent),0,100);
    const int32 Maximum=FMath::Clamp(FMath::Max(DirectAimMinPercent,DirectAimMaxPercent),0,100);
    LastShotAimChancePercent=FMath::RandRange(Minimum,Maximum);
    bLastShotAimedAtPlayer=LastShotAimChancePercent>0 && FMath::Clamp(ProbabilityRoll,1,100)<=LastShotAimChancePercent;
    // An empty miss-point array must not silently turn a missed roll into a direct aim.
    if (SurroundingAimPoints.IsEmpty()) SurroundingAimPoints.Add(PlayerAimLocation+PlayerYaw.RotateVector(FVector(0,150,0)));
    if (bLastShotAimedAtPlayer) return PlayerAimLocation;
    return SurroundingAimPoints[FMath::Clamp(SurroundingIndex, 0, SurroundingAimPoints.Num() - 1)];
}

void AOBEnemyCharacter::FireBullet()
{
    if (EnemyArchetype==EOBEnemyArchetype::HostageTaker) return;
    if (!bEncounterCombatant) return;
    if (bDead) return;
    if (CombatState != EOBEnemyCombatState::Attacking) return;
    // StateTree burst tasks own the exact shot count; animation notifies cannot add another shot.
    if (bPatternCombatActive && !bPatternShotAuthorized) return;
    if (IsRPGTrooper() && !bPatternCombatActive && (!bRPGAttackActive || !bRPGLaunchAuthorized)) return;

    const FTransform MuzzleTransform = IsRPGTrooper()
        ? FTransform(GetActorRotation(), GetRPGVisualMesh()->GetComponentTransform().TransformPosition(RPGMuzzleOffset))
        : WeaponMesh->GetSocketTransform(MuzzleSocketName, RTS_World);

    FVector AimLocation = MuzzleTransform.GetLocation() + MuzzleTransform.GetRotation().GetForwardVector() * 1000.0f;
    SurroundingAimPoints.Reset();
    bLastShotAimedAtPlayer = false;
    LastShotAimChancePercent = 0;

    if (const ACharacter* PlayerCharacter = UGameplayStatics::GetPlayerCharacter(this, 0))
    {
        AimLocation = SelectShotAimLocation(*PlayerCharacter, FMath::RandRange(1,100), FMath::RandHelper(SurroundingAimOffsets.Num()));
    }

    if (bDrawShotAimDebug)
    {
        for (const FVector& Point : SurroundingAimPoints)
        {
            DrawDebugSphere(GetWorld(), Point, 12.0f, 8, FColor::Cyan, false, 1.0f);
        }
        const FColor AimColor = bLastShotAimedAtPlayer ? FColor::Red : FColor::Yellow;
        DrawDebugSphere(GetWorld(), AimLocation, 18.0f, 12, AimColor, false, 1.0f);
        DrawDebugLine(GetWorld(), MuzzleTransform.GetLocation(), AimLocation, AimColor, false, 1.0f);
    }

    const FVector ShotDirection =
        (AimLocation - MuzzleTransform.GetLocation()).GetSafeNormal();

    const FRotator SpawnRotation = ShotDirection.Rotation();

    FActorSpawnParameters SpawnParameters;
    SpawnParameters.Owner = this;
    SpawnParameters.Instigator = this;
    SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    const TSubclassOf<AOBBulletActor> ProjectileClass = IsRPGTrooper() ? RPGRocketClass : BulletClass;
    AOBBulletActor* SpawnedBullet = GetWorld()->SpawnActor<AOBBulletActor>(ProjectileClass, MuzzleTransform.GetLocation(), SpawnRotation, SpawnParameters);
    if (SpawnedBullet) SpawnedBullet->CustomTimeDilation=CustomTimeDilation;
    if (SpawnedBullet && UGameplayStatics::GetPlayerCharacter(this,0))
    {
        // A miss must stay a miss even if its visible flight path crosses the capsule later.
        SpawnedBullet->SetPlayerHitAllowed(bLastShotAimedAtPlayer);
        if (bLastShotAimedAtPlayer)
            if (auto* Rocket=Cast<AOBRPGRocketActor>(SpawnedBullet))
                Rocket->LockOnPlayer(Cast<AOBPlayerCharacter>(UGameplayStatics::GetPlayerCharacter(this,0)));
    }
    if (SpawnedBullet && IsRPGTrooper()) ++RPGShotsFired;
    if (SpawnedBullet && bPatternCombatActive) ++PatternShotsFired;
    if (SpawnedBullet) PlayEnemyFireSound();

    UE_LOG(LogOBEnemy, Display, TEXT("%s | FireBullet=%s | AimChance=%d Direct=%d"), *GetName(), *GetNameSafe(SpawnedBullet),LastShotAimChancePercent,bLastShotAimedAtPlayer);
}

void AOBEnemyCharacter::Die()
{
    if (bDead) return;

    bDead = true;
    SetAimOutline(false);
    StopEnemyAudio();
    SetCanBeDamaged(false);
    SetCombatState(EOBEnemyCombatState::Dead);

    if (AOBAIController* AIController = Cast<AOBAIController>(GetController()))
    {
        AIController->StopStateTree();
    }

    GetCharacterMovement()->StopMovementImmediately();
    GetCharacterMovement()->DisableMovement();
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    for (UBoxComponent* HitBox : HitBoxes)
    {
        if (IsValid(HitBox)) HitBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }

    float MontageDuration = 0.0f;

    if (UOBEnemyAnimInstance* AnimInstance = Cast<UOBEnemyAnimInstance>(GetMesh()->GetAnimInstance()))
    {
        MontageDuration = AnimInstance->PlayDeadMontage();
    }

    if (MontageDuration <= 0.0f)
    {
        DropWeapon();
        BeginDeathPhysics();
    }

    OnKilled.Broadcast(this);
}

void AOBEnemyCharacter::StartStateTree()
{
    if (bDead || !bEncounterCombatant) return;
    if (!CombatPatterns && StartArchetypeAttack()) return;

    if (AOBAIController* AIController = Cast<AOBAIController>(GetController()))
    {
        AIController->StartStateTree();
    }
}

void AOBEnemyCharacter::StartEncounterBehavior()
{
    if (EnemyArchetype==EOBEnemyArchetype::HostageTaker) return;
    if (bDead || !bEncounterCombatant) return;
    if (!CombatPatterns && StartArchetypeAttack()) return;
    if (auto* AIController = Cast<AOBAIController>(GetController())) AIController->StartEncounterBehavior();
}

void AOBEnemyCharacter::DropWeapon()
{
    if (!bDead || bWeaponDropped || !IsValid(WeaponMesh)) return;
    // The launcher remains on the ragdoll; do not detach the hidden rifle and simulate it.
    if (IsRPGTrooper()) { bWeaponDropped = true; return; }
    // Unarmed character subclasses intentionally leave the inherited component empty.
    if (!WeaponMesh->GetSkeletalMeshAsset()) { bWeaponDropped = true; return; }

    if (!WeaponMesh->GetPhysicsAsset())
    {
        UE_LOG(LogOBEnemy, Warning, TEXT("%s | Weapon PhysicsAsset is missing"), *GetName());
        return;
    }

    bWeaponDropped = true;
    WeaponMesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
    WeaponMesh->SetCollisionProfileName(TEXT("PhysicsActor"));
    WeaponMesh->SetCollisionResponseToChannel(HitTraceChannel, ECR_Ignore);
    WeaponMesh->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Ignore);
    WeaponMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    WeaponMesh->SetEnableGravity(true);
    WeaponMesh->SetSimulatePhysics(true);
    WeaponMesh->SetLinearDamping(RagdollLinearDamping);
    WeaponMesh->SetAngularDamping(RagdollAngularDamping);
    WeaponMesh->SetAllPhysicsLinearVelocity(FVector::ZeroVector, false);
    WeaponMesh->SetAllPhysicsAngularVelocityInRadians(FVector::ZeroVector, false);
    WeaponMesh->SetPhysMaterialOverride(GetOrCreateRagdollContactMaterial());
    WeaponMesh->ForEachBodyBelow(NAME_None, true, false, [this](FBodyInstance* Body) { ConfigureRagdollBody(Body); });
    WeaponMesh->WakeAllRigidBodies();
}

UPhysicalMaterial* AOBEnemyCharacter::GetOrCreateRagdollContactMaterial()
{
    if (RagdollContactMaterial) return RagdollContactMaterial;
    auto* RootBody = GetMesh()->GetBodyInstance(RagdollRootBoneName);
    auto* Template = RootBody ? RootBody->GetSimplePhysicalMaterial() : GetMesh()->BodyInstance.GetSimplePhysicalMaterial();
    RagdollContactMaterial = Template
        ? DuplicateObject<UPhysicalMaterial>(Template, this)
        : NewObject<UPhysicalMaterial>(this, NAME_None, RF_Transient);
    if (RagdollContactMaterial)
    {
        RagdollContactMaterial->ClearFlags(RF_Public | RF_Standalone);
        RagdollContactMaterial->SetFlags(RF_Transient);
        auto SafeSurfaceValue = [](float Value, float Fallback) {
            return FMath::IsFinite(Value) ? FMath::Clamp(Value, 0.f, 1.f) : Fallback;
        };
        RagdollContactMaterial->Friction = SafeSurfaceValue(RagdollFriction, .4f);
        RagdollContactMaterial->StaticFriction = SafeSurfaceValue(RagdollStaticFriction, .4f);
        RagdollContactMaterial->Restitution = SafeSurfaceValue(RagdollRestitution, 0.f);
        RagdollContactMaterial->bOverrideFrictionCombineMode = true;
        RagdollContactMaterial->FrictionCombineMode = EFrictionCombineMode::Min;
        RagdollContactMaterial->bOverrideRestitutionCombineMode = true;
        RagdollContactMaterial->RestitutionCombineMode = EFrictionCombineMode::Min;
        // Density, sleep thresholds and other unrelated material settings retain their template values.
        // Duplication/PostLoad can create the Chaos handle before these overrides when async body
        // initialization is enabled. Refresh only this local material's cached native representation.
        FChaosEngineInterface::UpdateMaterial(RagdollContactMaterial->GetPhysicsMaterial(), RagdollContactMaterial);
    }
    return RagdollContactMaterial;
}

void AOBEnemyCharacter::ConfigureRagdollBody(FBodyInstance* BodyInstance)
{
    if (!BodyInstance) return;
    BodyInstance->SetOverrideIterationCounts(true);
    BodyInstance->SetPositionSolverIterationCount(static_cast<uint8>(FMath::Clamp(RagdollPositionSolverIterations, 1, 64)));
    BodyInstance->SetVelocitySolverIterationCount(static_cast<uint8>(FMath::Clamp(RagdollVelocitySolverIterations, 1, 64)));
    BodyInstance->SetProjectionSolverIterationCount(static_cast<uint8>(FMath::Clamp(RagdollProjectionSolverIterations, 0, 8)));
    const float MaxAngular = FMath::IsFinite(RagdollMaxAngularVelocity) ? FMath::Clamp(RagdollMaxAngularVelocity, .1f, 100.f) : 8.f;
    BodyInstance->SetMaxAngularVelocityInRadians(MaxAngular, false);
    if (auto* Material = GetOrCreateRagdollContactMaterial()) BodyInstance->SetPhysMaterialOverride(Material);
}

FVector AOBEnemyCharacter::ResolveInitialRagdollPenetration()
{
    auto* CharacterMesh = GetMesh();
    auto* World = GetWorld();
    auto* Asset = CharacterMesh ? CharacterMesh->GetPhysicsAsset() : nullptr;
    if (!World || !Asset || CharacterMesh->IsAnySimulatingPhysics()) return FVector::ZeroVector;

    // The death notify runs after pose finalization. Read that component-space buffer without
    // refreshing animation here: a refresh from inside PostAnimEvaluation would be reentrant.
    // Work on virtual translations and move the whole pose once before enabling simulation.
    struct FInitialShape
    {
        FVector Center;
        FQuat Rotation;
        FCollisionShape Shape;
    };
    TArray<FInitialShape, TInlineAllocator<40>> Shapes;
    for (const auto& Setup : Asset->SkeletalBodySetups)
    {
        if (!Setup || !CharacterMesh->DoesSocketExist(Setup->BoneName)) continue;
        const FTransform BoneWorld = CharacterMesh->GetSocketTransform(Setup->BoneName, RTS_World);
        if (!BoneWorld.IsValid()) continue;
        const FVector Scale = BoneWorld.GetScale3D();
        const FTransform BoneNoScale(BoneWorld.GetRotation(), BoneWorld.GetLocation());
        for (const auto& Element : Setup->AggGeom.SphereElems)
        {
            if (!CollisionEnabledHasPhysics(Element.GetCollisionEnabled())) continue;
            const auto Scaled = Element.GetFinalScaled(Scale, FTransform::Identity);
            if (!FMath::IsFinite(Scaled.Radius) || Scaled.Radius <= UE_SMALL_NUMBER) continue;
            Shapes.Add({BoneNoScale.TransformPosition(Scaled.Center), FQuat::Identity, FCollisionShape::MakeSphere(Scaled.Radius)});
        }
        for (const auto& Element : Setup->AggGeom.SphylElems)
        {
            if (!CollisionEnabledHasPhysics(Element.GetCollisionEnabled())) continue;
            const auto Scaled = Element.GetFinalScaled(Scale, FTransform::Identity);
            const float HalfHeight = Scaled.Length * .5f + Scaled.Radius;
            if (!FMath::IsFinite(Scaled.Radius) || !FMath::IsFinite(HalfHeight) || Scaled.Radius <= UE_SMALL_NUMBER) continue;
            Shapes.Add({BoneNoScale.TransformPosition(Scaled.Center), BoneWorld.GetRotation() * Scaled.Rotation.Quaternion(),
                FCollisionShape::MakeCapsule(Scaled.Radius, HalfHeight)});
        }
        for (const auto& Element : Setup->AggGeom.BoxElems)
        {
            if (!CollisionEnabledHasPhysics(Element.GetCollisionEnabled())) continue;
            const auto Scaled = Element.GetFinalScaled(Scale, FTransform::Identity);
            const FVector Extent(Scaled.X * .5f, Scaled.Y * .5f, Scaled.Z * .5f);
            if (Extent.ContainsNaN() || Extent.GetMin() <= UE_SMALL_NUMBER) continue;
            Shapes.Add({BoneNoScale.TransformPosition(Scaled.Center), BoneWorld.GetRotation() * Scaled.Rotation.Quaternion(), FCollisionShape::MakeBox(Extent)});
        }
    }

    FCollisionQueryParams Params(SCENE_QUERY_STAT(OBInitialRagdollOverlap), false, this);
    Params.bFindInitialOverlaps = true;
    FCollisionResponseParams Responses(CharacterMesh->GetCollisionResponseToChannels());
    Responses.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);
    TArray<FHitResult> Hits;
    FVector Offset = FVector::ZeroVector;
    float RemainingDistance = 100.f;
    constexpr float SeparationSlop = .5f;
    for (int32 Pass = 0; Pass < 8 && RemainingDistance > UE_KINDA_SMALL_NUMBER; ++Pass)
    {
        FVector Correction = FVector::ZeroVector;
        for (const auto& Shape : Shapes)
        {
            const FVector Position = Shape.Center + Offset;
            Hits.Reset();
            World->SweepMultiByChannel(Hits, Position, Position, Shape.Rotation, ECC_PhysicsBody, Shape.Shape, Params, Responses);
            for (const auto& Hit : Hits)
            {
                if (!Hit.bBlockingHit || !Hit.bStartPenetrating || !FMath::IsFinite(Hit.PenetrationDepth)
                    || Hit.PenetrationDepth <= UE_KINDA_SMALL_NUMBER || Cast<AOBBulletActor>(Hit.GetActor())) continue;
                FVector Normal = FVector(Hit.Normal).GetSafeNormal();
                if (Normal.IsNearlyZero() || Normal.ContainsNaN()) continue;
                // A limb can finish the montage on the far side of a thin platform. Resolve
                // toward the capsule's side, rather than pushing that linked limb farther through.
                if (FVector::DotProduct(GetActorLocation() - FVector(Hit.ImpactPoint), Normal) < 0.f) Normal = -Normal;
                const double MissingDistance = Hit.PenetrationDepth + SeparationSlop - FVector::DotProduct(Correction, Normal);
                if (MissingDistance > 0.) Correction += Normal * MissingDistance;
            }
        }
        if (Correction.ContainsNaN() || Correction.IsNearlyZero()) break;
        Correction = Correction.GetClampedToMaxSize(RemainingDistance);
        Offset += Correction;
        RemainingDistance -= Correction.Size();
    }
    if (!Offset.IsNearlyZero())
    {
        CharacterMesh->AddWorldOffset(Offset, false, nullptr, ETeleportType::TeleportPhysics);
        // Transform propagation may defer kinematic body updates. Commit this same finalized
        // pose before bodies become simulated, without evaluating animation or its notifies again.
        CharacterMesh->UpdateKinematicBonesToAnim(CharacterMesh->GetComponentSpaceTransforms(), ETeleportType::TeleportPhysics,
            false, EAllowKinematicDeferral::DisallowDeferral);
    }
    return Offset;
}

void AOBEnemyCharacter::BeginDeathPhysics()
{
    if (!bDead || bDeathPhysicsStarted) return;

    USkeletalMeshComponent* CharacterMesh = GetMesh();

    bDeathPhysicsStarted = true;
    DropWeapon();

    CharacterMesh->bPauseAnims = true;
    CharacterMesh->SetCollisionProfileName(TEXT("Ragdoll"));
    CharacterMesh->SetCollisionResponseToChannel(HitTraceChannel, ECR_Ignore);
    CharacterMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    ResolveInitialRagdollPenetration();
    CharacterMesh->SetAllBodiesBelowSimulatePhysics(RagdollRootBoneName, true, true);
    CharacterMesh->SetAllMassScale(RagdollMassScale);
    CharacterMesh->SetPhysMaterialOverride(GetOrCreateRagdollContactMaterial());
    CharacterMesh->ForEachBodyBelow(RagdollRootBoneName, true, false, [this](FBodyInstance* BodyInstance)
    {
        if (!BodyInstance) return;

        BodyInstance->LinearDamping = RagdollLinearDamping;
        BodyInstance->AngularDamping = RagdollAngularDamping;
        BodyInstance->UpdateDampingProperties();
        BodyInstance->SetMaxDepenetrationVelocity(RagdollMaxDepenetrationVelocity);
        BodyInstance->SetLinearVelocity(FVector::ZeroVector, false);
        BodyInstance->SetAngularVelocityInRadians(FVector::ZeroVector, false);
        ConfigureRagdollBody(BodyInstance);
    });

    if (FBodyInstance* RootBodyInstance = CharacterMesh->GetBodyInstance(RagdollRootBoneName))
    {
        // Ignore the legacy serialized flag. A world anchor cannot resolve a wall's separation constraint.
        RootBodyInstance->bLockTranslation = RootBodyInstance->bLockRotation = false;
        RootBodyInstance->bLockXTranslation = RootBodyInstance->bLockYTranslation = RootBodyInstance->bLockZTranslation = false;
        RootBodyInstance->bLockXRotation = RootBodyInstance->bLockYRotation = RootBodyInstance->bLockZRotation = false;
        RootBodyInstance->SetDOFLock(EDOFMode::None);
    }
    for (FConstraintInstance* Constraint : CharacterMesh->Constraints)
    {
        if (!Constraint) continue;
        const auto Profile = Constraint->ProfileInstance;
        // Preserve authored angular ranges and tuning, but avoid elastic angular recovery on this corpse.
        Constraint->SetSoftSwingLimitParams(false, Profile.ConeLimit.Stiffness, Profile.ConeLimit.Damping,
            Profile.ConeLimit.Restitution, Profile.ConeLimit.ContactDistance);
        Constraint->SetSoftTwistLimitParams(false, Profile.TwistLimit.Stiffness, Profile.TwistLimit.Damping,
            Profile.TwistLimit.Restitution, Profile.TwistLimit.ContactDistance);
        Constraint->SetProjectionParams(false, Profile.ProjectionLinearAlpha, Profile.ProjectionAngularAlpha,
            Profile.ProjectionLinearTolerance, Profile.ProjectionAngularTolerance);
        Constraint->SetShockPropagationParams(false, Profile.ShockPropagationAlpha);
    }

    CharacterMesh->SetAllBodiesBelowPhysicsBlendWeight(RagdollRootBoneName, 0.0f, false, true);
    CharacterMesh->WakeAllRigidBodies();

    DeathPhysicsElapsed = 0.0f;
    GetWorldTimerManager().SetTimer(DeathPhysicsBlendTimer, this, &ThisClass::UpdateDeathPhysicsBlend, 0.02f, true);
}

void AOBEnemyCharacter::UpdateDeathPhysicsBlend()
{
    constexpr float BlendInterval = 0.02f;

    DeathPhysicsElapsed += BlendInterval;

    const float NormalizedTime = FMath::Clamp(DeathPhysicsElapsed / DeathPhysicsBlendDuration, 0.f, 1.f);

    const float BlendWeight = FMath::InterpEaseInOut(0.0f, 1.0f, NormalizedTime, 2.0f);

    GetMesh()->SetAllBodiesBelowPhysicsBlendWeight(RagdollRootBoneName, BlendWeight, false, true);

    if (NormalizedTime >= 1.0f)
    {
        GetWorldTimerManager().ClearTimer(DeathPhysicsBlendTimer);
    }
}

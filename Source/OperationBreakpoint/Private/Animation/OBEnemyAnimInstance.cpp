#include "Animation/OBEnemyAnimInstance.h"

#include "GameFramework/CharacterMovementComponent.h"
#include "Animation/AnimSequence.h"
#include "AI/OBAIController.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Stage/OBStageFlowComponent.h"

void UOBEnemyAnimInstance::NativeInitializeAnimation()
{
    Super::NativeInitializeAnimation();

    EnemyCharacter = Cast<AOBEnemyCharacter>(TryGetPawnOwner());
    ResetEnemyAimContext();
}

void UOBEnemyAnimInstance::ResetEnemyAimContext()
{
    EnemyAimContext = FOBEnemyAimContext();
    AimPitch = AimYaw = AimAlpha = 0.f;
}

void UOBEnemyAnimInstance::UpdateEnemyAimContext(float DeltaSeconds)
{
    check(IsInGameThread());
    EnemyCharacter = Cast<AOBEnemyCharacter>(TryGetPawnOwner());
    auto* Enemy = EnemyCharacter.Get();
    auto* Mesh = Enemy ? Enemy->GetMesh() : nullptr;
    auto* Movement = Enemy ? Enemy->GetCharacterMovement() : nullptr;
    const auto* AI = Enemy ? Cast<AOBAIController>(Enemy->GetController()) : nullptr;
    const EOBEnemyCombatState State = Enemy ? Enemy->GetCombatState() : EOBEnemyCombatState::Idle;
    const bool bAimState = State == EOBEnemyCombatState::PreparingAttack || State == EOBEnemyCombatState::Attacking
        || State == EOBEnemyCombatState::InCover || State == EOBEnemyCombatState::Reloading;
    const auto* Rifle = Enemy ? Enemy->GetWeaponMesh() : nullptr;
    const auto* Launcher = Enemy ? Enemy->GetRPGVisualMesh() : nullptr;
    const bool bAimWeapon = Enemy && Enemy->IsRPGTrooper()
        ? Launcher && Launcher->IsVisible() && !Launcher->bHiddenInGame
            && Launcher->GetAttachParent() == Mesh && !Launcher->IsSimulatingPhysics()
        : Rifle && Rifle->GetSkeletalMeshAsset() && Rifle->IsVisible() && !Rifle->bHiddenInGame
            && Rifle->GetAttachParent() == Mesh && !Rifle->IsSimulatingPhysics();
    auto* Player = Cast<ACharacter>(UGameplayStatics::GetPlayerCharacter(this, 0));
    bool bStageAllowsAim = true;
    if (const auto* OBPlayer = Cast<AOBPlayerCharacter>(Player))
    {
        const auto* Stage = OBPlayer->GetStageFlowComponent();
        bStageAllowsAim = !OBPlayer->GetPlayerSplineComponent()->IsCameraTuningForQA()
            && (!Stage || !Stage->IsStageActive() || Stage->GetPhase() == EOBStagePhase::Combat
                || (AI && AI->IsZone6GuardScene() && Stage->GetPhase()!=EOBStagePhase::QA
                    && Stage->GetPhase()!=EOBStagePhase::Ending && Stage->GetPhase()!=EOBStagePhase::Complete));
    }
    const bool bRPGReloadPose=Enemy && Enemy->IsRPGTrooper() && bPlayingRPGShot && RPGShotElapsed>.35f;
    const bool bPatternNotAiming=Enemy && Enemy->IsCombatPatternActive()
        && Enemy->GetPatternAction()!=EOBEnemyPatternAction::Aim && Enemy->GetPatternAction()!=EOBEnemyPatternAction::Fire;
    if (!bEnableUpperBodyAim || bPatternNotAiming || bRPGReloadPose || !IsValid(Enemy) || !IsValid(Player) || !Mesh || !Movement
        || !Enemy->IsEncounterCombatant() || Enemy->GetCurrentHealth() <= 0.f || !bAimState || !bAimWeapon
        || !AI || !AI->HasReachedStagingPoint() || !bStageAllowsAim
        || Mesh->IsAnySimulatingPhysics() || Movement->IsFalling() || Enemy->GetVelocity().SizeSquared2D() > 9.f)
    {
        // No stale upper-body transform may survive into locomotion, noncombat, QA or ragdoll.
        ResetEnemyAimContext();
        return;
    }
    const FVector Origin = Mesh->DoesSocketExist(TEXT("spine_03"))
        ? Mesh->GetSocketLocation(TEXT("spine_03")) : Enemy->GetActorLocation() + FVector(0, 0, 50.f);
    const auto* SceneTarget=AI ? Cast<ACharacter>(AI->GetZone6FacingTarget()) : nullptr;
    const FVector RawTarget = SceneTarget && SceneTarget!=Player && SceneTarget->GetMesh()->DoesSocketExist(TEXT("head"))
        ? SceneTarget->GetMesh()->GetSocketLocation(TEXT("head")) : Enemy->GetDirectAimTarget(*Player);
    const FVector ToTarget = RawTarget - Origin;
    const float Distance = ToTarget.Size();
    const FTransform MeshToWorld = Mesh->GetComponentTransform();
    if (Origin.ContainsNaN() || RawTarget.ContainsNaN() || !FMath::IsFinite(Distance) || Distance < 1.f || !MeshToWorld.IsValid())
    {
        ResetEnemyAimContext();
        return;
    }
    const FQuat BodyYaw = FRotator(0, Enemy->GetActorRotation().Yaw, 0).Quaternion();
    const FRotator Desired = BodyYaw.UnrotateVector(ToTarget).Rotation();
    const float PitchLimit = FMath::IsFinite(MaxAimPitch) ? FMath::Clamp(MaxAimPitch, 0.f, 89.f) : 70.f;
    const float YawLimit = FMath::IsFinite(MaxAimYaw) ? FMath::Clamp(MaxAimYaw, 0.f, 89.f) : 60.f;
    const float Delta = FMath::IsFinite(DeltaSeconds) ? FMath::Max(0.f, DeltaSeconds) : 0.f;
    const float Speed = FMath::IsFinite(AimInterpSpeed) ? FMath::Max(0.f, AimInterpSpeed) : 8.f;
    const float Blend = FMath::IsFinite(AimBlendSeconds) ? FMath::Max(.01f, AimBlendSeconds) : .15f;
    AimPitch = FMath::FInterpTo(AimPitch, FMath::Clamp(FRotator::NormalizeAxis(Desired.Pitch), -PitchLimit, PitchLimit), Delta, Speed);
    AimYaw = FMath::FInterpTo(AimYaw, FMath::Clamp(FRotator::NormalizeAxis(Desired.Yaw), -YawLimit, YawLimit), Delta, Speed);
    AimAlpha = FMath::FInterpConstantTo(AimAlpha, 1.f, Delta, 1.f / Blend);
    EnemyAimContext.bActive = true;
    EnemyAimContext.OriginWorld = Origin;
    EnemyAimContext.RawTargetWorld = RawTarget;
    // The actual pose solver follows these clamped, smoothed angles rather than bypassing the AnimBP fields.
    EnemyAimContext.TargetWorld = Origin + BodyYaw.RotateVector(FRotator(AimPitch, AimYaw, 0).Vector() * Distance);
    EnemyAimContext.MeshToWorld = MeshToWorld;
    EnemyAimContext.TargetComponentSpace = MeshToWorld.InverseTransformPosition(EnemyAimContext.TargetWorld);
    EnemyAimContext.AimPitch = AimPitch;
    EnemyAimContext.AimYaw = AimYaw;
    EnemyAimContext.Alpha = AimAlpha;
    EnemyAimContext.MaxPitch = PitchLimit;
    EnemyAimContext.MaxYaw = YawLimit;
}

void UOBEnemyAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);

    if (!IsValid(EnemyCharacter)) return;

    FVector HorizontalVelocity = EnemyCharacter->GetVelocity();
    HorizontalVelocity.Z = 0.0f;

    GroundSpeed = HorizontalVelocity.Size();
    bIsMoving = GroundSpeed > 3.0f;
    bIsInAir = EnemyCharacter->GetCharacterMovement()->IsFalling();
    CombatState = EnemyCharacter->GetCombatState();
    PatternAnimationElapsed+=FMath::Max(0.f,DeltaSeconds);
    if (bPlayingRPGShot)
    {
        RPGShotElapsed+=FMath::Max(0.f,DeltaSeconds);
        auto* Sequence=EnemyCharacter->RPGShotReloadAnimation.Get();
        if (!Sequence || RPGShotElapsed>=Sequence->GetPlayLength() || !EnemyCharacter->IsRPGAttackActive())
        { bPlayingRPGShot=false;RPGShotElapsed=0.f; }
    }
}

UAnimSequence* UOBEnemyAnimInstance::GetRPGPoseSequence() const
{
    if (EnemyCharacter && EnemyCharacter->IsCombatPatternActive()) return nullptr;
    return EnemyCharacter && EnemyCharacter->IsRPGTrooper() && EnemyCharacter->IsEncounterCombatant()
        && EnemyCharacter->GetCombatState()!=EOBEnemyCombatState::Dead && !bIsMoving && !bIsInAir
        ? EnemyCharacter->RPGShotReloadAnimation.Get() : nullptr;
}

FOBEnemyPatternPose UOBEnemyAnimInstance::GetPatternPose() const
{
    FOBEnemyPatternPose Pose;
    const auto* Enemy=EnemyCharacter.Get();
    if (!Enemy || !Enemy->IsCombatPatternActive() || !Enemy->CombatPatterns || bIsMoving || bIsInAir
        || Enemy->GetCombatState()==EOBEnemyCombatState::Dead) return Pose;
    Pose.Base=Enemy->GetPostureSequence();
    const auto Action=Enemy->GetPatternAction();
    if (Pose.Base && !Enemy->IsRPGTrooper() && Enemy->GetCombatPosture()!=EOBEnemyPosture::Prone)
        Pose.BaseTime=FMath::Fmod(PatternAnimationElapsed,FMath::Max(.01f,Pose.Base->GetPlayLength()));
    if (Action==EOBEnemyPatternAction::Posture && Enemy->IsPostureTransitioning())
    {
        if (auto* Transition=Enemy->GetPostureTransitionSequence())
        { Pose.Base=Transition;Pose.BaseTime=FMath::Min(PatternAnimationElapsed,Transition->GetPlayLength()); }
        else Pose.BlendAlpha=FMath::Clamp(PatternAnimationElapsed/FMath::Max(.01f,Enemy->CombatPatterns->PostureBlendSeconds),0.f,1.f);
    }
    if (auto* Sequence=Enemy->GetPatternActionSequence())
    {
        const float Offset=Enemy->IsRPGTrooper() && Action==EOBEnemyPatternAction::Reload
            ? Enemy->CombatPatterns->RPGReloadStartSeconds : 0.f;
        const float Time=FMath::Min(Offset+PatternAnimationElapsed,Sequence->GetPlayLength());
        if (Enemy->IsRPGTrooper()) { Pose.Base=Sequence;Pose.BaseTime=Time; }
        else { Pose.Action=Sequence;Pose.ActionTime=Time;Pose.bArmsOnly=Enemy->GetCombatPosture()==EOBEnemyPosture::Prone; }
    }
    return Pose;
}

float UOBEnemyAnimInstance::PlayAttackMontage()
{
    if (EnemyCharacter && EnemyCharacter->IsRPGTrooper() && EnemyCharacter->RPGShotReloadAnimation)
    {
        RPGShotElapsed=0.f;bPlayingRPGShot=true;
        return EnemyCharacter->RPGShotReloadAnimation->GetPlayLength();
    }
    if (!AttackMontage) return 0.0f;
    

    const float Duration = Montage_Play(AttackMontage);

    return Duration;
}

float UOBEnemyAnimInstance::PlayDeadMontage()
{
    if (!DeadMontage) return 0.0f;
    return Montage_Play(DeadMontage);
}

void UOBEnemyAnimInstance::StopAttackMontage(float BlendOutTime)
{
    if (!AttackMontage || !Montage_IsPlaying(AttackMontage)) return;

    Montage_Stop(BlendOutTime, AttackMontage);
}

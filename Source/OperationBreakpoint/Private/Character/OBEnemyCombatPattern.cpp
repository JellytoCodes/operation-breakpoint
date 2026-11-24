#include "AI/OBEnemyCombatPattern.h"
#include "Character/OBEnemyCharacter.h"
#include "AI/OBAIController.h"
#include "Animation/OBEnemyAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBActionComponent.h"
#include "Stage/OBStageFlowComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet/GameplayStatics.h"

EOBEnemyCombatPattern AOBEnemyCharacter::ResolveCombatPattern() const
{
    if (IsRPGTrooper()) return EOBEnemyCombatPattern::RPG;
    const auto Choice=EncounterBehavior.CombatPattern;
    if (Choice!=EOBEnemyCombatPattern::Automatic)
        return Choice==EOBEnemyCombatPattern::RPG ? EOBEnemyCombatPattern::Stand : Choice;
    return EncounterBehavior.CoverMode==EOBEnemyCoverMode::None
        ? EOBEnemyCombatPattern::Stand : EOBEnemyCombatPattern::WindowCover;
}

bool AOBEnemyCharacter::CanRunCombatPattern(bool bAllowArrivalFrame) const
{
    const auto* AI=Cast<AOBAIController>(GetController());
    if (bDead || CurrentHealth<=0.f || !bEncounterCombatant || !CanBeDamaged()
        || CombatState==EOBEnemyCombatState::Dead || !AI || !AI->HasReachedStagingPoint()) return false;
    if (const auto* Player=Cast<AOBPlayerCharacter>(UGameplayStatics::GetPlayerCharacter(this,0)))
    {
        if (Player->GetActionComponent()->GetActionState()!=EOBActionState::Action) return false;
        const auto* Stage=Player->GetStageFlowComponent();
        if (Stage && Stage->IsStageActive() && Stage->GetPhase()!=EOBStagePhase::Combat
            && (!bAllowArrivalFrame || Stage->GetPhase()!=EOBStagePhase::Travel)) return false;
    }
    return true;
}

void AOBEnemyCharacter::SetCombatPosture(EOBEnemyPosture Posture)
{
    PreviousCombatPosture=CombatPosture;
    CombatPosture=Posture;
    bPostureTransitioning=PreviousCombatPosture!=CombatPosture;
    if (Posture==EOBEnemyPosture::Stand) UnCrouch();
    else Crouch(); // Keep a low capsule; the authored prone pose and bone hitboxes provide the prone body.
}

void AOBEnemyCharacter::BeginCombatPattern()
{
    StopArchetypeAttack();
    bPatternCombatActive=true;bPatternShotAuthorized=false;
    if (auto* Anim=Cast<UOBEnemyAnimInstance>(GetMesh()->GetAnimInstance())) Anim->StopAttackMontage(0.f);
}

void AOBEnemyCharacter::StopCombatPattern()
{
    StopEnemyAudio();
    const bool WasActive=bPatternCombatActive;
    bPatternCombatActive=bPatternShotAuthorized=bPostureTransitioning=false;
    if (auto* Anim=Cast<UOBEnemyAnimInstance>(GetMesh()->GetAnimInstance())) Anim->StopAttackMontage(0.f);
    if (WasActive && !bDead && CombatState!=EOBEnemyCombatState::Dead) SetCombatState(EOBEnemyCombatState::Idle);
}

UAnimSequence* AOBEnemyCharacter::GetPostureSequence() const
{
    if (!CombatPatterns) return nullptr;
    if (IsRPGTrooper()) return RPGShotReloadAnimation;
    if (CombatPosture==EOBEnemyPosture::Crouch) return CombatPatterns->CrouchAim;
    if (CombatPosture==EOBEnemyPosture::Prone) return CombatPatterns->ProneAim;
    return CombatPatterns->StandAim;
}

UAnimSequence* AOBEnemyCharacter::GetPostureTransitionSequence() const
{
    if (!CombatPatterns || IsRPGTrooper() || !bPostureTransitioning) return nullptr;
    if (CombatPosture==EOBEnemyPosture::Crouch && PreviousCombatPosture==EOBEnemyPosture::Stand) return CombatPatterns->CrouchEntry;
    if (CombatPosture==EOBEnemyPosture::Stand && PreviousCombatPosture==EOBEnemyPosture::Crouch) return CombatPatterns->CrouchExit;
    return nullptr;
}

UAnimSequence* AOBEnemyCharacter::GetPatternActionSequence() const
{
    if (PatternAction!=EOBEnemyPatternAction::Fire && PatternAction!=EOBEnemyPatternAction::Reload) return nullptr;
    if (IsRPGTrooper()) return RPGShotReloadAnimation;
    return !CombatPatterns ? nullptr : PatternAction==EOBEnemyPatternAction::Fire ? CombatPatterns->RifleFire : CombatPatterns->RifleReload;
}

float AOBEnemyCharacter::BeginPatternAction(EOBEnemyPatternAction Action,EOBEnemyPosture Posture)
{
    PatternAction=Action;
    if (Action==EOBEnemyPatternAction::Posture) SetCombatPosture(Posture);
    else bPostureTransitioning=false;
    float Duration=.01f;
    switch (Action)
    {
    case EOBEnemyPatternAction::Posture:
        SetCombatState(CombatPosture==EOBEnemyPosture::Stand ? EOBEnemyCombatState::PreparingAttack : EOBEnemyCombatState::InCover);
        if (auto* Sequence=GetPostureTransitionSequence()) Duration=Sequence->GetPlayLength();
        else Duration=CombatPatterns ? CombatPatterns->PostureBlendSeconds : .5f;
        break;
    case EOBEnemyPatternAction::Aim:
        SetCombatState(EOBEnemyCombatState::PreparingAttack);
        if (IsRPGTrooper() && RPGWeaponMesh) { RPGWeaponMesh->Stop();RPGWeaponMesh->SetPosition(0.f,false); }
        Duration=IsRPGTrooper() ? RPGFirstAttackDelay : CombatPatterns ? CombatPatterns->AimSeconds : .8f;
        break;
    case EOBEnemyPatternAction::Fire:
        SetCombatState(EOBEnemyCombatState::Attacking);
        Duration=IsRPGTrooper() && CombatPatterns ? CombatPatterns->RPGReloadStartSeconds : .2f;
        break;
    case EOBEnemyPatternAction::Reload:
        SetCombatState(EOBEnemyCombatState::Reloading);
        if (IsRPGTrooper() && RPGWeaponMesh && RPGWeaponShotReloadAnimation)
        {
            RPGWeaponMesh->PlayAnimation(RPGWeaponShotReloadAnimation,false);
            RPGWeaponMesh->SetPosition(CombatPatterns ? CombatPatterns->RPGReloadStartSeconds : .35f,false);
        }
        if (auto* Sequence=GetPatternActionSequence())
            Duration=Sequence->GetPlayLength()-(IsRPGTrooper() && CombatPatterns ? CombatPatterns->RPGReloadStartSeconds : 0.f);
        else Duration=2.f;
        break;
    case EOBEnemyPatternAction::CheckAlive: break;
    }
    if (auto* Anim=Cast<UOBEnemyAnimInstance>(GetMesh()->GetAnimInstance())) Anim->BeginPatternAnimation();
    return FMath::Max(.01f,Duration);
}

bool AOBEnemyCharacter::FirePatternRound()
{
    if (!bPatternCombatActive || !CanRunCombatPattern(false)) return false;
    BeginPatternAction(EOBEnemyPatternAction::Fire,CombatPosture);
    if (IsRPGTrooper() && RPGWeaponMesh && RPGWeaponShotReloadAnimation)
        RPGWeaponMesh->PlayAnimation(RPGWeaponShotReloadAnimation,false);
    const int32 Before=PatternShotsFired;
    bPatternShotAuthorized=true;FireBullet();bPatternShotAuthorized=false;
    return PatternShotsFired==Before+1;
}

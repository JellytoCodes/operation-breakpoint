#include "Character/OBActionComponent.h"

#include "AI/OBAIController.h"
#include "Character/OBEnemyCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Stage/OBStageFlowComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogOBAction, Log, All);

UOBActionComponent::UOBActionComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UOBActionComponent::BeginPlay()
{
    Super::BeginPlay();

    PlayerSplineComponent = GetOwner()->FindComponentByClass<UOBPlayerSplineComponent>();
    if (!IsValid(PlayerSplineComponent))
    {
        UE_LOG(LogOBAction, Error, TEXT("ActionComponent requires PlayerSplineComponent | Owner=%s"), *GetNameSafe(GetOwner()));
        return;
    }
    PlayerSplineComponent->OnActionPointReached.AddUObject(this, &ThisClass::HandleActionPointReached);
}

void UOBActionComponent::HandleActionPointReached(int32 PointIndex)
{
    ActionState = EOBActionState::Action;

    if (PointIndex != ActiveEncounterPointIndex)
    {
        UE_LOG(LogOBAction, Display, TEXT("Action Started | Point=%d | Encounter=None"), PointIndex);
        return;
    }

    bPlayerAtActionPoint = true;
    UE_LOG(LogOBAction, Display,
        TEXT("Player Action Ready | Point=%d | EnemiesReady=%d/%d"),
        PointIndex, StagedEnemies.Num(), EncounterEnemies.Num());
    TryStartReadyEnemyStateTrees();
}

void UOBActionComponent::BeginEncounter(int32 PointIndex)
{
    ClearEncounterBindings();
    EncounterEnemies.Reset();
    StagedEnemies.Reset();
    StartedEnemies.Reset();
    ActiveEncounterPointIndex = PointIndex;
    TargetKillCount = 0;
    KilledCount = 0;
    bPlayerAtActionPoint = false;
    bEncounterSetupComplete = false;
    ActionState = EOBActionState::Prepare;
}

void UOBActionComponent::FinalizeEncounterSetup()
{
    if (bEncounterSetupComplete) return;
    TargetKillCount = EncounterEnemies.Num();
    bEncounterSetupComplete = true;
    AOBEnemyCharacter::PlayEncounterEntryVoices(EncounterEnemies);
    UE_LOG(LogOBAction, Display,
        TEXT("Encounter Started | Point=%d | Target=%d | EnemiesReady=%d"),
        ActiveEncounterPointIndex, TargetKillCount, StagedEnemies.Num());
    TryStartReadyEnemyStateTrees();
}

void UOBActionComponent::RegisterEnemy(AOBEnemyCharacter* Enemy)
{
    if (!IsValid(Enemy) || !Enemy->IsEncounterCombatant()) return;

    EncounterEnemies.AddUnique(Enemy);
    Enemy->OnKilled.RemoveAll(this);
    Enemy->OnKilled.AddUObject(this, &ThisClass::HandleEnemyKilled);

    AOBAIController* AIController = Cast<AOBAIController>(Enemy->GetController());
    if (!AIController)
    {
        UE_LOG(LogOBAction, Warning, TEXT("Enemy registration has no OB AIController | Enemy=%s"), *GetNameSafe(Enemy));
        return;
    }

    AIController->OnStagingMoveFinished.RemoveAll(this);
    AIController->OnStagingMoveFinished.AddUObject(this, &ThisClass::HandleStagingMoveFinished);
    if (AIController->HasReachedStagingPoint()) StagedEnemies.Add(Enemy);
    TryStartEnemyStateTree(Enemy);
}

void UOBActionComponent::HandleStagingMoveFinished(AOBAIController* AIController, bool bSucceeded)
{
    AOBEnemyCharacter* Enemy = AIController ? Cast<AOBEnemyCharacter>(AIController->GetPawn()) : nullptr;
    if (!IsValid(Enemy)) return;
    if (!EncounterEnemies.Contains(Enemy)) return;

    if (bSucceeded)
    {
        StagedEnemies.Add(Enemy);
        UE_LOG(LogOBAction, Display,
            TEXT("Enemy Staging Ready | Enemy=%s | Ready=%d/%d"),
            *GetNameSafe(Enemy), StagedEnemies.Num(), EncounterEnemies.Num());
        TryStartEnemyStateTree(Enemy);
        return;
    }

    StagedEnemies.Remove(Enemy);
    UE_LOG(LogOBAction, Warning,
        TEXT("Enemy Staging Failed | Enemy=%s | Ready=%d/%d"),
        *GetNameSafe(Enemy), StagedEnemies.Num(), EncounterEnemies.Num());
}

void UOBActionComponent::TryStartReadyEnemyStateTrees()
{
    if (!bEncounterSetupComplete) return;
    if (!bPlayerAtActionPoint) return;

    for (AOBEnemyCharacter* Enemy : EncounterEnemies)
    {
        TryStartEnemyStateTree(Enemy);
    }
}

void UOBActionComponent::TryStartEnemyStateTree(AOBEnemyCharacter* Enemy)
{
    if (!bEncounterSetupComplete) return;
    if (!bPlayerAtActionPoint) return;
    if (!IsValid(Enemy) || !Enemy->IsEncounterCombatant()) return;
    if (!EncounterEnemies.Contains(Enemy)) return;
    if (!StagedEnemies.Contains(Enemy)) return;
    if (StartedEnemies.Contains(Enemy)) return;

    StartedEnemies.Add(Enemy);
    Enemy->StartEncounterBehavior();
    UE_LOG(LogOBAction, Display,
        TEXT("Enemy Encounter Behavior Requested | Enemy=%s | Point=%d"),
        *GetNameSafe(Enemy), ActiveEncounterPointIndex);
}

void UOBActionComponent::HandleEnemyKilled(AOBEnemyCharacter* Enemy)
{
    if (!EncounterEnemies.Contains(Enemy)) return;
    if (IsValid(Enemy))
    {
        Enemy->OnKilled.RemoveAll(this);

        if (AOBAIController* AIController = Cast<AOBAIController>(Enemy->GetController()))
        {
            AIController->OnStagingMoveFinished.RemoveAll(this);
        }
    }

    EncounterEnemies.Remove(Enemy);
    StagedEnemies.Remove(Enemy);
    StartedEnemies.Remove(Enemy);

    ++KilledCount;
    UE_LOG(LogOBAction, Display, TEXT("Enemy Killed | %d / %d"), KilledCount, TargetKillCount);
    if (KilledCount != TargetKillCount) return;

    UE_LOG(LogOBAction, Display, TEXT("Encounter Cleared"));
    if (auto* Stage=GetOwner()->FindComponentByClass<UOBStageFlowComponent>(); Stage && Stage->HandleEncounterCleared()) return;
    const auto* Stage=GetOwner()->FindComponentByClass<UOBStageFlowComponent>();
    if (bAutoAdvanceOnEnemyClear || (Stage && Stage->GetPhase()==EOBStagePhase::SupportingFire)) CompleteAction();
}

void UOBActionComponent::CompleteAction()
{
    if (PlayerSplineComponent && PlayerSplineComponent->IsCameraTuningForQA()) return;
    if (auto* Stage=GetOwner()->FindComponentByClass<UOBStageFlowComponent>(); Stage && Stage->HandleEncounterCleared()) return;
    if (ActionState != EOBActionState::Action) return;
    if (auto* Stage=GetOwner()->FindComponentByClass<UOBStageFlowComponent>(); Stage && Stage->DeferTravelUntilNarrationDone()) return;
    ClearEncounterBindings();
    EncounterEnemies.Reset();
    StagedEnemies.Reset();
    StartedEnemies.Reset();
    ActiveEncounterPointIndex = INDEX_NONE;
    TargetKillCount = 0;
    KilledCount = 0;
    bPlayerAtActionPoint = false;
    bEncounterSetupComplete = false;
    ActionState = EOBActionState::Prepare;
    if (IsValid(PlayerSplineComponent)) PlayerSplineComponent->StartMoveToNextPoint();
}

void UOBActionComponent::ClearEncounterBindings()
{
    for (AOBEnemyCharacter* Enemy : EncounterEnemies)
    {
        if (!IsValid(Enemy)) continue;

        Enemy->OnKilled.RemoveAll(this);
        if (AOBAIController* AIController = Cast<AOBAIController>(Enemy->GetController()))
        {
            AIController->OnStagingMoveFinished.RemoveAll(this);
        }
    }
}

void UOBActionComponent::ResetEncounterForQA()
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    ClearEncounterBindings();EncounterEnemies.Reset();StagedEnemies.Reset();StartedEnemies.Reset();
    ActiveEncounterPointIndex=INDEX_NONE;TargetKillCount=KilledCount=0;
    bPlayerAtActionPoint=bEncounterSetupComplete=false;ActionState=EOBActionState::Prepare;
#endif
}

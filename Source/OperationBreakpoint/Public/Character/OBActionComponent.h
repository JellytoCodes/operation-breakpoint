#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "OBActionComponent.generated.h"

class AOBEnemyCharacter;
class AOBAIController;
class UOBPlayerSplineComponent;

UENUM(BlueprintType)
enum class EOBActionState : uint8
{
    Prepare,
    Action
};

UCLASS(ClassGroup = (OB), meta = (BlueprintSpawnableComponent))
class OPERATIONBREAKPOINT_API UOBActionComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UOBActionComponent();

    void BeginEncounter(int32 PointIndex);
    void RegisterEnemy(AOBEnemyCharacter* Enemy);
    void FinalizeEncounterSetup();

    UFUNCTION(BlueprintCallable, Category = "OB|Action")
    void CompleteAction();
    void ResetEncounterForQA();

    void SetExternalActionState(bool bAction) { ActionState = bAction ? EOBActionState::Action : EOBActionState::Prepare; }

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OB|Action")
    bool bAutoAdvanceOnEnemyClear = true;

    UFUNCTION(BlueprintPure, Category = "OB|Action")
    EOBActionState GetActionState() const { return ActionState; }
    bool HasEncounterEnemies() const { return !EncounterEnemies.IsEmpty(); }
    const TArray<TObjectPtr<AOBEnemyCharacter>>& GetEncounterEnemies() const { return EncounterEnemies; }

protected:
    virtual void BeginPlay() override;

private:
    friend class FOBTrainingFlowTest;
    friend class FOBAllyRedeployTest;
    friend class FOBNoncombatSpawnTest;
    friend class FOBStageEncounterMapSmoke;
    friend class FOBTrainingSplinePrototypeTest;
    void HandleActionPointReached(int32 PointIndex);
    void HandleStagingMoveFinished(AOBAIController* AIController, bool bSucceeded);
    void HandleEnemyKilled(AOBEnemyCharacter* Enemy);
    void TryStartReadyEnemyStateTrees();
    void TryStartEnemyStateTree(AOBEnemyCharacter* Enemy);
    void ClearEncounterBindings();

    UPROPERTY(Transient)
    TObjectPtr<UOBPlayerSplineComponent> PlayerSplineComponent;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "OB|Action", meta = (AllowPrivateAccess = "true"))
    EOBActionState ActionState = EOBActionState::Prepare;

    int32 ActiveEncounterPointIndex = INDEX_NONE;
    int32 TargetKillCount = 0;
    int32 KilledCount = 0;

    bool bPlayerAtActionPoint = false;
    bool bEncounterSetupComplete = false;
    UPROPERTY(Transient)
    TArray<TObjectPtr<AOBEnemyCharacter>> EncounterEnemies;
    UPROPERTY(Transient)
    TSet<TObjectPtr<AOBEnemyCharacter>> StagedEnemies;
    UPROPERTY(Transient)
    TSet<TObjectPtr<AOBEnemyCharacter>> StartedEnemies;
};

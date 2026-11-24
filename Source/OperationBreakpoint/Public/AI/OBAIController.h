#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "OBAIController.generated.h"

class UStateTreeAIComponent;
class UStateTree;
struct FPathFollowingResult;

class AOBAIController;
DECLARE_MULTICAST_DELEGATE_TwoParams(FOBOnStagingMoveFinished, AOBAIController*, bool);

UCLASS()
class OPERATIONBREAKPOINT_API AOBAIController : public AAIController
{
    GENERATED_BODY()

public:
    AOBAIController(const FObjectInitializer& ObjectInitializer=FObjectInitializer::Get());

    void StartStateTree();
    void StopStateTree();
    bool SetEncounterStateTree(UStateTree* Tree,bool bExplicitOverride=true);
    bool MoveToStagingPoint(const FTransform& InStagingTransform);
    bool PrepareEncounterEntry(const FTransform& InStagingTransform, const FTransform& InCoverTransform, float StagingMoveDelay=0.f);
    void StartEncounterBehavior();
    bool HasReachedStagingPoint() const { return bHasReachedStagingPoint; }
    const UStateTree* GetActiveCombatTree() const { return ActiveCombatTree; }
    bool IsCombatTreeRunning() const;
    bool BeginZone6GuardScene(AActor* Hostage,UStateTree* Tree,bool bFarGuard);
    bool IsZone6GuardScene() const { return Zone6Hostage.IsValid(); }
    bool IsZone6FarGuard() const { return bZone6FarGuard; }
    void ReactToZone6Entry() { bZone6Entry=true; }
    AActor* GetZone6FacingTarget() const;
    void UpdateZone6Facing(float DeltaSeconds,bool bImmediately=false);

    FOBOnStagingMoveFinished OnStagingMoveFinished;

protected:
    virtual void OnPossess(APawn* InPawn) override;
    virtual void OnUnPossess() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result) override;

private:
    friend class FOBEncounterSpawnerEntryTest;
    void BeginDelayedStagingMove();
    void FinishStagingMove(bool bSucceeded);
    void FacePlayer(float DeltaSeconds, bool bImmediately=false);
    void BeginEncounterReaction();
    void BeginCoverHold();
    void FinishEncounterReaction();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OB|AI", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UStateTreeAIComponent> StateTreeComponent;

    UPROPERTY(EditDefaultsOnly, Category = "OB|AI|Staging", meta = (ClampMin = "0.0"))
    float StagingAcceptanceRadius = 50.0f;

    FAIRequestID StagingMoveRequestId;
    FTransform StagingTargetTransform = FTransform::Identity;
    bool bMovingToStagingPoint = false;
    bool bHasReachedStagingPoint = false;
    FTransform CoverTargetTransform = FTransform::Identity;
    FAIRequestID CoverMoveRequestId;
    FTimerHandle ReactionTimer;
    FTimerHandle StagingDelayTimer;
    bool bEncounterActivated = false;
    bool bMovingToCover = false;
    UPROPERTY(Transient) TObjectPtr<UStateTree> ActiveCombatTree;
    UPROPERTY(Transient) TObjectPtr<UStateTree> EncounterTreeOverride;
    TWeakObjectPtr<AActor> Zone6Hostage;
    bool bZone6FarGuard=false,bZone6Entry=false;
};

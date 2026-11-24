#pragma once

#include "CoreMinimal.h"
#include "AI/OBEnemyCombatPattern.h"
#include "Tasks/StateTreeAITask.h"
#include "OBStateTreeEnemyPatternTask.generated.h"

USTRUCT()
struct OPERATIONBREAKPOINT_API FOBStateTreeEnemyPatternTaskData
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,Category=Context) TObjectPtr<AActor> Actor;
    UPROPERTY(EditAnywhere,Category=Parameter) EOBEnemyPatternAction Action=EOBEnemyPatternAction::Aim;
    UPROPERTY(EditAnywhere,Category=Parameter) EOBEnemyPosture Posture=EOBEnemyPosture::Stand;
    UPROPERTY(EditAnywhere,Category=Parameter,meta=(ClampMin="1",ClampMax="32")) int32 ShotCount=5;
    // Negative uses the configured animation / aim duration.
    UPROPERTY(EditAnywhere,Category=Parameter) float Duration=-1.f;
    UPROPERTY(EditAnywhere,Category=Parameter) bool bWindowCoverHold=false;
    UPROPERTY(Transient) float Elapsed=0;
    UPROPERTY(Transient) float StepDuration=0;
    UPROPERTY(Transient) float NextShotAt=0;
    UPROPERTY(Transient) float ShotInterval=0.f;
    UPROPERTY(Transient) float ReturnToAimAt=0.f;
    UPROPERTY(Transient) int32 ShotsIssued=0;
};

USTRUCT(meta=(DisplayName="OB Enemy Pattern Step",Category="OB|AI"))
struct OPERATIONBREAKPOINT_API FOBStateTreeEnemyPatternTask : public FStateTreeAITaskBase
{
    GENERATED_BODY()
    using FInstanceDataType=FOBStateTreeEnemyPatternTaskData;
    virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
    virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,const FStateTreeTransitionResult& Transition) const override;
    virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context,float DeltaTime) const override;
};

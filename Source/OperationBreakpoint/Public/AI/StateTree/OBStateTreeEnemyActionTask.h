#pragma once

#include "CoreMinimal.h"
#include "Character/OBEnemyCharacter.h"
#include "Tasks/StateTreeAITask.h"
#include "OBStateTreeEnemyActionTask.generated.h"

class AActor;

USTRUCT()
struct OPERATIONBREAKPOINT_API FOBStateTreeEnemyActionTaskInstanceData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = Context)
    TObjectPtr<AActor> Actor;

    UPROPERTY(EditAnywhere, Category = Parameter)
    EOBEnemyCombatState CombatState = EOBEnemyCombatState::Attacking;

    UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
    float Duration = 2.0f;

    UPROPERTY(Transient)
    float RemainingTime = 0.0f;
};

USTRUCT(meta = (DisplayName = "OB Enemy Action", Category = "OB|AI"))
struct OPERATIONBREAKPOINT_API FOBStateTreeEnemyActionTask : public FStateTreeAITaskBase
{
    GENERATED_BODY()

    using FInstanceDataType = FOBStateTreeEnemyActionTaskInstanceData;

    virtual const UStruct* GetInstanceDataType() const override
    {
        return FInstanceDataType::StaticStruct();
    }

    virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
    virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;
};
#include "AI/StateTree/OBStateTreeEnemyActionTask.h"

#include "StateTreeExecutionContext.h"

EStateTreeRunStatus FOBStateTreeEnemyActionTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
    FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

    AOBEnemyCharacter* Enemy = Cast<AOBEnemyCharacter>(InstanceData.Actor);

    if (!IsValid(Enemy)) return EStateTreeRunStatus::Failed;

    Enemy->SetCombatState(InstanceData.CombatState);
    InstanceData.RemainingTime = FMath::Max(InstanceData.Duration, 0.0f);

    return InstanceData.RemainingTime > 0.0f ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Succeeded;
}

EStateTreeRunStatus FOBStateTreeEnemyActionTask::Tick(FStateTreeExecutionContext& Context, float DeltaTime) const
{
    FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

    if (!IsValid(InstanceData.Actor)) return EStateTreeRunStatus::Failed;

    InstanceData.RemainingTime -= DeltaTime;

    return InstanceData.RemainingTime <= 0.0f ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Running;
}
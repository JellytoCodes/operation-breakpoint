#include "AI/StateTree/OBStateTreeZone6GuardTask.h"
#include "AI/OBAIController.h"
#include "Character/OBEnemyCharacter.h"
#include "StateTreeExecutionContext.h"

EStateTreeRunStatus FOBStateTreeZone6GuardTask::EnterState(FStateTreeExecutionContext& Context,const FStateTreeTransitionResult&) const
{
    return Tick(Context,0);
}
EStateTreeRunStatus FOBStateTreeZone6GuardTask::Tick(FStateTreeExecutionContext& Context,float DeltaTime) const
{
    auto* Enemy=Cast<AOBEnemyCharacter>(Context.GetInstanceData(*this).Actor);
    auto* AI=Enemy ? Cast<AOBAIController>(Enemy->GetController()) : nullptr;
    if (!AI || !AI->IsZone6GuardScene() || Enemy->GetCombatState()==EOBEnemyCombatState::Dead) return EStateTreeRunStatus::Failed;
    AI->UpdateZone6Facing(DeltaTime);
    return EStateTreeRunStatus::Running;
}

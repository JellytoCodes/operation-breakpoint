#include "AI/StateTree/OBStateTreeEnemyPatternTask.h"
#include "Character/OBEnemyCharacter.h"
#include "Animation/AnimSequence.h"
#include "StateTreeExecutionContext.h"

EStateTreeRunStatus FOBStateTreeEnemyPatternTask::EnterState(FStateTreeExecutionContext& Context,const FStateTreeTransitionResult&) const
{
    auto& Data=Context.GetInstanceData(*this);
    auto* Enemy=Cast<AOBEnemyCharacter>(Data.Actor);
    if (!Enemy || !Enemy->IsCombatPatternActive() || !Enemy->CanRunCombatPattern())
    {
        if (Enemy) Enemy->StopCombatPattern();
        return EStateTreeRunStatus::Failed;
    }
    Data.Elapsed=Data.NextShotAt=0;Data.ShotsIssued=0;
    // FirePatternRound starts each shot animation together with its one projectile.
    // Do not start a separate, unpaired fire animation on task entry.
    const float AnimationDuration=Data.Action==EOBEnemyPatternAction::Fire ? 0.f : Enemy->BeginPatternAction(Data.Action,Data.Posture);
    Data.StepDuration=Data.Duration>=0.f ? Data.Duration : AnimationDuration;
    if (Data.bWindowCoverHold && Enemy->CombatPatterns) Data.StepDuration+=Enemy->CombatPatterns->WindowCoverSeconds;
    if (Enemy->CombatPatterns && Data.Action!=EOBEnemyPatternAction::Fire && Data.Action!=EOBEnemyPatternAction::CheckAlive)
        Data.StepDuration+=Enemy->CombatPatterns->SampleActionDelay();
    Data.ShotInterval=Enemy->IsRPGTrooper()
        ? Enemy->CombatPatterns ? FMath::Max(.05f,Enemy->CombatPatterns->RPGReloadStartSeconds) : 1.f
        : Enemy->CombatPatterns ? FMath::Max(0.f,Enemy->CombatPatterns->RifleShotInterval) : 0.f;
    Data.ReturnToAimAt=0.f;
    if (Data.Action==EOBEnemyPatternAction::CheckAlive) return EStateTreeRunStatus::Succeeded;
    return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FOBStateTreeEnemyPatternTask::Tick(FStateTreeExecutionContext& Context,float DeltaTime) const
{
    auto& Data=Context.GetInstanceData(*this);
    auto* Enemy=Cast<AOBEnemyCharacter>(Data.Actor);
    if (!Enemy || !Enemy->IsCombatPatternActive() || !Enemy->CanRunCombatPattern())
    {
        if (Enemy) Enemy->StopCombatPattern();
        return EStateTreeRunStatus::Failed;
    }
    // Arrival dispatch can reach ActionComponent before StageFlow on the same frame.
    // Keep the warning armed, but never fire a projectile while the stage still says Travel.
    if (Data.Action==EOBEnemyPatternAction::Fire && !Enemy->CanRunCombatPattern(false)) return EStateTreeRunStatus::Running;
    Data.Elapsed+=FMath::Max(0.f,DeltaTime);
    if (Data.Action!=EOBEnemyPatternAction::Fire)
        return Data.Elapsed>=Data.StepDuration ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Running;
    if (Data.ShotsIssued<FMath::Clamp(Data.ShotCount,1,32) && Data.Elapsed>=Data.NextShotAt)
    {
        if (!Enemy->FirePatternRound()) { Enemy->StopCombatPattern();return EStateTreeRunStatus::Failed; }
        ++Data.ShotsIssued;
        const auto* ShotAnimation=Enemy->GetPatternActionSequence();
        Data.ReturnToAimAt=Data.Elapsed+(ShotAnimation ? ShotAnimation->GetPlayLength() : .2f);
        // Every rifle round owns a complete animation playback. A zero configured interval
        // means replay immediately after that playback, not once per frame.
        Data.NextShotAt=Enemy->IsRPGTrooper() ? Data.Elapsed+Data.ShotInterval
            : FMath::Max(Data.Elapsed+Data.ShotInterval,Data.ReturnToAimAt);
        Data.StepDuration=Enemy->IsRPGTrooper() ? Data.Elapsed+Data.ShotInterval : Data.ReturnToAimAt;
    }
    return Data.ShotsIssued>=FMath::Clamp(Data.ShotCount,1,32) && Data.Elapsed>=Data.StepDuration
        ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Running;
}

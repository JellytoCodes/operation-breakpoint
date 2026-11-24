#include "Stage/OBStageFlowComponent.h"
#include "AI/OBAIController.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Character/OBEnemyCharacter.h"
#include "Character/OBHostageCharacter.h"
#include "World/OBEncounterTrigger.h"
#include "World/OBEncounterSpawner.h"
#include "Engine/World.h"
#include "EngineUtils.h"

float UOBStageFlowComponent::GetTravelMoveSpeed(float BaseSpeed) const
{
    const auto* Rail=Player ? Player->GetPlayerSplineComponent() : nullptr;
    return bActive && bUseZone6Breach && bZone6EntrySceneStarted && Phase==EOBStagePhase::Travel && Rail
        && Rail->GetCurrentPointIndex()>=Zone6BreachPoint && Rail->GetCurrentPointIndex()<Zone6EncounterPoint
        ? FMath::Min(BaseSpeed,FMath::Max(1.f,Zone6EntryMoveSpeed)) : BaseSpeed;
}
void UOBStageFlowComponent::PrepareZone6Scene()
{
    TArray<AOBEncounterSpawner*> Guards,Hostages;
    for (TActorIterator<AOBEncounterTrigger> It(GetWorld());It;++It) if (It->GetPointIndex()==Zone6EncounterPoint)
        for (AOBEncounterSpawner* Spawner:It->GetSpawners()) if (IsValid(Spawner))
        {
            Spawner->PrepareEncounter(Zone6EncounterPoint);
            if (!IsValid(Spawner->GetSpawnedEnemy())) continue;
            if (Spawner->bParticipatesInCombat) Guards.AddUnique(Spawner);
            else if (Spawner->GetSpawnedEnemy()->IsA<AOBHostageCharacter>()) Hostages.AddUnique(Spawner);
        }
    for (auto* Hostage:Hostages)
        if (Phase==EOBStagePhase::Zone6Dialogue || Phase==EOBStagePhase::Zone6Breach) Hostage->GetSpawnedEnemy()->SetCanBeDamaged(false);
    TMap<AOBEncounterSpawner*,AOBEncounterSpawner*> Targets,Farthest;
    for (auto* Guard:Guards)
    {
        auto* Hostage=Guard->GuardedHostageSpawner.Get();
        if (!Hostages.Contains(Hostage))
        {
            Hostage=nullptr;double Best=TNumericLimits<double>::Max();
            for (auto* Candidate:Hostages)
            {
                const double Distance=FVector::DistSquared2D(Guard->GetActorLocation(),Candidate->GetActorLocation());
                if (Distance<Best) { Best=Distance;Hostage=Candidate; }
            }
        }
        if (!Hostage) continue;
        Targets.Add(Guard,Hostage);
        auto** Existing=Farthest.Find(Hostage);
        if (!Existing || FVector::DistSquared2D(Guard->GetActorLocation(),Hostage->GetActorLocation())>
            FVector::DistSquared2D((*Existing)->GetActorLocation(),Hostage->GetActorLocation())) Farthest.Add(Hostage,Guard);
    }
    for (const auto& Pair:Targets)
    {
        auto* Guard=Pair.Key->GetSpawnedEnemy();auto* AI=Cast<AOBAIController>(Guard->GetController());
        const bool Far=Farthest.FindRef(Pair.Value)==Pair.Key;
        if (AI && AI->BeginZone6GuardScene(Pair.Value->GetSpawnedEnemy(),Pair.Key->CombatStateTree,Far))
            Zone6SceneGuards.AddUnique(AI);
        // The roster is visible through the doorway, but cannot be shot before activation.
        if (Phase==EOBStagePhase::Zone6Dialogue || Phase==EOBStagePhase::Zone6Breach) Guard->SetCanBeDamaged(false);
    }
}
void UOBStageFlowComponent::StartZone6EntryScene()
{
    PrepareZone6Scene();
    if (bZone6EntrySceneStarted) return;
    bZone6EntrySceneStarted=true;
    for (auto Weak:Zone6SceneGuards) if (auto* AI=Weak.Get())
    {
        AI->ReactToZone6Entry();
        if (auto* Enemy=Cast<AOBEnemyCharacter>(AI->GetPawn()))
        {
            Enemy->SetCanBeDamaged(true);
            for (AActor* Actor:{static_cast<AActor*>(Enemy),static_cast<AActor*>(AI)})
            {
                OriginalDilations.FindOrAdd(Actor,Actor->CustomTimeDilation);
                Actor->CustomTimeDilation=FMath::Clamp(EnemySlowMotionRate,.05f,1.f);
            }
        }
    }
}

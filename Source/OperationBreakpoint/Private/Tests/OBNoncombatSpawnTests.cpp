#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "AI/OBAIController.h"
#include "Character/OBEnemyCharacter.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBActionComponent.h"
#include "Character/OBPlayerSplineComponent.h"
#include "World/OBEncounterSpawner.h"
#include "World/OBEncounterTrigger.h"
#include "World/OBBulletActor.h"
#include "Components/StateTreeAIComponent.h"
#include "Navigation/PathFollowingComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBNoncombatSpawnTest,"OperationBreakpoint.Infiltration.NoncombatHostages",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOBNoncombatSpawnTest::RunTest(const FString&)
{
    auto* W=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
    auto* Class=LoadClass<AOBEnemyCharacter>(nullptr,TEXT("/Game/Blueprints/Enemy/BP_EnemyCharacter.BP_EnemyCharacter_C"));
    if (!TestNotNull(TEXT("Existing soldier Blueprint loads"),Class))
    { W->DestroyWorld(false); GEngine->DestroyWorldContext(W); return false; }
    auto* Player=W->SpawnActor<AOBPlayerCharacter>(); auto* Action=Player->GetActionComponent();
    Action->PlayerSplineComponent=Player->GetPlayerSplineComponent();
    auto* Trigger=W->SpawnActor<AOBEncounterTrigger>(); Trigger->PointIndex=85;
    for (int32 I=0; I<6; ++I)
    {
        auto* NPC=W->SpawnActor<AOBEnemyCharacter>(Class,FVector(I*500+3000,0,200),FRotator::ZeroRotator);
        auto* Spawner=W->SpawnActor<AOBEncounterSpawner>(); Spawner->PlacedEnemy=NPC;
        Spawner->bParticipatesInCombat=I>=2;
        // Noncombat role must also override accidental movement/attack settings.
        Spawner->bMoveToStagingPoint=I<2; Spawner->StagingMoveDelay=3;
        Spawner->bOverrideEnemyBehavior=true;
        Spawner->EnemyBehavior.bStartStateTreeAfterReaction=false;
        Trigger->Spawners.Add(Spawner);
    }
    Trigger->HandleBeginOverlap(nullptr,Player,nullptr,0,false,FHitResult());
    TestEqual(TEXT("Only four captors are counted by the real overlap handler"),Action->TargetKillCount,4);
    TestEqual(TEXT("Only four captors enter the combat registry"),Action->EncounterEnemies.Num(),4);
    auto CountBullets=[&]() { int32 Count=0; for (TActorIterator<AOBBulletActor> It(W); It; ++It) ++Count; return Count; };
    const int32 BeforeBullets=CountBullets();
    for (int32 I=0; I<2; ++I)
    {
        auto* Spawner=Trigger->Spawners[I].Get(); auto* NPC=Spawner->GetSpawnedEnemy();
        TestTrue(TEXT("Hostage is spawned and prepared once"),Spawner->IsPrepared() && IsValid(NPC));
        TestFalse(TEXT("Hostage carries its noncombat role"),NPC->IsEncounterCombatant());
        TestFalse(TEXT("Hostage is not bound to enemy death counting"),NPC->OnKilled.IsBoundToObject(Action));
        Action->RegisterEnemy(NPC); // Other registration paths must also reject this pawn.
        NPC->SetCombatState(EOBEnemyCombatState::Attacking); NPC->StartStateTree(); NPC->StartEncounterBehavior(); NPC->FireBullet();
        if (auto* AI=Cast<AOBAIController>(NPC->GetController()))
        {
            AI->StartStateTree(); AI->StartEncounterBehavior();
            TestFalse(TEXT("Direct controller entry cannot move a hostage"),AI->PrepareEncounterEntry(FTransform(FVector(5000,0,0)),FTransform::Identity,3));
            TestFalse(TEXT("Direct controller staging cannot move a hostage"),AI->MoveToStagingPoint(FTransform(FVector(5000,0,0))));
            TestTrue(TEXT("Hostage controller has no movement request"),AI->GetMoveStatus()==EPathFollowingStatus::Idle);
            TestFalse(TEXT("Hostage StateTree stays stopped"),AI->FindComponentByClass<UStateTreeAIComponent>()->IsRunning());
        }
        TestTrue(TEXT("Hostage stays idle despite attack requests"),NPC->GetCombatState()==EOBEnemyCombatState::Idle);
        NPC->OnKilled.Broadcast(NPC);
    }
    TestEqual(TEXT("Hostage fire requests produce no projectiles"),CountBullets(),BeforeBullets);
    TestEqual(TEXT("Hostage events do not alter the combat target count"),Action->TargetKillCount,4);
    TestEqual(TEXT("Hostage events do not count as enemy kills"),Action->KilledCount,0);
    Action->SetExternalActionState(true);
    for (int32 I=2; I<6; ++I)
    {
        // Exercise the death subscription here; real damage is covered by the saved-map PIE smoke.
        auto* Captor=Trigger->Spawners[I]->GetSpawnedEnemy();
        Captor->OnKilled.Broadcast(Captor);
        if (I<5) TestTrue(TEXT("Encounter remains active until the last captor dies"),Action->GetActionState()==EOBActionState::Action);
    }
    TestTrue(TEXT("Four captor deaths complete combat while hostages are still alive"),Action->GetActionState()==EOBActionState::Prepare && !Action->HasEncounterEnemies());
    for (int32 I=0; I<2; ++I)
        TestTrue(TEXT("Hostage survival does not block completion"),Trigger->Spawners[I]->GetSpawnedEnemy()->GetCurrentHealth()>0);
    W->DestroyWorld(false); GEngine->DestroyWorldContext(W);
    return !HasAnyErrors();
}
#endif

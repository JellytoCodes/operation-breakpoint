#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "AI/OBAIController.h"
#include "Character/OBEnemyCharacter.h"
#include "World/OBEncounterSpawner.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBEncounterSpawnerEntryTest,"OperationBreakpoint.Infiltration.SpawnerEntryDelay",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOBEncounterSpawnerEntryTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* EnemyClass=LoadClass<AOBEnemyCharacter>(nullptr,TEXT("/Game/Blueprints/Enemy/BP_EnemyCharacter.BP_EnemyCharacter_C"));
    if (!TestNotNull(TEXT("Configured enemy class loads"),EnemyClass))
    { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); return false; }
    auto* Enemy=World->SpawnActor<AOBEnemyCharacter>(EnemyClass);
    auto* AI=World->SpawnActor<AOBAIController>(); AI->Possess(Enemy);
    auto* Spawner=World->SpawnActor<AOBEncounterSpawner>(); Spawner->PlacedEnemy=Enemy;
    TestTrue(TEXT("Movement is enabled by default"),Spawner->bMoveToStagingPoint);
    TestEqual(TEXT("Default staging delay is three seconds"),Spawner->StagingMoveDelay,3.f);
    Spawner->PrepareEncounter(7);
    TestTrue(TEXT("Moving enemy is spawned and registered while the delay is pending"),Spawner->IsPrepared() && Spawner->GetSpawnedEnemy()==Enemy);
    TestTrue(TEXT("Moving spawner overrides the legacy entry mode"),Enemy->EncounterBehavior.EntryMode==EOBEnemyEntryMode::RunToStaging);
    TestFalse(TEXT("Delayed runner is not ready for combat"),AI->HasReachedStagingPoint());
    const float EntryDelay=World->GetTimerManager().GetTimerRemaining(AI->StagingDelayTimer);
    TestTrue(TEXT("Production entry delay adds per-enemy variation to the authored three seconds"),EntryDelay>=3.05f-.001f && EntryDelay<=3.9f+.001f);
    const FVector Before=Enemy->GetActorLocation();
    auto Advance=[&](float Seconds) {
        for (float Elapsed=0; Elapsed<Seconds; Elapsed+=.05f) { ++GFrameCounter; World->GetTimerManager().Tick(.05f); }
    };
    Advance(2.5f);
    TestTrue(TEXT("Runner holds its spawn location before the delay expires"),Enemy->GetActorLocation().Equals(Before));
    TestFalse(TEXT("Runner cannot enter combat during the delay"),AI->HasReachedStagingPoint());
    int32 Results=0;
    AI->OnStagingMoveFinished.AddLambda([&](AOBAIController*,bool) { ++Results; });
    // A goal already reached by this isolated fixture may finish immediately, but only after expiry.
    Advance(EntryDelay-2.5f+.1f);
    TestEqual(TEXT("Expiry requests the staging move exactly once"),Results,1);
    TestFalse(TEXT("Expired delay timer is released"),World->GetTimerManager().IsTimerActive(AI->StagingDelayTimer));
    AI->OnStagingMoveFinished.Clear();
    Enemy->EncounterBehavior.EntryMode=EOBEnemyEntryMode::RunToStaging;
    AI->PrepareEncounterEntry(FTransform(FVector(1000,0,0)),FTransform::Identity,50.f);
    TestTrue(TEXT("Runtime delay is also clamped at ten seconds"),FMath::IsNearlyEqual(World->GetTimerManager().GetTimerRemaining(AI->StagingDelayTimer),10.f,.01f));
    AI->StopStateTree();
    TestFalse(TEXT("Death/stop cancels the delayed move"),World->GetTimerManager().IsTimerActive(AI->StagingDelayTimer));
    AI->PrepareEncounterEntry(FTransform(FVector(1000,0,0)),FTransform::Identity,3.f);
    AI->UnPossess();
    TestFalse(TEXT("Unpossess cancels the delayed move"),World->GetTimerManager().IsTimerActive(AI->StagingDelayTimer));
    AI->Possess(Enemy);
    auto* Guard=World->SpawnActor<AOBEncounterSpawner>(); Guard->PlacedEnemy=Enemy; Guard->bMoveToStagingPoint=false;
    Guard->StagingMoveDelay=10.f; Guard->PrepareEncounter(19);
    TestTrue(TEXT("Stationary spawner overrides the legacy run entry"),Enemy->EncounterBehavior.EntryMode==EOBEnemyEntryMode::WaitAtSpawn);
    TestTrue(TEXT("Stationary guards become ready immediately"),AI->HasReachedStagingPoint());
    TestFalse(TEXT("Stationary guards never arm a movement delay"),World->GetTimerManager().IsTimerActive(AI->StagingDelayTimer));
    TestTrue(TEXT("Stationary spawn does not change the pawn location"),Enemy->GetActorLocation().Equals(Before));
    Guard->PrepareEncounter(44);
    TestEqual(TEXT("Repeated trigger preparation does not restart the spawner"),Guard->GetPreparedPointIndex(),19);
    World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    return !HasAnyErrors();
}
#endif

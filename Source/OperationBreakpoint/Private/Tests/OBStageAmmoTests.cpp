#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Core/OBPlayerController.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBActionComponent.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Stage/OBStageFlowComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBStageAmmoTest,"OperationBreakpoint.Stage.Ammunition",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOBStageAmmoTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false);GEngine->DestroyWorldContext(World); };
    if (!World->GetPhysicsScene()) World->CreatePhysicsScene();
    auto* Pawn=World->SpawnActor<AOBPlayerCharacter>();
    auto* PC=World->SpawnActor<AOBPlayerController>();PC->Possess(Pawn);
    PC->FireSound.Reset();PC->EmptyFireSound.Reset();PC->ReloadSound.Reset();
    auto* Stage=Pawn->GetStageFlowComponent();Stage->Player=Pawn;Stage->bActive=true;
    Stage->Phase=EOBStagePhase::Combat;Stage->CurrentZone=1;
    auto* Action=Pawn->GetActionComponent();auto* Spline=Pawn->GetPlayerSplineComponent();
    Action->SetExternalActionState(true);
    TestEqual(TEXT("A fresh gun starts with twenty rounds"),PC->GetCurrentAmmo(),20);
    TestEqual(TEXT("A standard magazine holds twenty rounds"),PC->GetMagazineCapacity(),20);
    int32 Shots=0;PC->OnShotResolved.AddLambda([&](const FHitResult&){++Shots;});
    const FVector Start(0,0,10000),End(1000,0,10000);
    for (int32 I=0;I<17;++I) PC->FireAlongRay(Start,End);
    TestEqual(TEXT("Seventeen actual shots consume seventeen rounds"),Shots,17);
    TestEqual(TEXT("Zone1 retains three rounds"),PC->GetCurrentAmmo(),3);
    Spline->RailState=UOBPlayerSplineComponent::EOBRailState::Moving;
    Action->SetExternalActionState(false);Stage->HandleTravel();
    PC->FireAlongRay(Start,End);
    TestEqual(TEXT("Travel never fires or consumes a round"),Shots,17);
    TestEqual(TEXT("Leaving Zone1 does not refill ammunition"),PC->GetCurrentAmmo(),3);
    Stage->CurrentZone=2;Stage->Phase=EOBStagePhase::Combat;
    Spline->RailState=UOBPlayerSplineComponent::EOBRailState::Action;
    Action->SetExternalActionState(true);Stage->RefreshHUD();
    TestEqual(TEXT("Zone2 starts at 3/20 without a moving reload"),PC->GetCurrentAmmo(),3);
    for (int32 I=0;I<8;++I) PC->FireAlongRay(Start,End);
    TestEqual(TEXT("Only twenty live shots resolve before reloading"),Shots,20);
    TestEqual(TEXT("Repeated empty pulls cannot produce negative ammo"),PC->GetCurrentAmmo(),0);
    TestFalse(TEXT("The empty gun rejects firing outside tutorial controls"),PC->CheckWeaponFireAllowed());
    Spline->RailState=UOBPlayerSplineComponent::EOBRailState::Moving;
    Action->SetExternalActionState(false);Stage->HandleTravel();
    PC->CompleteReload();
    TestEqual(TEXT("Moving reload fills an empty magazine to twenty"),PC->GetCurrentAmmo(),20);
    for (int32 I=0;I<3;++I) PC->CompleteReload();
    TestEqual(TEXT("Repeated reloads cannot accumulate over twenty"),PC->GetCurrentAmmo(),20);
    PC->SetTrainingAmmo(3,20);PC->CompleteReload();
    TestEqual(TEXT("Reloading three remaining rounds replaces them with twenty, not twenty-three"),PC->GetCurrentAmmo(),20);
    PC->SetTrainingAmmo(99,99);
    TestEqual(TEXT("Stage ammo assignments cannot bypass the twenty-round cap"),PC->GetCurrentAmmo(),20);
    TestEqual(TEXT("Stage magazine assignments cannot increase the capacity"),PC->GetMagazineCapacity(),20);
    for (const auto Phase:{EOBStagePhase::Opening,EOBStagePhase::AwaitingDeparture,EOBStagePhase::Zone6Breach,EOBStagePhase::Ending,EOBStagePhase::Complete})
    {
        Stage->Phase=Phase;PC->SetTrainingAmmo(3,20);PC->CompleteReload();
        TestEqual(TEXT("Cinematic and result input cannot refill the gun"),PC->GetCurrentAmmo(),3);
    }
    Stage->bActive=false;Stage->Phase=EOBStagePhase::Travel;
    Spline->RailState=UOBPlayerSplineComponent::EOBRailState::Action;
    Action->SetExternalActionState(true);
    PC->SetTrainingAmmo(20,20);
    for (int32 I=0;I<21;++I) PC->FireAlongRay(Start,End);
    TestEqual(TEXT("Normal shooting outside Stage also stops at twenty shots"),Shots,40);
    TestEqual(TEXT("Normal shooting has no unlimited-ammo bypass"),PC->GetCurrentAmmo(),0);
    PC->SetTrainingControls(true,true,true,false);PC->SetTrainingAmmo(3,20,3);
    PC->FireAlongRay(Start,End);PC->CompleteReload();
    TestEqual(TEXT("The explicit three-round zeroing exercise remains intact"),PC->GetCurrentAmmo(),3);
    TestEqual(TEXT("Zeroing retains the twenty-round maximum with a three-round reload"),PC->GetMagazineCapacity(),20);
    return !HasAnyErrors();
}
#endif

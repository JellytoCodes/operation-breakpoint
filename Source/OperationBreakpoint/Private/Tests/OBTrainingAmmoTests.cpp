#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Core/OBPlayerController.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBActionComponent.h"
#include "Training/OBTrainingDirector.h"
#include "Training/OBTrainingTarget.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBTrainingAmmoTest,"OperationBreakpoint.Training.Ammunition",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOBTrainingAmmoTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false);GEngine->DestroyWorldContext(World); };
    if (!World->GetPhysicsScene()) World->CreatePhysicsScene();
    auto* Pawn=World->SpawnActor<AOBPlayerCharacter>();
    auto* PC=World->SpawnActor<AOBPlayerController>();PC->Possess(Pawn);
    PC->FireSound.Reset();PC->EmptyFireSound.Reset();PC->ReloadSound.Reset();
    auto* Director=World->SpawnActor<AOBTrainingDirector>();
    Director->Controller=PC;Director->Action=Pawn->GetActionComponent();
    Director->bUseSplinePrototype=true;Director->bUsePopUpTargets=false;
    Director->PrototypeRouteIndex=1;
    Director->AccuracyTarget=World->SpawnActor<AOBTrainingTarget>();
    Director->BeginPrototypeExercise(EOBTrainingStep::Reload);
    TestEqual(TEXT("Basic training starts empty with a twenty-round capacity"),PC->GetCurrentAmmo(),0);
    TestEqual(TEXT("Basic training capacity is twenty"),PC->GetMagazineCapacity(),20);
    for (int32 I=0;I<6;++I)
    {
        PC->CompleteReload();
        TestEqual(TEXT("Every basic reload sets exactly three rounds; it never adds three"),PC->GetCurrentAmmo(),3);
    }
    PC->SetTrainingAmmo(2,20,3);PC->CompleteReload();
    TestEqual(TEXT("Two remaining rounds are replaced by three, not increased to five"),PC->GetCurrentAmmo(),3);
    Director->BeginPrototypeExercise(EOBTrainingStep::Accuracy);
    TestEqual(TEXT("Zeroing begins at 3/20"),PC->GetCurrentAmmo(),3);
    TestEqual(TEXT("Zeroing does not shrink the magazine capacity to three"),PC->GetMagazineCapacity(),20);
    int32 Shots=0;PC->OnShotResolved.AddLambda([&](const FHitResult&){++Shots;});
    const FVector Start(0,0,10000),End(1000,0,10000);
    PC->FireAlongRay(Start,End);
    TestEqual(TEXT("A basic shot consumes one round"),PC->GetCurrentAmmo(),2);
    // Exercise the reload policy independently of zeroing's existing step input gate.
    PC->SetTrainingControls(true,true,true,false);PC->CompleteReload();
    TestEqual(TEXT("Shooting preserves the basic three-round reload policy"),PC->GetCurrentAmmo(),3);
    for (int32 I=0;I<4;++I) PC->FireAlongRay(Start,End);
    TestEqual(TEXT("Basic ammo reaches zero without an extra live shot"),Shots,4);
    TestEqual(TEXT("Basic ammunition cannot become negative"),PC->GetCurrentAmmo(),0);
    Director->PrototypeRouteIndex=3;Director->BeginPrototypeExercise(EOBTrainingStep::TimedTargets);
    PC->SetCrouchMode(false);
    TestEqual(TEXT("Mock training starts at 20/20"),PC->GetCurrentAmmo(),20);
    TestEqual(TEXT("Mock training capacity is twenty"),PC->GetMagazineCapacity(),20);
    for (int32 I=0;I<12;++I) PC->FireAlongRay(Start,End);
    Director->PrototypeRouteIndex=4;Director->BeginPrototypeExercise(EOBTrainingStep::TimedTargets);
    TestEqual(TEXT("The second mock shooting point retains eight rounds"),PC->GetCurrentAmmo(),8);
    for (int32 I=0;I<10;++I) PC->FireAlongRay(Start,End);
    TestEqual(TEXT("Mock training stops after twenty shots without a reload"),Shots,24);
    TestEqual(TEXT("Mock training remains empty after extra trigger pulls"),PC->GetCurrentAmmo(),0);
    PC->CompleteReload();
    TestEqual(TEXT("Mock reload fills twenty rounds rather than using the basic three-round rule"),PC->GetCurrentAmmo(),20);
    for (int32 I=0;I<3;++I) PC->CompleteReload();
    TestEqual(TEXT("Repeated mock reloads never exceed twenty"),PC->GetCurrentAmmo(),20);
    PC->SetTrainingAmmo(100,100,100);
    TestEqual(TEXT("Even stale training settings cannot exceed twenty rounds"),PC->GetCurrentAmmo(),20);
    TestEqual(TEXT("Training capacity is capped at twenty"),PC->GetMagazineCapacity(),20);
    return !HasAnyErrors();
}
#endif

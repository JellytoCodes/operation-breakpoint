#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Core/OBQACheatManager.h"
#include "Core/OBPlayerController.h"
#include "Training/OBTrainingDirector.h"
#include "Training/OBTrainingTarget.h"
#include "Training/OBTrainingDoor.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Character/OBActionComponent.h"
#include "World/OBRailPath.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "TimerManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBQACheatManagerTest,"OperationBreakpoint.QA.CommandGuards",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOBQACheatManagerTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* PC=World->SpawnActor<AOBPlayerController>();
    auto* Player=World->SpawnActor<AOBPlayerCharacter>(); PC->Possess(Player);
    Player->SetActorLocation(FVector(100,200,300)); PC->SetTrainingAmmo(7,30);
    auto* Cheats=NewObject<UOBQACheatManager>(PC);
    FString Reply;
    TestTrue(TEXT("QA activation includes editor builds"),Cheats->IsEnabledBuild());
    TestEqual(TEXT("Editor player controllers select the QA CheatManager"),PC->CheatClass.Get(),UOBQACheatManager::StaticClass());
    for (const auto* Command : {TEXT("Help"),TEXT("List"),TEXT("Status"),TEXT("hElP")})
        TestTrue(TEXT("Common commands work outside TutorialMap"),Cheats->ExecuteCommand(Command,Reply));
    for (const auto* Command : {TEXT("Goto Accuracy"),TEXT("Restart"),TEXT("Next"),TEXT("UI 3001"),TEXT("UI 3010"),TEXT("UI Close")})
    {
        TestFalse(TEXT("Tutorial commands reject the wrong map"),Cheats->ExecuteCommand(Command,Reply));
        TestTrue(TEXT("Rejection identifies invalid access and expected map"),Reply.Contains(TEXT("잘못된 접근")) && Reply.Contains(TEXT("TutorialMap")));
        TestTrue(TEXT("Rejected command preserves player location"),Player->GetActorLocation().Equals(FVector(100,200,300)));
        TestEqual(TEXT("Rejected command preserves ammunition"),PC->GetCurrentAmmo(),7);
    }
    for (const auto* Command : {TEXT("Goto TutorialMap Accuracy"),TEXT("Goto"),TEXT("Unknown"),TEXT("Reset TutorialMap"),
        TEXT("UI"),TEXT("UI 3000"),TEXT("UI 3011"),TEXT("UI 3001x"),TEXT("UI 3001 3002"),TEXT("UI 3001.5")})
        TestFalse(TEXT("Malformed commands cannot mutate the world"),Cheats->ExecuteCommand(Command,Reply));
    TestTrue(TEXT("Help documents all UI pages and how to close them"),Cheats->ExecuteCommand(TEXT("Help"),Reply)
        && Reply.Contains(TEXT("QA UI 3001")) && Reply.Contains(TEXT("QA UI 3010")) && Reply.Contains(TEXT("QA UI Close")));
    TestTrue(TEXT("Help describes both live Zone6 checkpoints and device-free input"),Reply.Contains(TEXT("QA Breach 83"))
        && Reply.Contains(TEXT("QA Breach 85")) && Reply.Contains(TEXT("QA Breach Trigger")));
    for (const auto* Command : {TEXT("Breach"),TEXT("Breach 83"),TEXT("Breach 85"),TEXT("Breach Trigger")})
    {
        TestFalse(TEXT("Breach checkpoints reject other maps"),Cheats->ExecuteCommand(Command,Reply));
        TestTrue(TEXT("Breach guard explains the required map"),Reply.Contains(TEXT("Stage_01-only")));
        TestTrue(TEXT("Rejected breach preserves player location"),Player->GetActorLocation().Equals(FVector(100,200,300)));
        TestTrue(TEXT("Rejected breach cannot queue level travel"),World->NextURL.IsEmpty());
    }
    for (const auto* Command : {TEXT("Breach 84"),TEXT("Breach 85x"),TEXT("Breach 83 85"),TEXT("Breach Trigger Extra")})
        TestFalse(TEXT("Malformed breach commands cannot request a checkpoint"),Cheats->ExecuteCommand(Command,Reply));
    AddExpectedMessagePlain(TEXT("잘못된 접근: QA goto is TutorialMap-only."),ELogVerbosity::Warning);
    Cheats->QA(TEXT("Goto Accuracy"));
    TestEqual(TEXT("Editor exec invocation still preserves state on the wrong map"),PC->GetCurrentAmmo(),7);

    auto* PieWorld=UWorld::CreateWorld(EWorldType::Game,false,TEXT("UEDPIE_91_TutorialMap"),CreatePackage(TEXT("/Game/Maps/UEDPIE_91_TutorialMap")));
    TestTrue(TEXT("Map guard normalizes the UE PIE prefix"),UOBQACheatManager::IsTrainingMap(PieWorld));
    PieWorld->DestroyWorld(false);
    auto* SimilarWorld=UWorld::CreateWorld(EWorldType::Game,false,TEXT("NotTutorialMap"),CreatePackage(TEXT("/Game/Maps/NotTutorialMap")));
    TestFalse(TEXT("Similar map names are not accepted"),UOBQACheatManager::IsTrainingMap(SimilarWorld));
    SimilarWorld->DestroyWorld(false);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBQATrainingCheckpointTest,"OperationBreakpoint.QA.TrainingCheckpointEntry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOBQATrainingCheckpointTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Player=World->SpawnActor<AOBPlayerCharacter>();
    auto* PC=World->SpawnActor<AOBPlayerController>(); PC->Possess(Player);
    PC->PlayerCameraManager=World->SpawnActor<APlayerCameraManager>(); PC->PlayerCameraManager->InitializeFor(PC);
    auto* Rail=World->SpawnActor<AOBRailPath>(); Rail->bExternallyControlled=true;
    Rail->GetSpline()->ClearSplinePoints();
    for (int32 I=0; I<18; ++I)
        Rail->GetSpline()->AddSplinePoint(FVector(I*100,0,I*20),ESplineCoordinateSpace::World);
    TMap<int32,FOBRailStopSettings> Stops;
    for (int32 I : {2,4,7,12,17}) Stops.Add(I);
    *FindFProperty<FMapProperty>(AOBRailPath::StaticClass(),TEXT("StopPointSettings"))->ContainerPtrToValuePtr<TMap<int32,FOBRailStopSettings>>(Rail)=Stops;
    auto* Spline=Player->GetPlayerSplineComponent();
    Spline->PlayerCharacter=Player; Spline->PlayerCamera=Player->GetPlayerCamera(); Spline->RailPath=Rail;
    Spline->FirstPersonCameraLocation=Player->GetPlayerCamera()->GetRelativeLocation();
    Spline->FirstPersonCameraRotation=Player->GetPlayerCamera()->GetRelativeRotation();
    auto* Director=World->SpawnActor<AOBTrainingDirector>();
    ON_SCOPE_EXIT {
        Director->EndPlay(EEndPlayReason::Destroyed);
        World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    };
    Director->bInitialized=Director->bUseSplinePrototype=true;
    Director->Controller=PC; Director->Spline=Spline; Director->Action=Player->GetActionComponent();
    Director->AccuracyTarget=World->SpawnActor<AOBTrainingTarget>();
    for (int32 I=0; I<6; ++I) Director->TimedTargets.Add(World->SpawnActor<AOBTrainingTarget>(FVector(4000,I*300,0),FRotator::ZeroRotator));
    Director->BreachDoor=World->SpawnActor<AOBTrainingDoor>();
    FString Error;
    for (const auto& Entry : TArray<TPair<FName,int32>>{{TEXT("WeaponCheck"),2},{TEXT("AimHold"),2},{TEXT("Cover"),2},{TEXT("Reload"),2},
        {TEXT("Accuracy"),2},{TEXT("Countdown"),7},{TEXT("Shooting1"),7},{TEXT("Shooting2"),12},{TEXT("Breach"),17}})
    {
        TestTrue(TEXT("Checkpoint can be entered in arbitrary order"),Director->EnterQACheckpoint(Entry.Key,Error));
        TestEqual(TEXT("Spline index matches checkpoint"),Spline->GetCurrentPointIndex(),Entry.Value);
        FVector Feet=Player->GetActorLocation(); Feet.Z-=Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        TestTrue(TEXT("Teleport synchronizes feet XYZ to the saved spline"),Feet.Equals(Rail->GetSpline()->GetLocationAtSplinePoint(Entry.Value,ESplineCoordinateSpace::World),.01f));
        TestTrue(TEXT("Checkpoint exposes a QA-marked session"),Director->Result.bUsedTestInputs && Director->bQASession);
        TestFalse(TEXT("No previous turning or narration is active"),Director->bPrototypeTurning || Director->bPrototypeIntroPending || Director->bPrototypeNarrationActive);
    }
    Director->EnterQACheckpoint(TEXT("Accuracy"),Error);
    const FVector Before=Player->GetActorLocation();
    const FGuid SessionBefore=Director->Result.SessionId;
    TestFalse(TEXT("Unsupported checkpoint is rejected"),Director->EnterQACheckpoint(TEXT("BadName"),Error));
    TestFalse(TEXT("Prototype does not invent a legacy calibration phase"),Director->EnterQACheckpoint(TEXT("Calibration"),Error));
    TestTrue(TEXT("Rejected checkpoint leaves active session intact"),SessionBefore==Director->Result.SessionId && Player->GetActorLocation().Equals(Before));
    PC->SetTrainingAmmo(1,3); Director->Shots=2; Director->Hits=1;
    Director->EnterQACheckpoint(TEXT("Accuracy"),Error);
    TestEqual(TEXT("Restart replenishes all three zeroing rounds"),PC->GetCurrentAmmo(),3);
    TestEqual(TEXT("Restart clears the previous shot measurement"),Director->Shots,0);
    TestTrue(TEXT("Accuracy target is immediately ready"),Director->AccuracyTarget->IsTargetRaised());
    TestEqual(TEXT("Exercise input is enabled after checkpoint entry"),Player->GetActionComponent()->GetActionState(),EOBActionState::Action);
    Director->EnterQACheckpoint(TEXT("Countdown"),Error);
    TestTrue(TEXT("Countdown uses the real start-signal timer"),Director->bPrototypeMockStartPending);
    Director->EnterQACheckpoint(TEXT("Breach"),Error);
    TestFalse(TEXT("Jump cancels the previous countdown timer"),World->GetTimerManager().IsTimerActive(Director->PrototypeMockStartTimer));
    TestFalse(TEXT("Breach door is reset for a fresh test"),Director->BreachDoor->IsBreached());
    Director->EnterQACheckpoint(TEXT("Result"),Error);
    TestTrue(TEXT("Result preview can finish without issuing a qualification"),Director->Result.bFinished && !Director->Result.bPassed);
    TestTrue(TEXT("Can restart an exercise after finishing the result"),Director->EnterQACheckpoint(TEXT("Reload"),Error));
    TestFalse(TEXT("Finished state is cleared for the new test"),Director->Result.bFinished);
    TestEqual(TEXT("Reload begins with an empty magazine"),PC->GetCurrentAmmo(),0);
    TestTrue(TEXT("Intro can be restarted explicitly"),Director->EnterQACheckpoint(TEXT("Intro"),Error));
    TestEqual(TEXT("Intro state is restored before its fade timer"),Director->CurrentStep,EOBTrainingStep::Intro);
    TestTrue(TEXT("Intro restarts its normal fade and dialogue lifecycle"),Director->bPrototypeIntroPending);
    TestEqual(TEXT("Intro places the player at the first route point"),Spline->GetCurrentPointIndex(),0);
    return !HasAnyErrors();
}
#endif

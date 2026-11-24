#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SplineComponent.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Stage/OBStageFlowComponent.h"
#include "World/OBRailPath.h"
#include "World/OBBreachDoor.h"
#include "MediaPipe/OBMediaPipeOSCSubsystem.h"
#include "Core/OBPlayerController.h"
#include "OSCAddress.h"
#include "OSCManager.h"
#include "OSCMessage.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBStageZone6GateTest, "OperationBreakpoint.Stage.Zone6Gates",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOBStageZone6GateTest::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Instance = NewObject<UGameInstance>(GEngine);
    World->SetGameInstance(Instance);
    FURL URL; URL.AddOption(TEXT("game=/Script/Engine.GameModeBase")); World->SetGameMode(URL);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };

    auto* Controller = World->SpawnActor<AOBPlayerController>();
    World->AddController(Controller);
    auto* Player = World->SpawnActor<AOBPlayerCharacter>(); Controller->Possess(Player);
    auto* Rail = Player->GetPlayerSplineComponent();
    auto* Path = World->SpawnActor<AOBRailPath>();
    auto* Spline = Path->GetSpline(); Spline->ClearSplinePoints(false);
    for (int32 Point = 0; Point <= 87; ++Point)
        Spline->AddSplinePoint(FVector(Point * 100., 0, 100), ESplineCoordinateSpace::World, false);
    Spline->UpdateSpline();
    Rail->PlayerCharacter = Player; Rail->RailPath = Path; Rail->PlayerCamera = Player->GetPlayerCamera();
    Rail->CurrentPointIndex = 83; Rail->RailState = UOBPlayerSplineComponent::EOBRailState::Action;
    Rail->CurrentDistance = Rail->TargetDistance = Spline->GetDistanceAlongSplineAtSplinePoint(83);

    auto* Flow = Player->GetStageFlowComponent();
    Flow->Player = Player; Flow->bActive = true; Flow->Phase = EOBStagePhase::Travel;
    Flow->bUseZone6Breach = true;
    auto* Scripts = NewObject<UDataTable>(); Scripts->RowStruct = FOBStageScriptRow::StaticStruct();
    for (int32 Id : {2016, 2017, 2018, 2022, 2023})
    {
        FOBStageScriptRow Row; Row.Text = FText::FromString(FString::FromInt(Id)); Row.Display_Time = .2f;
        Scripts->AddRow(FName(*FString::FromInt(Id)), Row);
    }
    Flow->LoadedScripts = Scripts;
    auto* Motion = NewObject<UOBMediaPipeOSCSubsystem>(Instance);
    Flow->MotionInput = Motion;
    Motion->OnBreachStateChanged.AddUObject(Flow, &UOBStageFlowComponent::HandleBreachStateChanged);
    auto SendBreach = [&](int32 State) {
        FOSCMessage Message; UOSCManager::SetOSCMessageAddress(Message, FOSCAddress(TEXT("/motion/breach")));
        UOSCManager::AddInt32(Message, State); Motion->HandleOSCMessage(Message, TEXT("127.0.0.1"), 8000);
    };

    TestEqual(TEXT("83 is inserted before the authored final stop"), Flow->GetNextStageStop(82, 86), 83);
    TestEqual(TEXT("85 follows the dialogue stop"), Flow->GetNextStageStop(83, 86), 85);
    TestEqual(TEXT("86 follows the breach stop"), Flow->GetNextStageStop(85, INDEX_NONE), 86);
    TestFalse(TEXT("A final encounter overlap cannot activate before the breach"), Flow->CanActivateEncounter(86));

    SendBreach(1); // Recognition is already held before either Zone6 stop.
    Flow->HandleActionPoint(83);
    TestTrue(TEXT("83 holds movement and input without starting a combat clock"),
        Flow->GetPhase() == EOBStagePhase::Zone6Dialogue && Flow->IsInputBlocked()
        && Player->GetCharacterMovement()->MovementMode == MOVE_None && Flow->GetRemainingSeconds() == 0.f);
    TestEqual(TEXT("The hostage speaks first at 83"), Flow->ActiveScriptId, 2017);
    TestTrue(TEXT("Only the terrorist follows the hostage here"), Flow->NarrationQueue.Num() == 1 && Flow->NarrationQueue[0].Id == 2018);
    Flow->HandleActionPoint(83);
    TestEqual(TEXT("Repeated arrival does not duplicate the two-line queue"), Flow->NarrationQueue.Num(), 1);
    Flow->TickNarration(.21f);
    TestEqual(TEXT("The terrorist follows the finished hostage line"), Flow->ActiveScriptId, 2018);
    Flow->TickZone6Gate(10.f);
    TestTrue(TEXT("No elapsed delay can release 83 while a line is still active"), Flow->GetPhase() == EOBStagePhase::Zone6Dialogue);
    Flow->TickNarration(.21f);
    Flow->TickZone6Gate(0.f);
    TestTrue(TEXT("The pause starts after both lines, not on arrival"), Flow->bZone6DialogueFinished && Flow->Zone6GapRemaining > 0.f);
    Flow->TickZone6Gate(Flow->Zone6GapRemaining - .01f);
    TestTrue(TEXT("The deliberate pause still blocks input until its end"), Flow->GetPhase() == EOBStagePhase::Zone6Dialogue && Flow->IsInputBlocked());
    Flow->TickZone6Gate(.02f);
    TestTrue(TEXT("Dialogue and the following pause release travel"), Flow->GetPhase() == EOBStagePhase::Travel && !Rail->bStageMovementBlocked);

    // This settings-only test does not pretend a queued Chaos command is a real break.
    // The real-PIE fixture supplies the authored collection and observes IsRootBroken().
    auto* Door = World->SpawnActor<AOBBreachDoor>(); Flow->Zone6Door = Door;
    Rail->CurrentPointIndex = 85; Rail->RailState = UOBPlayerSplineComponent::EOBRailState::Action;
    Flow->HandleActionPoint(85);
    TestTrue(TEXT("85 arms the dedicated breach gate and its arrival instruction"), Flow->IsWaitingForBreach() && Flow->ActiveScriptId == 2016);
    TestFalse(TEXT("A held pre-arrival recognition is not neutral"), Flow->bBreachSawNeutral);
    SendBreach(1);
    TestFalse(TEXT("Repeating a held OSC true does not request a breach"), Flow->bBreachRequested);
    TestFalse(TEXT("No clock or overlap can complete the waiting door"), Flow->bBreachCompleted || Flow->CanActivateEncounter(86));
    Flow->TickComponent(5.f, LEVELTICK_All, &Flow->PrimaryComponentTick);
    TestTrue(TEXT("Waiting for a new gesture leaves the hostage timer stopped"), Flow->IsWaitingForBreach() && Flow->GetRemainingSeconds() == 0.f);
    SendBreach(0);
    TestTrue(TEXT("Legacy OSC neutral is parsed without planting Stage_01 C4"), !Flow->bBreachRequested && !Motion->IsBreachRecognized());
    Flow->SuspendForQA(); SendBreach(1);
    TestTrue(TEXT("QA cancels the pending gate and ignores later recognition"), Flow->GetPhase() == EOBStagePhase::QA && !Flow->bBreachRequested && !Flow->ActiveBreachDoor);

    // Model a completed external door event only for the stop/timer boundary.
    Door->bBreached=true;
    Flow->ActiveBreachDoor=Door;
    Flow->Phase = EOBStagePhase::Zone6Breach; Flow->bBreachRequested=Flow->bBreachPhysicsStarted=Flow->bBreachCompleted=true;
    Flow->bBreachBlackHoldStarted = false; Flow->BreachFadeOutRemaining = .4f; Flow->BreachBlackRemaining = 0.f;
    Rail->SetStageMovementBlocked(true);
    Flow->TickZone6Gate(.39f);
    TestFalse(TEXT("Fade-out does not consume or start the full-black hold"), Flow->bBreachBlackHoldStarted);
    Flow->TickZone6Gate(.02f);
    TestTrue(TEXT("The full two-second hold starts after fade-out, with movement blocked"),
        Flow->bBreachBlackHoldStarted && Flow->BreachBlackRemaining == 2.f && Rail->bStageMovementBlocked);
    Flow->TickZone6Gate(1.99f);
    TestTrue(TEXT("Movement remains blocked throughout the independent full-black hold"),
        Flow->GetPhase() == EOBStagePhase::Zone6Breach && Rail->bStageMovementBlocked);
    Flow->TickZone6Gate(.02f);
    TestTrue(TEXT("Only the completed full-black hold releases travel and fade-in"),
        Flow->GetPhase() == EOBStagePhase::Travel && !Rail->bStageMovementBlocked);
    Flow->Phase = EOBStagePhase::Travel; Flow->bBreachCompleted = true;
    // Travel was really released above; model the next stopped arrival independently.
    Rail->RailState = UOBPlayerSplineComponent::EOBRailState::Action;
    Rail->CurrentPointIndex = 85;
    TestFalse(TEXT("A completed door still cannot spawn combat from stop 85"), Flow->CanActivateEncounter(86));
    Rail->CurrentPointIndex = 86;
    TestTrue(TEXT("The completed door permits the actual encounter stop"), Flow->CanActivateEncounter(86));
    Flow->HandleActionPoint(86);
    Flow->StageRecord.bUsedTestInputs=true;
    TestTrue(TEXT("86 starts the independent three-second rescue introduction"), Flow->GetPhase() == EOBStagePhase::Zone6Intro && Flow->IsInputBlocked() && Flow->GetRemainingSeconds() == 10.f);
    Flow->TickComponent(.5f, LEVELTICK_All, &Flow->PrimaryComponentTick);
    TestEqual(TEXT("The introduction does not consume the rescue clock"), Flow->GetRemainingSeconds(),10.f);
    Flow->TickComponent(2.5f,LEVELTICK_All,&Flow->PrimaryComponentTick);
    TestTrue(TEXT("Input opens after the introduction with the full ten seconds"),Flow->GetPhase()==EOBStagePhase::Combat && !Flow->IsInputBlocked() && Flow->GetRemainingSeconds()==10.f);
    Flow->TickComponent(.5f,LEVELTICK_All,&Flow->PrimaryComponentTick);
    TestEqual(TEXT("Only active rescue time is scored"),Flow->GetRemainingSeconds(),9.5f);
    TestEqual(TEXT("Zone6 never changes global time dilation"), World->GetWorldSettings()->GetEffectiveTimeDilation(), 1.f);

    for (bool bSuccess : {true, false})
    {
        Flow->ClearPresentation(); Flow->Phase = EOBStagePhase::Ending; Flow->bSucceeded = bSuccess;
        Flow->bEndingVideoOpened = true; Flow->FinishEndingVideo();
        TestTrue(TEXT("An opened ending completes without replaying its caption as later narration"),
            Flow->GetPhase() == EOBStagePhase::Complete && !Flow->ActiveScriptId && !Flow->NarrationVoice);
        Flow->Phase = EOBStagePhase::Ending; Flow->bSucceeded = bSuccess; Flow->bEndingVideoOpened = false;
        Flow->FinishEndingVideo();
        TestEqual(TEXT("Unavailable media retains the matching text-only fallback"), Flow->ActiveScriptId, bSuccess ? 2022 : 2023);
        Flow->TickNarration(.21f);
        TestTrue(TEXT("The media-failure fallback also reaches completion"), Flow->GetPhase() == EOBStagePhase::Complete);
    }
    Motion->OnBreachStateChanged.RemoveAll(Flow); Flow->bActive = false;
    return !HasAnyErrors();
}
#endif

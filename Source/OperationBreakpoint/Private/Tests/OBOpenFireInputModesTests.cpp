#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/ScopeExit.h"
#include "Input/OBOpenFireTypes.h"
#include "Core/OBPlayerController.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBActionComponent.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Stage/OBStageFlowComponent.h"
#include "Widget/OBHUD.h"
#include "Widget/OBPlayerOverlayWidget.h"
#include "Training/OBTrainingResultWidget.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBOpenFireRawInteractionTest,"OperationBreakpoint.OpenFire.RawInteraction",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOBOpenFireRawInteractionTest::RunTest(const FString&)
{
    const FString Json=TEXT(R"({"v":1,"session":"raw-test","device":"gun","sequence":1,"x":0.5,"y":0.5,"connected":true,"aim_valid":false,"demo":false,"calibrated":false,"trigger":false,"reload":false,"calibrate":false,"trigger_count":0,"reload_count":0,"calibrate_count":0,"interaction_trigger":false,"interaction_trigger_count":0})");
    FOBOpenFireFrame Frame;FOBOpenFireState State;
    TestTrue(TEXT("Raw UI channel is valid with zero LEDs"),FOBOpenFireFrame::Decode(Json,Frame));
    State.Accept(Frame,1.,false);
    Frame.Sequence++;Frame.bInteractionTrigger=true;Frame.InteractionTriggerCount++;
    State.Accept(Frame,1.01,false);
    TestTrue(TEXT("Zero LED input produces a UI press and hold"),State.Events.bInteractionPressed && State.Events.bInteractionHeld);
    TestFalse(TEXT("The same press has no combat event or aim"),State.Events.bTriggerPressed || State.CanAim(1.01));
    Frame.Sequence++;Frame.bAimValid=true;
    State.Accept(Frame,1.02,false);
    TestTrue(TEXT("LED reacquisition restores aim"),State.CanAim(1.02));
    TestFalse(TEXT("A held offscreen UI press cannot become a delayed shot"),State.Events.bTriggerPressed);
    Frame.Sequence++;Frame.bInteractionTrigger=false;State.Accept(Frame,1.03,false);
    Frame.Sequence++;Frame.bTrigger=Frame.bInteractionTrigger=true;Frame.TriggerCount++;Frame.InteractionTriggerCount++;
    State.Accept(Frame,1.04,false);
    TestTrue(TEXT("A new on-screen pull can produce a combat press"),State.Events.bTriggerPressed);
    TestFalse(TEXT("A malformed raw-channel pair is rejected"),FOBOpenFireFrame::Decode(Json.Replace(TEXT(",\"interaction_trigger_count\":0"),TEXT("")),Frame));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBOpenFireInputModesTest,"OperationBreakpoint.OpenFire.InputModes",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOBOpenFireInputModesTest::RunTest(const FString&)
{
    const FString Previous(FCommandLine::Get());FCommandLine::Set(*(Previous+TEXT(" -OBOpenFire")));
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false);GEngine->DestroyWorldContext(World);FCommandLine::Set(*Previous); };
    auto* Player=World->SpawnActor<AOBPlayerCharacter>();auto* PC=World->SpawnActor<AOBPlayerController>();PC->Possess(Player);
    auto* Action=Player->GetActionComponent();auto* Spline=Player->GetPlayerSplineComponent();
    Action->SetExternalActionState(true);
    PC->SetTrainingControls(true);PC->SetTrainingAmmo(1,30);
    int32 Triggers=0,Reloads=0,Shots=0;
    PC->OnTriggerPressed.AddLambda([&]{++Triggers;});PC->OnReloadCompleted.AddLambda([&]{++Reloads;});
    PC->OnShotResolved.AddLambda([&](const FHitResult&){++Shots;});
    FOBOpenFireEvents Released,UI;UI.bInteractionHeld=UI.bInteractionPressed=true;
    PC->ApplyOpenFireInput(Released);
    TestTrue(TEXT("A guide uses Interaction mode"),PC->GetGunInputMode()==EOBGunInputMode::Interaction);
    PC->ApplyOpenFireInput(UI);
    TestEqual(TEXT("A UI pull advances the guide without aim"),Triggers,1);
    TestEqual(TEXT("A UI pull never fires"),Shots,0);
    PC->CompleteReload();TestEqual(TEXT("Interaction mode cannot reload"),Reloads,0);
    PC->SetTrainingControls(true,true,true,false);
    PC->ApplyOpenFireInput(UI);
    TestFalse(TEXT("Holding through the switch to Shooting is disarmed"),PC->IsTriggerHeld());
    PC->ApplyOpenFireInput(Released);
    FOBOpenFireEvents Combat=UI;Combat.bTriggerHeld=Combat.bTriggerPressed=true;
    PC->ApplyOpenFireInput(Combat);
    TestEqual(TEXT("A combat pull without valid aim cannot advance a guide or shoot"),Triggers,1);
    TestEqual(TEXT("Offscreen combat retains ammunition"),PC->GetCurrentAmmo(),1);
    PC->CompleteReload();TestEqual(TEXT("Shooting reload works without LEDs"),PC->GetCurrentAmmo(),20);
    TestEqual(TEXT("Shooting reload emits one completion"),Reloads,1);
    Action->SetExternalActionState(false);Spline->RailState=UOBPlayerSplineComponent::EOBRailState::Moving;
    PC->SetTrainingControls(true);PC->SetTrainingAmmo(4,30);
    TestTrue(TEXT("Spline movement has an explicit Travel mode"),PC->GetGunInputMode()==EOBGunInputMode::Travel);
    PC->CompleteReload();TestEqual(TEXT("Moving reload defaults off"),PC->GetCurrentAmmo(),4);
    PC->bAllowReloadWhileMoving=true;PC->CompleteReload();
    TestEqual(TEXT("The bool flag enables moving reload despite Prepare state"),PC->GetCurrentAmmo(),20);
    PC->ApplyOpenFireInput(Released);PC->ApplyOpenFireInput(Combat);
    TestEqual(TEXT("Travel mode still cannot shoot"),Shots,0);
    PC->bAllowReloadWhileMoving=false;
    auto* Stage=Player->GetStageFlowComponent();Stage->bActive=true;Stage->Phase=EOBStagePhase::Travel;
    TestTrue(TEXT("Stage_01 travel enables reload without opting in the tutorial"),PC->CanReloadInCurrentMode());
    for(const auto Phase:{EOBStagePhase::Opening,EOBStagePhase::Ending,EOBStagePhase::Complete,EOBStagePhase::AwaitingDeparture,EOBStagePhase::Zone6Breach})
    {
        Stage->Phase=Phase;
        TestFalse(TEXT("Stage cinematics, results and breach still block travel reload"),PC->CanReloadInCurrentMode());
    }
    Stage->Phase=EOBStagePhase::Travel;Stage->bActive=false;
    TestFalse(TEXT("Leaving Stage_01 restores the tutorial moving-reload policy"),PC->CanReloadInCurrentMode());
    PC->bAllowReloadWhileMoving=true;

    // Wire the real result widget into the real controller routing. Automatic
    // navigation is disabled so this isolated test cannot leave its test world.
    auto* HUD=World->SpawnActor<AOBHUD>();PC->MyHUD=HUD;
    auto* OverlayClass=LoadClass<UOBPlayerOverlayWidget>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/WBP_PlayerOverlay.WBP_PlayerOverlay_C"));
    if (!TestNotNull(TEXT("Actual overlay Blueprint"),OverlayClass)) return false;
    auto* Overlay=CreateWidget<UOBPlayerOverlayWidget>(World,OverlayClass);
    auto* Result=CreateWidget<UOBTrainingResultWidget>(World,UOBTrainingResultWidget::StaticClass());
    if (!TestNotNull(TEXT("Overlay"),Overlay) || !TestNotNull(TEXT("Result widget"),Result)) return false;
    const auto OverlaySlate=Overlay->TakeWidget();
    const auto ResultSlate=Result->TakeWidget();
    HUD->PlayerOverlayWidget=Overlay;Overlay->TrainingResult=Result;Overlay->bTrainingResultShown=true;
    Overlay->SetVisibility(ESlateVisibility::Visible);Result->SetVisibility(ESlateVisibility::Visible);
    TestTrue(TEXT("The actual Slate widgets are visible"),OverlaySlate->GetVisibility().IsVisible() && ResultSlate->GetVisibility().IsVisible());
    TestTrue(TEXT("Visible result automatically switches to Interaction"),PC->GetGunInputMode()==EOBGunInputMode::Interaction);
    Result->bAutomaticNavigation=false;
    FOBTrainingResultData Data;PC->ApplyOpenFireInput(Released);Result->SetResult(Data);
    PC->ApplyOpenFireInput(Released);PC->ApplyOpenFireInput(UI);
    FOBOpenFireEvents Hold=UI;Hold.bInteractionPressed=false;PC->ApplyOpenFireInput(Hold);
    Result->AdvanceInteraction(2.1f);
    TestTrue(TEXT("Result long press selects retraining without LEDs"),Result->LastAction==EOBTrainingResultAction::Retry);
    TestFalse(TEXT("UI result rejects reload even with moving reload enabled"),PC->CanReloadInCurrentMode());
    PC->ApplyOpenFireInput(Released);Result->SetResult(Data);PC->ApplyOpenFireInput(UI);PC->ApplyOpenFireInput(Released);
    TestTrue(TEXT("Result short tap selects the next screen without LEDs"),Result->LastAction==EOBTrainingResultAction::NextStage);
    Result->SetResult(Data);PC->ApplyOpenFireInput(UI);PC->CancelOpenFireInput();Result->AdvanceInteraction(3.f);
    TestTrue(TEXT("Connection or focus loss cancels rather than selecting a result action"),Result->LastAction==EOBTrainingResultAction::None);
    PC->ApplyOpenFireInput(Hold);Result->AdvanceInteraction(3.f);
    TestTrue(TEXT("Focus restoration with a held trigger remains disarmed"),Result->LastAction==EOBTrainingResultAction::None);
    PC->ApplyOpenFireInput(Released);PC->ApplyOpenFireInput(UI);PC->ApplyOpenFireInput(Released);
    TestTrue(TEXT("A fresh tap after release works immediately"),Result->LastAction==EOBTrainingResultAction::NextStage);
    TestEqual(TEXT("No interaction resolved a shot"),Shots,0);
    return !HasAnyErrors();
}
#endif

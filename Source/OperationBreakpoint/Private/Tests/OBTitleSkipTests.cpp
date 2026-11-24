#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Training/OBTrainingDirector.h"
#include "Widget/OBTrainingSkipWidget.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "TimerManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBTitleSkipTest,"OperationBreakpoint.Training.InitialSkipGate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOBTitleSkipTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* D=World->SpawnActor<AOBTrainingDirector>();
    ON_SCOPE_EXIT { D->StopPrototypeNarration(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    D->bInitialized=D->bUseSplinePrototype=D->bInitialSkipOpen=true;
    D->bPrototypeIntroPending=true;
    TestFalse(TEXT("Skip is hidden during the unchanged black intro"),D->IsInitialSkipAvailable());
    D->bPrototypeIntroPending=false; D->CurrentDialogueId=2;
    TestTrue(TEXT("Instructor intro opens the skip window"),D->IsInitialSkipAvailable());
    D->CurrentStep=EOBTrainingStep::Welcome; D->CurrentDialogueId=3; D->bPrototypeNarrationActive=true;
    D->PrototypeMessage=AOBTrainingDirector::EPrototypeMessage::Narration;
    D->GuideTriggerDelaySeconds=1.f; D->FinishPrototypeNarration();
    TestTrue(TEXT("Skip remains open during the post-voice delay"),D->IsInitialSkipAvailable());
    for (int32 I=0;I<120;++I) { ++GFrameCounter; World->GetTimerManager().Tick(.01f); }
    TestTrue(TEXT("Guide trigger becomes ready"),D->bGuideAdvanceReady);
    TestFalse(TEXT("That same event permanently closes skip"),D->IsInitialSkipAvailable());
    TestFalse(TEXT("Late skip requests cannot travel"),D->RequestInitialSkip());
    D->CurrentDialogueId=2;
    TestFalse(TEXT("Revisiting earlier dialogue does not reopen the window"),D->IsInitialSkipAvailable());

    auto* Skip=NewObject<UOBTrainingSkipWidget>(World); Skip->Initialize();
    Skip->SetAvailable(true,true); Skip->HandleTrigger(true); Skip->AdvanceHold(4);
    TestEqual(TEXT("A trigger held before the window opens cannot skip"),Skip->GetHoldProgress(),0.f);
    Skip->HandleTrigger(false); Skip->HandleTrigger(true); Skip->AdvanceHold(1.99f);
    TestFalse(TEXT("Less than two seconds does not skip"),Skip->bTriggered);
    Skip->HandleTrigger(false); TestEqual(TEXT("Releasing resets all hold progress"),Skip->GetHoldProgress(),0.f);
    Skip->HandleTrigger(true); Skip->AdvanceHold(2.f);
    TestTrue(TEXT("A continuous two second hold triggers once"),Skip->bTriggered);
    Skip->SetAvailable(false); Skip->HandleTrigger(true); Skip->AdvanceHold(10);
    TestFalse(TEXT("Disabled skip cannot be triggered again"),Skip->bTriggered);
    TestEqual(TEXT("Disabled skip has no remaining progress"),Skip->GetHoldProgress(),0.f);
    return !HasAnyErrors();
}
#endif

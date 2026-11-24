#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Widget/OBPlayerOverlayWidget.h"
#include "Training/OBTrainingDirector.h"
#include "Blueprint/WidgetTree.h"
#include "Components/BackgroundBlur.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBTrainingBackgroundBlurTest,"OperationBreakpoint.UI.TrainingBackgroundBlur",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOBTrainingBackgroundBlurTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Class=LoadClass<UOBPlayerOverlayWidget>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/WBP_PlayerOverlay.WBP_PlayerOverlay_C"));
    if (!TestNotNull(TEXT("Saved overlay loads"),Class)) return false;
    auto* Overlay=CreateWidget<UOBPlayerOverlayWidget>(World,Class); const auto Slate=Overlay->TakeWidget();
    auto* Blur=Cast<UBackgroundBlur>(Overlay->GetWidgetFromName(TEXT("TrainingBackgroundBlur")));
    if (!TestNotNull(TEXT("Saved overlay has a BackgroundBlur"),Blur)) return false;
    auto* Slot=CastChecked<UCanvasPanelSlot>(Blur->Slot);
    TestEqual(TEXT("Blur starts at the viewport origin"),Slot->GetAnchors().Minimum,FVector2D::ZeroVector);
    TestEqual(TEXT("Blur covers the full viewport"),Slot->GetAnchors().Maximum,FVector2D(1,1));
    for (const auto* Name:{TEXT("Guide"),TEXT("TrainingResult"),TEXT("Subtitle")})
        TestTrue(TEXT("Foreground UI is above the blur layer"),CastChecked<UCanvasPanelSlot>(Overlay->GetWidgetFromName(Name)->Slot)->GetZOrder()>Slot->GetZOrder());
    TestFalse(TEXT("Normal stage HUD is sharp"),Blur->IsVisible());
    auto* Director=World->SpawnActor<AOBTrainingDirector>(); Director->bUseSplinePrototype=true; Director->CurrentStep=EOBTrainingStep::Welcome;
    for (const int32 Id:{2,8,9,15,16,19,20})
    { Director->CurrentDialogueId=Id; TestFalse(FString::Printf(TEXT("Line %d is outside a guide block"),Id),Director->IsGuideSectionActive()); }
    for (const int32 Id:{3,4,5,6,7,17,18})
    {
        Director->CurrentDialogueId=Id; Overlay->SetTrainingDirector(Director);
        TestTrue(FString::Printf(TEXT("Line %d keeps background blur active"),Id),Blur->IsVisible());
    }
    for (const int32 Previous:{3,6,17})
    {
        Director->CurrentDialogueId=0; Director->PrototypePresentedId=Previous;
        Overlay->SetTrainingDirector(Director);
        Overlay->SetPanelEnabled(EOBOverlayElement::Guide,false);
        TestFalse(TEXT("Guide widget can disappear between pages"),Overlay->GetWidgetFromName(TEXT("Guide"))->IsVisible());
        TestTrue(TEXT("Page delay and hidden guide do not flicker the blur"),Blur->IsVisible());
        Overlay->SetPanelEnabled(EOBOverlayElement::Guide,true);
    }
    for (const int32 LastPage:{7,18})
    {
        Director->CurrentDialogueId=0; Director->PrototypePresentedId=LastPage; Director->CurrentStep=EOBTrainingStep::Moving;
        Overlay->SetTrainingDirector(Director);
        TestFalse(TEXT("Returning from either guide block releases blur"),Blur->IsVisible());
    }
    for (const bool Passed:{false,true})
    {
        FOBTrainingResultData Data; Data.bPrototypeFlow=true; Data.bScenarioCompleted=Passed; Data.RecordedFireSeconds=1; Data.RecordedFireLimitSeconds=120;
        Overlay->ShowTrainingResult(Data); TestTrue(TEXT("Pass and fail result screens blur the world"),Blur->IsVisible());
        Overlay->SetStageMode(); TestFalse(TEXT("Closing a result restores a sharp HUD"),Blur->IsVisible());
    }
    Director->bPrototypePaused=true; Overlay->SetTrainingDirector(Director);
    TestTrue(TEXT("Warning result also blurs the world"),Blur->IsVisible());
    Overlay->SetStageMode(); TestFalse(TEXT("Leaving training clears blur ownership"),Blur->IsVisible());
    return !HasAnyErrors();
}
#endif

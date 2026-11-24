#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Widget/OBPlayerOverlayWidget.h"
#include "Widget/OBSystemMessageWidget.h"
#include "Widget/OBHudPanelWidget.h"
#include "Components/TextBlock.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBSystemMessageTest,
    "OperationBreakpoint.UI.SystemMessage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOBSystemMessageTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    UOBPlayerOverlayWidget* Overlay = nullptr;
    ON_SCOPE_EXIT
    {
        if (Overlay) Overlay->ResetSystemMessage();
        World->DestroyWorld(false);
        GEngine->DestroyWorldContext(World);
    };

    UClass* Class = LoadClass<UOBPlayerOverlayWidget>(nullptr,
        TEXT("/Game/Blueprints/Widget/PlayerOverlay/WBP_PlayerOverlay.WBP_PlayerOverlay_C"));
    if (!TestNotNull(TEXT("Saved overlay Blueprint loads"), Class)) return false;
    Overlay = CreateWidget<UOBPlayerOverlayWidget>(World, Class);
    if (!TestNotNull(TEXT("Overlay instantiates"), Overlay)) return false;
    if (!TestNotNull(TEXT("Designer fade-in binds"), Overlay->SystemMessageFadeIn.Get())
        || !TestNotNull(TEXT("Designer fade-out binds"), Overlay->SystemMessageFadeOut.Get())) return false;
    UOBSystemMessageWidget* Child = Cast<UOBSystemMessageWidget>(
        Overlay->WidgetTree->FindWidget(TEXT("SystemMessage")));
    if (!TestNotNull(TEXT("Designer SystemMessage retains the message C++ parent"), Child)) return false;
    Overlay->SetSystemMessage(Child);
    TestTrue(TEXT("Setter keeps the existing Designer instance"), Overlay->SystemMessageWidget == Child);
    TestEqual(TEXT("Registered message starts hidden"), Child->GetVisibility(), ESlateVisibility::Collapsed);

    auto AdvanceTimers = [World](float Seconds)
    {
        ++GFrameCounter;
        World->GetTimerManager().Tick(Seconds);
    };
    AdvanceTimers(0.0f);
    using EPhase = UOBPlayerOverlayWidget::ESystemMessagePhase;
    Overlay->ShowSystemMessage(FText::FromString(TEXT("First message")), 3.0f);
    TestEqual(TEXT("Message reaches child data"), Child->Message.ToString(), FString(TEXT("First message")));
    TestTrue(TEXT("Starts by fading in"), Overlay->SystemMessagePhase == EPhase::FadingIn);
    TestFalse(TEXT("No hold timer before fade-in completes"), World->GetTimerManager().TimerExists(Overlay->SystemMessageHoldTimer));

    // Simulate animation completion callbacks; this test does not render animation frames.
    Overlay->HandleSystemMessageFadeInFinished();
    AdvanceTimers(1.0f);
    TestTrue(TEXT("Holds after fade-in completion"), Overlay->SystemMessagePhase == EPhase::Holding);
    const FName FirstTag = Overlay->SystemMessageAnimationTag;
    Overlay->ShowSystemMessage(FText::FromString(TEXT("Replacement")), 2.0f);
    TestTrue(TEXT("Replacement has a distinct callback tag"), Overlay->SystemMessageAnimationTag != FirstTag);
    AdvanceTimers(5.0f);
    TestTrue(TEXT("Previous timer cannot fade out replacement"), Overlay->SystemMessagePhase == EPhase::FadingIn);
    TestEqual(TEXT("Replacement data is displayed"), Child->Message.ToString(), FString(TEXT("Replacement")));
    Overlay->HandleSystemMessageFadeInFinished();
    AdvanceTimers(1.0f);
    TestTrue(TEXT("Replacement holds for its own duration"), Overlay->SystemMessagePhase == EPhase::Holding);
    AdvanceTimers(1.1f);
    TestTrue(TEXT("Hold expiry starts fade-out"), Overlay->SystemMessagePhase == EPhase::FadingOut);
    Overlay->HandleSystemMessageFadeOutFinished();
    TestTrue(TEXT("Fade-out completion returns to idle"), Overlay->SystemMessagePhase == EPhase::Idle);
    TestEqual(TEXT("Fade-out completion hides child"), Child->GetVisibility(), ESlateVisibility::Collapsed);

    Overlay->ShowPersistentSystemMessage(FText::FromString(TEXT("Persistent")));
    Overlay->HandleSystemMessageFadeInFinished();
    AdvanceTimers(30.0f);
    TestTrue(TEXT("Persistent guide remains until explicit completion"), Overlay->SystemMessagePhase == EPhase::Holding);
    TestFalse(TEXT("Persistent guide has no hide timer"), World->GetTimerManager().TimerExists(Overlay->SystemMessageHoldTimer));
    Overlay->DismissSystemMessage();
    TestTrue(TEXT("Dismissal starts authored fade-out"), Overlay->SystemMessagePhase == EPhase::FadingOut);
    Overlay->HandleSystemMessageFadeOutFinished();
    Overlay->ShowPersistentSystemMessage(FText::FromString(TEXT("Early completion")));
    Overlay->DismissSystemMessage();
    TestTrue(TEXT("Early dismissal waits for fade-in completion"), Overlay->SystemMessagePhase == EPhase::FadingIn);
    Overlay->HandleSystemMessageFadeInFinished();
    TestTrue(TEXT("Pending dismissal fades out after fade-in"), Overlay->SystemMessagePhase == EPhase::FadingOut);
    Overlay->HandleSystemMessageFadeOutFinished();

    Overlay->ShowSystemMessage(FText::FromString(TEXT("Cleanup")), 3.0f);
    Overlay->HandleSystemMessageFadeInFinished();
    Overlay->SetSystemMessage(nullptr);
    TestFalse(TEXT("Clearing reference cancels hold timer"), World->GetTimerManager().TimerExists(Overlay->SystemMessageHoldTimer));
    TestEqual(TEXT("Clearing reference hides the old child"), Child->GetVisibility(), ESlateVisibility::Collapsed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBSubtitleMessageTest,"OperationBreakpoint.UI.SubtitleMessage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOBSubtitleMessageTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    UOBPlayerOverlayWidget* Overlay=nullptr;
    ON_SCOPE_EXIT
    {
        if (Overlay) { Overlay->ResetSubtitle(); Overlay->ResetSystemMessage(); }
        World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    };
    auto* Class=LoadClass<UOBPlayerOverlayWidget>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/WBP_PlayerOverlay.WBP_PlayerOverlay_C"));
    if (!TestNotNull(TEXT("Overlay loads after SystemMessage rename"),Class)) return false;
    Overlay=CreateWidget<UOBPlayerOverlayWidget>(World,Class);
    const auto Slate=Overlay->TakeWidget();
    TestTrue(TEXT("Subtitle is automatically registered"),Overlay->IsSubtitleReady());
    TestTrue(TEXT("SystemMessage is automatically registered"),Overlay->IsSystemMessageReady());
    if (!TestNotNull(TEXT("SubtitleMessageFadeIn binds"),Overlay->SubtitleMessageFadeIn.Get()) ||
        !TestNotNull(TEXT("SubtitleMessageFadeOut binds"),Overlay->SubtitleMessageFadeOut.Get())) return false;
    auto* Child=Cast<UOBSubtitleWidget>(Overlay->WidgetTree->FindWidget(TEXT("Subtitle")));
    if (!TestNotNull(TEXT("Overlay embeds the restored Subtitle UserWidget"),Child)) return false;
    TestEqual(TEXT("The embedded child uses the original WBP_Subtitle asset"),Child->GetClass()->GetPathName(),FString(TEXT("/Game/Blueprints/Widget/PlayerOverlay/Components/WBP_Subtitle.WBP_Subtitle_C")));
    auto* Speaker=Cast<UTextBlock>(Child->GetWidgetFromName(TEXT("Name")));
    auto* Script=Cast<UTextBlock>(Child->GetWidgetFromName(TEXT("Script")));
    if (!TestNotNull(TEXT("Subtitle script exists"),Script) || !TestNotNull(TEXT("Subtitle speaker exists"),Speaker)) return false;
    auto Advance=[&](float Time) { ++GFrameCounter; World->GetTimerManager().Tick(Time); Overlay->RefreshPanelVisibility(); };
    Overlay->ShowSubtitle(FText::FromString(TEXT("교관")),FText::FromString(TEXT("첫 안내")),3);
    TestTrue(TEXT("Timed Subtitle is independently visible"),Child->IsVisible());
    TestEqual(TEXT("Subtitle API updates speaker"),Speaker->GetText().ToString(),FString(TEXT("교관")));
    TestEqual(TEXT("Subtitle API updates script"),Script->GetText().ToString(),FString(TEXT("첫 안내")));
    Advance(1);
    TestFalse(TEXT("Subtitle hold starts only after fade-in"),World->GetTimerManager().TimerExists(Overlay->SubtitleHoldTimer));
    Overlay->ShowSubtitle(FText::FromString(TEXT("교관")),FText::FromString(TEXT("교체 안내")),4);
    Advance(2.1f);
    TestTrue(TEXT("Replaced Subtitle survives the old timer"),Child->IsVisible());
    Overlay->HandleSubtitleFadeInFinished();
    Advance(2.1f);
    Advance(2.1f);
    TestTrue(TEXT("Expiry waits for fade-out before hiding"),Child->IsVisible());
    TestTrue(TEXT("Expiry starts SubtitleMessageFadeOut"),Overlay->SubtitlePhase==UOBPlayerOverlayWidget::ESystemMessagePhase::FadingOut);
    Overlay->HandleSubtitleFadeOutFinished();
    TestFalse(TEXT("Subtitle hides when its fade-out completes"),Child->IsVisible());
    Overlay->ShowPersistentSystemMessage(FText::FromString(TEXT("시스템 안내")));
    Overlay->HandleSystemMessageFadeInFinished();
    Overlay->ShowPersistentSubtitle(FText::FromString(TEXT("교관")),FText::FromString(TEXT("지속 안내")));
    Overlay->HandleSubtitleFadeInFinished();
    Advance(30);
    TestTrue(TEXT("Persistent Subtitle waits for explicit dismissal"),Child->IsVisible());
    TestFalse(TEXT("Persistent Subtitle has no hold timer"),World->GetTimerManager().TimerExists(Overlay->SubtitleHoldTimer));
    Overlay->DismissSubtitle();
    TestTrue(TEXT("Persistent Subtitle stays through fade-out"),Child->IsVisible());
    Overlay->HandleSubtitleFadeOutFinished();
    TestFalse(TEXT("Subtitle dismissal hides only Subtitle"),Child->IsVisible());
    TestTrue(TEXT("Subtitle dismissal does not stop SystemMessage"),Overlay->SystemMessageWidget->IsVisible());
    auto* FadeIn=Overlay->SubtitleMessageFadeIn.Get();
    auto* FadeOut=Overlay->SubtitleMessageFadeOut.Get();
    Overlay->SetCinematicSubtitles(true);
    Overlay->ShowPersistentSubtitle(FText::FromString(TEXT("지휘관")),FText::FromString(TEXT("영상 대사")));
    TestTrue(TEXT("Movies retain the same embedded WBP_Subtitle instance"),Overlay->GetSubtitleWidget()==Child);
    TestTrue(TEXT("Movies retain the authored overlay animations"),Overlay->SubtitleMessageFadeIn==FadeIn && Overlay->SubtitleMessageFadeOut==FadeOut);
    TestTrue(TEXT("Movie captions remain visible"),Child->IsVisible());
    TestFalse(TEXT("Movie mode suppresses SystemMessage"),Overlay->SystemMessageWidget->IsVisible());
    for (const TCHAR* Name : {TEXT("Guide"),TEXT("Mission"),TEXT("Life"),TEXT("Timer"),TEXT("Result")})
        if (auto* Panel=Overlay->WidgetTree->FindWidget(FName(Name))) TestFalse(TEXT("Movie mode suppresses other HUD panels"),Panel->IsVisible());
    Overlay->SetCinematicSubtitles(false);
    TestTrue(TEXT("Movie cleanup restores the existing SystemMessage state"),Overlay->SystemMessageWidget->IsVisible());
    Overlay->ShowSubtitle(FText::FromString(TEXT("교관")),FText::FromString(TEXT("정리")),3);
    Overlay->SetSubtitle(nullptr);
    TestFalse(TEXT("Clearing Subtitle registration cancels its timer"),World->GetTimerManager().TimerExists(Overlay->SubtitleHoldTimer));
    return !HasAnyErrors();
}
#endif

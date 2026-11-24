#include "Widget/OBPlayerOverlayWidget.h"

#include "Animation/WidgetAnimation.h"
#include "Animation/WidgetAnimationHandle.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Widget/OBSystemMessageWidget.h"
#include "Widget/OBHudPanelWidget.h"
#include "Training/OBTrainingDirector.h"
#include "Training/OBTrainingStatusWidget.h"
#include "Training/OBTrainingResultWidget.h"
#include "GameFramework/PlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Core/OBPlayerController.h"
#include "Widget/OBTrainingSkipWidget.h"
#include "Components/TextBlock.h"
#include "Components/BackgroundBlur.h"
#include "Character/OBPlayerCharacter.h"
#include "Stage/OBStageFlowComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogOBPlayerOverlay, Log, All);

void UOBPlayerOverlayWidget::SynchronizeProperties()
{
    Super::SynchronizeProperties();
    if (IsDesignTime()) ApplyDesignerPreview();
}
void UOBPlayerOverlayWidget::NativePreConstruct()
{
    Super::NativePreConstruct();
    if (IsDesignTime()) ApplyDesignerPreview();
}

void UOBPlayerOverlayWidget::ApplyDesignerPreview()
{
    int32 Mask = 1 | 4 | 8 | 16 | 32;
    EOBOverlayPreviewMode Mode=DesignerPreview;
#if WITH_EDITOR
    if (SelectedDesignerPanel==EOBOverlayElement::Result && Mode!=EOBOverlayPreviewMode::ResultFail && Mode!=EOBOverlayPreviewMode::Pause) Mode=EOBOverlayPreviewMode::ResultPass;
    if (SelectedDesignerPanel==EOBOverlayElement::Markers && Mode!=EOBOverlayPreviewMode::MarkersBreach) Mode=EOBOverlayPreviewMode::MarkersAim;
#endif
    switch (Mode)
    {
    case EOBOverlayPreviewMode::Stage: Mask = 16 | 32; break;
    case EOBOverlayPreviewMode::TrainingGuide: Mask = 1 | 4; break;
    case EOBOverlayPreviewMode::TrainingPractice: Mask = 4 | 8 | 16; break;
    case EOBOverlayPreviewMode::SystemMessage: Mask=0; break;
    case EOBOverlayPreviewMode::Pause:
        Mask=128;
        if (TrainingResult) TrainingResult->SetWarning(FOBTrainingResultData());
        break;
    case EOBOverlayPreviewMode::MarkersAim:
    case EOBOverlayPreviewMode::MarkersBreach:
        Mask=64;
        if (TrainingMarkers) TrainingMarkers->DesignerPreview=Mode==EOBOverlayPreviewMode::MarkersBreach ? EOBTrainingMarkerPreview::Breach : EOBTrainingMarkerPreview::Aim;
        break;
    case EOBOverlayPreviewMode::ResultPass:
    case EOBOverlayPreviewMode::ResultFail:
        Mask = 128;
        if (TrainingResult)
        {
            FOBTrainingResultData Sample;
            Sample.bPrototypeFlow = Sample.bFinished = Sample.bScenarioCompleted = true;
            Sample.RecordedFireLimitSeconds = 120;
            Sample.RecordedFireSeconds = Mode == EOBOverlayPreviewMode::ResultPass ? 75 : 121;
            TrainingResult->SetResult(Sample);
        }
        break;
    default: break;
    }
    if (TrainingMarkers) TrainingMarkers->SynchronizeProperties();
    ApplyPanelVisibility(Mask);
    auto* Message=SystemMessage.Get();
    if (!Message && WidgetTree) Message=Cast<UOBSystemMessageWidget>(WidgetTree->FindWidget(TEXT("SystemMessage")));
    if (Message)
    {
        const bool bShow=Mode==EOBOverlayPreviewMode::SystemMessage;
        if (bShow) Message->SetMessage(FText::FromString(TEXT("기초 훈련을 시작합니다. 교관의 명령에 따라 훈련을 진행합니다.")));
        Message->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    }
}
#if WITH_EDITOR
void UOBPlayerOverlayWidget::PreviewSelectedPanel(EOBOverlayElement Panel,bool bSelected)
{
    if (!IsDesignTime()) return;
    if (bSelected) SelectedDesignerPanel=Panel;
    else if (SelectedDesignerPanel==Panel) SelectedDesignerPanel=EOBOverlayElement::None;
    ApplyDesignerPreview();
}
#endif

bool UOBPlayerOverlayWidget::IsSystemMessageReady() const
{
    return IsValid(SystemMessageWidget);
}

void UOBPlayerOverlayWidget::NativeConstruct()
{
    Super::NativeConstruct();
    if (TrainingSkip) TrainingSkip->OnSkipRequested.AddUniqueDynamic(this,&ThisClass::HandleInitialTrainingSkip);
    if (SystemMessage) SetSystemMessage(SystemMessage);
    if (Subtitle) SetSubtitle(Subtitle);
    if (Life) Life->SetHealth(CurrentHealth, MaxHealth);
    RefreshPanelVisibility();
    OnOverlayUpdated();
}

void UOBPlayerOverlayWidget::NativeDestruct()
{
    ResetSystemMessage();
    ResetSubtitle();
    Super::NativeDestruct();
}

void UOBPlayerOverlayWidget::SetSystemMessage(UOBSystemMessageWidget* InSystemMessageWidget)
{
    ResetSystemMessage();
    SystemMessageWidget = InSystemMessageWidget;
    if (IsValid(SystemMessageWidget))
    {
        SystemMessageWidget->SetVisibility(ESlateVisibility::Collapsed);
    }
}

void UOBPlayerOverlayWidget::ShowSystemMessage(const FText& Message, float HoldDuration)
{
    if (!IsValid(SystemMessageWidget) || !GetWorld())
    {
        UE_LOG(LogOBPlayerOverlay, Warning,
            TEXT("ShowSystemMessage: call SetSystemMessage with the Designer's SystemMessage first."));
        return;
    }

    ResetSystemMessage();
    SystemMessageHoldDuration = FMath::IsFinite(HoldDuration) ? FMath::Max(0.0f, HoldDuration) : 0.0f;
    bPersistentSystemMessage = HoldDuration < 0.0f;
    SystemMessageAnimationTag = FName(TEXT("OBSystemMessage"), ++SystemMessageRequestNumber);
    SystemMessageWidget->SetMessage(Message);
    SystemMessageWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
    SystemMessageWidget->SetRenderOpacity(1.0f);
    SystemMessagePhase = ESystemMessagePhase::FadingIn;

    if (SystemMessageFadeIn)
    {
        FWidgetAnimationDynamicEvent Finished;
        Finished.BindDynamic(this, &ThisClass::HandleSystemMessageFadeInFinished);
        BindToAnimationEvent(SystemMessageFadeIn, Finished, EWidgetAnimationEvent::Finished, SystemMessageAnimationTag);
        FWidgetAnimationHandle Handle = PlayAnimation(SystemMessageFadeIn);
        Handle.SetUserTag(SystemMessageAnimationTag);
    }
    else
    {
        HandleSystemMessageFadeInFinished();
    }
}

void UOBPlayerOverlayWidget::HandleSystemMessageFadeInFinished()
{
    if (SystemMessagePhase != ESystemMessagePhase::FadingIn) return;
    SystemMessagePhase = ESystemMessagePhase::Holding;
    const int32 RequestNumber = SystemMessageRequestNumber;
    OnSystemMessageShown.Broadcast();
    if (RequestNumber != SystemMessageRequestNumber || SystemMessagePhase != ESystemMessagePhase::Holding) return;
    if (bDismissSystemMessageRequested)
    {
        BeginSystemMessageFadeOut();
        return;
    }
    if (bPersistentSystemMessage) return;
    if (SystemMessageHoldDuration <= 0.0f)
    {
        BeginSystemMessageFadeOut();
    }
    else if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().SetTimer(SystemMessageHoldTimer, this,
            &ThisClass::BeginSystemMessageFadeOut, SystemMessageHoldDuration, false);
    }
}

void UOBPlayerOverlayWidget::ShowPersistentSystemMessage(const FText& Message)
{
    ShowSystemMessage(Message, -1.0f);
}

void UOBPlayerOverlayWidget::DismissSystemMessage()
{
    if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(SystemMessageHoldTimer);
    bDismissSystemMessageRequested = true;
    if (SystemMessagePhase == ESystemMessagePhase::Holding) BeginSystemMessageFadeOut();
    else if (SystemMessagePhase == ESystemMessagePhase::Idle) OnSystemMessageFinished.Broadcast();
}

void UOBPlayerOverlayWidget::BeginSystemMessageFadeOut()
{
    if (SystemMessagePhase != ESystemMessagePhase::Holding) return;
    SystemMessagePhase = ESystemMessagePhase::FadingOut;
    if (SystemMessageFadeOut)
    {
        FWidgetAnimationDynamicEvent Finished;
        Finished.BindDynamic(this, &ThisClass::HandleSystemMessageFadeOutFinished);
        BindToAnimationEvent(SystemMessageFadeOut, Finished, EWidgetAnimationEvent::Finished, SystemMessageAnimationTag);
        FWidgetAnimationHandle Handle = PlayAnimation(SystemMessageFadeOut);
        Handle.SetUserTag(SystemMessageAnimationTag);
    }
    else
    {
        HandleSystemMessageFadeOutFinished();
    }
}

void UOBPlayerOverlayWidget::HandleSystemMessageFadeOutFinished()
{
    if (SystemMessagePhase != ESystemMessagePhase::FadingOut) return;
    SystemMessagePhase = ESystemMessagePhase::Idle;
    if (IsValid(SystemMessageWidget))
    {
        SystemMessageWidget->SetVisibility(ESlateVisibility::Collapsed);
    }
    OnSystemMessageFinished.Broadcast();
}

void UOBPlayerOverlayWidget::ResetSystemMessage()
{
    // Remove only our callbacks before stopping; a stopped animation can also report completion.
    SystemMessagePhase = ESystemMessagePhase::Idle;
    if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(SystemMessageHoldTimer);
    bPersistentSystemMessage = false;
    bDismissSystemMessageRequested = false;

    if (SystemMessageFadeIn)
    {
        FWidgetAnimationDynamicEvent Finished;
        Finished.BindDynamic(this, &ThisClass::HandleSystemMessageFadeInFinished);
        UnbindFromAnimationFinished(SystemMessageFadeIn, Finished);
        StopAnimation(SystemMessageFadeIn);
    }
    if (SystemMessageFadeOut)
    {
        FWidgetAnimationDynamicEvent Finished;
        Finished.BindDynamic(this, &ThisClass::HandleSystemMessageFadeOutFinished);
        UnbindFromAnimationFinished(SystemMessageFadeOut, Finished);
        StopAnimation(SystemMessageFadeOut);
    }
    if (IsValid(SystemMessageWidget))
    {
        SystemMessageWidget->SetVisibility(ESlateVisibility::Collapsed);
    }
}

bool UOBPlayerOverlayWidget::IsSubtitleReady() const
{
    return IsValid(Cast<UOBSubtitleWidget>(SubtitleWidget));
}
FText UOBPlayerOverlayWidget::GetSubtitleText() const
{
    if (const auto* Panel=Cast<UOBSubtitleWidget>(SubtitleWidget))
        if (const auto* Text=Cast<UTextBlock>(Panel->GetWidgetFromName(TEXT("Script")))) return Text->GetText();
    return FText::GetEmpty();
}
void UOBPlayerOverlayWidget::SetSubtitleDialogue(const FText& Speaker,const FText& Script)
{
    if (auto* Panel=Cast<UOBSubtitleWidget>(SubtitleWidget)) { Panel->SetTrainingDirector(nullptr);Panel->SetDialogue(Speaker,Script); }
}
void UOBPlayerOverlayWidget::SetCinematicSubtitles(bool bEnabled)
{
    if (bCinematicSubtitleMode==bEnabled) return;
    if (bEnabled && WidgetTree && SubtitleWidget)
    {
        TSet<UWidget*> CaptionBranch;
        for (UWidget* Widget=SubtitleWidget;Widget;Widget=Widget->GetParent()) CaptionBranch.Add(Widget);
        UWidgetTree::ForWidgetAndChildren(SubtitleWidget,[&](UWidget* Widget) { CaptionBranch.Add(Widget); });
        WidgetTree->ForEachWidget([&](UWidget* Widget) {
            if (!CaptionBranch.Contains(Widget)) CinematicHiddenWidgets.Add(Widget,Widget->GetVisibility());
        });
    }
    else if (!bEnabled)
    {
        for (const auto& Pair : CinematicHiddenWidgets) if (Pair.Key.IsValid()) Pair.Key->SetVisibility(Pair.Value);
        CinematicHiddenWidgets.Empty();
    }
    bCinematicSubtitleMode=bEnabled;
    SetVisibility(ESlateVisibility::HitTestInvisible);
    RefreshPanelVisibility();
}
void UOBPlayerOverlayWidget::SetSubtitle(UWidget* InSubtitleWidget)
{
    ResetSubtitle();
    SubtitleWidget=InSubtitleWidget;
    if (SubtitleWidget) SubtitleWidget->SetVisibility(ESlateVisibility::Collapsed);
}
void UOBPlayerOverlayWidget::ShowSubtitle(const FText& Speaker,const FText& Script,float HoldDuration)
{
    if (!SubtitleWidget || !GetWorld()) return;
    ResetSubtitle();
    SetSubtitleDialogue(Speaker,Script);
    SubtitleWidget->SetRenderOpacity(1.f);
    bSubtitleMessageActive=bSubtitleVisibilityOwned=true;
    SubtitleHoldDuration=FMath::IsFinite(HoldDuration) ? FMath::Max(0.f,HoldDuration) : 0.f;
    bPersistentSubtitle=HoldDuration<0;
    SubtitleAnimationTag=FName(TEXT("OBSubtitle"),SubtitleRequestNumber);
    SubtitlePhase=ESystemMessagePhase::FadingIn;
    RefreshPanelVisibility();
    if (SubtitleMessageFadeIn)
    {
        FWidgetAnimationDynamicEvent Finished;
        Finished.BindDynamic(this,&ThisClass::HandleSubtitleFadeInFinished);
        BindToAnimationEvent(SubtitleMessageFadeIn,Finished,EWidgetAnimationEvent::Finished,SubtitleAnimationTag);
        auto Handle=PlayAnimation(SubtitleMessageFadeIn);
        Handle.SetUserTag(SubtitleAnimationTag);
    }
    else HandleSubtitleFadeInFinished();
}
void UOBPlayerOverlayWidget::HandleSubtitleFadeInFinished()
{
    if (SubtitlePhase!=ESystemMessagePhase::FadingIn) return;
    SubtitlePhase=ESystemMessagePhase::Holding;
    const int32 Request=SubtitleRequestNumber;
    OnSubtitleShown.Broadcast();
    if (Request!=SubtitleRequestNumber || SubtitlePhase!=ESystemMessagePhase::Holding) return;
    if (bDismissSubtitleRequested) { BeginSubtitleFadeOut(); return; }
    if (bPersistentSubtitle) return;
    if (SubtitleHoldDuration<=0) BeginSubtitleFadeOut();
    else GetWorld()->GetTimerManager().SetTimer(SubtitleHoldTimer,this,&ThisClass::BeginSubtitleFadeOut,SubtitleHoldDuration,false);
}
void UOBPlayerOverlayWidget::ShowPersistentSubtitle(const FText& Speaker,const FText& Script)
{
    ShowSubtitle(Speaker,Script,-1.f);
}
void UOBPlayerOverlayWidget::DismissSubtitle()
{
    if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(SubtitleHoldTimer);
    bDismissSubtitleRequested=true;
    if (SubtitlePhase==ESystemMessagePhase::Holding) BeginSubtitleFadeOut();
}
void UOBPlayerOverlayWidget::BeginSubtitleFadeOut()
{
    if (SubtitlePhase!=ESystemMessagePhase::Holding) return;
    SubtitlePhase=ESystemMessagePhase::FadingOut;
    if (SubtitleMessageFadeOut)
    {
        FWidgetAnimationDynamicEvent Finished;
        Finished.BindDynamic(this,&ThisClass::HandleSubtitleFadeOutFinished);
        BindToAnimationEvent(SubtitleMessageFadeOut,Finished,EWidgetAnimationEvent::Finished,SubtitleAnimationTag);
        auto Handle=PlayAnimation(SubtitleMessageFadeOut);
        Handle.SetUserTag(SubtitleAnimationTag);
    }
    else HandleSubtitleFadeOutFinished();
}
void UOBPlayerOverlayWidget::HandleSubtitleFadeOutFinished()
{
    if (SubtitlePhase!=ESystemMessagePhase::FadingOut) return;
    SubtitlePhase=ESystemMessagePhase::Idle;
    bSubtitleMessageActive=false;
    if (SubtitleWidget) SubtitleWidget->SetVisibility(ESlateVisibility::Collapsed);
    RefreshPanelVisibility();
    OnSubtitleFinished.Broadcast();
}
void UOBPlayerOverlayWidget::ResetSubtitle()
{
    ++SubtitleRequestNumber;
    SubtitlePhase=ESystemMessagePhase::Idle;
    bPersistentSubtitle=bDismissSubtitleRequested=false;
    FWidgetAnimationDynamicEvent Finished;
    Finished.BindDynamic(this,&ThisClass::HandleSubtitleFadeInFinished);
    if (SubtitleMessageFadeIn) { UnbindFromAnimationFinished(SubtitleMessageFadeIn,Finished); StopAnimation(SubtitleMessageFadeIn); }
    Finished.BindDynamic(this,&ThisClass::HandleSubtitleFadeOutFinished);
    if (SubtitleMessageFadeOut) { UnbindFromAnimationFinished(SubtitleMessageFadeOut,Finished); StopAnimation(SubtitleMessageFadeOut); }
    if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(SubtitleHoldTimer);
    bSubtitleMessageActive=bSubtitleVisibilityOwned=false;
    if (SubtitleWidget)
    {
        SubtitleWidget->SetVisibility(ESlateVisibility::Collapsed);
        if (auto* Panel=Cast<UOBHudPanelWidget>(SubtitleWidget)) Panel->SetTrainingDirector(bTrainingDialogueAnimationControl ? nullptr : TrainingDirector.Get());
    }
}

void UOBPlayerOverlayWidget::SetTrainingDialogueAnimationControl(bool bEnabled)
{
    bTrainingDialogueAnimationControl=bEnabled;
    CancelTrainingDialogue();
}
void UOBPlayerOverlayWidget::CancelTrainingDialogue()
{
    ResetSystemMessage();
    ResetSubtitle();
    // Suppress the polling path between lines and throughout authored fades.
    bSubtitleVisibilityOwned=bTrainingDialogueAnimationControl;
}

void UOBPlayerOverlayWidget::SetHealth(float InCurrentHealth, float InMaxHealth)
{
    MaxHealth = FMath::Max(0.0f, InMaxHealth);
    CurrentHealth = FMath::Clamp(InCurrentHealth, 0.0f, MaxHealth);
    if (Life) Life->SetHealth(CurrentHealth, MaxHealth);
    OnOverlayUpdated();
}

void UOBPlayerOverlayWidget::SetAmmo(int32 InCurrentAmmo, int32 InMaxAmmo)
{
    MaxAmmo = FMath::Max(0, InMaxAmmo);
    CurrentAmmo = FMath::Clamp(InCurrentAmmo, 0, MaxAmmo);
    OnOverlayUpdated();
}

void UOBPlayerOverlayWidget::SetScore(int32 InScore)
{
    Score = InScore;
    OnOverlayUpdated();
}

void UOBPlayerOverlayWidget::SetTrainingDirector(AOBTrainingDirector* Director)
{
    SetTrainingDialogueAnimationControl(false);
    TrainingDirector = Director; bTrainingResultShown = false;
    bPauseInteractionActive=false;
    for (auto* Panel : {Guide.Get(), Mission.Get(), Timer.Get()})
        if (Panel) Panel->SetTrainingDirector(Director);
    if (auto* Panel=Cast<UOBHudPanelWidget>(Subtitle.Get())) Panel->SetTrainingDirector(Director);
    if (TrainingMarkers) TrainingMarkers->SetDirector(Director);
    if (TrainingResult) TrainingResult->SetTrainingDirector(Director);
    RefreshPanelVisibility();
}
void UOBPlayerOverlayWidget::SetStageMode()
{
    SetTrainingDirector(nullptr); bWasTrainingPaused = false;
}
void UOBPlayerOverlayWidget::RefreshTrainingGuide()
{
    if (auto* GuideWidget=Cast<UOBGuideWidget>(Guide))
    {
        const auto* Director=TrainingDirector.Get();
        GuideWidget->SetGuidePage(Director ? Director->GetGuideUIId() : 0,Director && Director->bGuideAdvanceReady,false);
    }
}
UOBGuideWidget* UOBPlayerOverlayWidget::GetTrainingGuideWidget() const { return Cast<UOBGuideWidget>(Guide); }
bool UOBPlayerOverlayWidget::IsInitialTrainingSkipVisible() const
{
    return !QAUIPreviewId && TrainingDirector.IsValid() && TrainingDirector->IsInitialSkipAvailable() && TrainingSkip && IsVisible();
}
void UOBPlayerOverlayWidget::RefreshInitialTrainingSkip()
{
    if (!TrainingSkip) return;
    const auto* PC=Cast<AOBPlayerController>(GetOwningPlayer());
    TrainingSkip->SetAvailable(IsInitialTrainingSkipVisible(),PC && PC->IsTriggerHeld());
}
bool UOBPlayerOverlayWidget::HandleInitialSkipTrigger(bool bPressed)
{
    RefreshInitialTrainingSkip();
    return TrainingSkip && TrainingSkip->HandleTrigger(bPressed);
}
void UOBPlayerOverlayWidget::HandleInitialTrainingSkip()
{
    if (auto* Director=TrainingDirector.Get()) Director->RequestInitialSkip();
}
bool UOBPlayerOverlayWidget::SetQAUIPreview(int32 UIId)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    if (UIId<3001 || UIId>3010 || !Guide || !TrainingResult) return false;
    QAUIPreviewId=UIId;
    if (UIId<=3007)
    {
        auto* Page=Cast<UOBGuideWidget>(Guide);
        if (!Page) { QAUIPreviewId=0; return false; }
        Page->SetGuidePage(UIId,true);
    }
    else
    {
        FOBTrainingResultData Sample;
        Sample.bPrototypeFlow=true; Sample.bScenarioCompleted=UIId==3009;
        Sample.RecordedFireLimitSeconds=120; Sample.RecordedFireSeconds=75;
        TrainingResult->bAutomaticNavigation=false;
        if (UIId==3008) TrainingResult->SetWarning(Sample);
        else TrainingResult->SetResult(Sample);
    }
    RefreshPanelVisibility();
    return true;
#else
    return false;
#endif
}
void UOBPlayerOverlayWidget::SetPanelEnabled(EOBOverlayElement Panel, bool bEnabled)
{
    int32& Allowed = TrainingDirector.IsValid() ? TrainingElements : StageElements;
    if (bEnabled) Allowed |= static_cast<int32>(Panel); else Allowed &= ~static_cast<int32>(Panel);
    RefreshPanelVisibility();
}
void UOBPlayerOverlayWidget::SetSystemMessageEnabled(bool bEnabled)
{
    bSystemMessageEnabled=bEnabled;
    RefreshPanelVisibility();
}
void UOBPlayerOverlayWidget::SetStageTime(float Seconds, bool bStartCountdown)
{
    if (Timer) { if (bStartCountdown) Timer->StartCountdown(Seconds); else Timer->SetRemainingSeconds(Seconds); }
}
void UOBPlayerOverlayWidget::SetZoneProgress(int32 Current, int32 Total) { if (Timer) Timer->SetZoneProgress(Current, Total); }
bool UOBPlayerOverlayWidget::IsTrainingResultVisible() const { return bTrainingResultShown && TrainingResult && TrainingResult->IsVisible() && IsVisible(); }
bool UOBPlayerOverlayWidget::IsTrainingPauseVisible() const
{
    return TrainingDirector.IsValid() && TrainingDirector->bPrototypePaused && !bTrainingResultShown && IsVisible()
        && TrainingResult && TrainingResult->IsVisible() && TrainingResult->IsWarningInteraction();
}
void UOBPlayerOverlayWidget::HandleTrainingTrigger(bool bPressed)
{
    if (IsTrainingResultVisible()) HandleResultTrigger(bPressed);
    else if (IsTrainingPauseVisible()) TrainingResult->HandleTrigger(bPressed);
}
void UOBPlayerOverlayWidget::HandleResultTrigger(bool bPressed) { if (IsTrainingResultVisible()) TrainingResult->HandleTrigger(bPressed); }
void UOBPlayerOverlayWidget::CancelResultTrigger() { if (TrainingResult) TrainingResult->CancelTrigger(); }
void UOBPlayerOverlayWidget::ShowTrainingResult(const FOBTrainingResultData& Result)
{
    if (!TrainingResult) return;
    bTrainingResultShown = true; TrainingResult->SetResult(Result);
    DismissSubtitle();
    DismissSystemMessage(); RefreshPanelVisibility();
    if (auto* PC = GetOwningPlayer())
    {
        PC->SetPause(false);
        FInputModeGameOnly Mode;Mode.SetConsumeCaptureMouseDown(false);
        PC->SetInputMode(Mode);PC->bShowMouseCursor=false;
    }
}
void UOBPlayerOverlayWidget::NativeTick(const FGeometry& Geometry, float DeltaSeconds)
{
    Super::NativeTick(Geometry, DeltaSeconds);
    if (IsDesignTime()) return;
    RefreshPanelVisibility();
    if (QAUIPreviewId) return;
    const bool bPaused = TrainingDirector.IsValid() && TrainingDirector->bPrototypePaused && !bTrainingResultShown;
    if (bPaused != bWasTrainingPaused)
    {
        bWasTrainingPaused = bPaused;
        if (auto* PC = GetOwningPlayer())
        {
            if (bPaused) { PC->SetInputMode(FInputModeGameOnly()); PC->bShowMouseCursor=false; }
            else if (!bTrainingResultShown)
            {
                FInputModeGameAndUI Mode; Mode.SetHideCursorDuringCapture(false); Mode.SetLockMouseToViewportBehavior(EMouseLockMode::LockAlways);
                PC->SetInputMode(Mode); PC->bShowMouseCursor=true;
            }
        }
    }
}
void UOBPlayerOverlayWidget::RefreshPanelVisibility()
{
    if (IsDesignTime()) { ApplyDesignerPreview(); return; }
    const auto* Player=GetOwningPlayer() ? Cast<AOBPlayerCharacter>(GetOwningPlayer()->GetPawn()) : nullptr;
    const auto* Stage=Player ? Player->GetStageFlowComponent() : nullptr;
    const bool bStageBreach=!TrainingDirector.IsValid() && !bTrainingResultShown && Stage && Stage->IsWaitingForBreach() && !Stage->HasRequestedBreach();
    if (Stage && Stage->GetPhase()==EOBStagePhase::Zone6Breach && Stage->HasRequestedBreach())
    {
        // UMG is drawn after the camera fade. Keep its captions/markers out of the full-black breach frame.
        ApplyPanelVisibility(0);
        if (SystemMessage) SystemMessage->SetVisibility(ESlateVisibility::Collapsed);
        if (TrainingSkip) TrainingSkip->SetVisibility(ESlateVisibility::Collapsed);
        return;
    }
    if (bCinematicSubtitleMode)
    {
        ApplyPanelVisibility(bSubtitleMessageActive ? static_cast<int32>(EOBOverlayElement::Subtitle) : 0);
        for (const auto& Pair : CinematicHiddenWidgets) if (Pair.Key.IsValid()) Pair.Key->SetVisibility(ESlateVisibility::Collapsed);
        if (SystemMessage) SystemMessage->SetVisibility(ESlateVisibility::Collapsed);
        if (TrainingSkip) TrainingSkip->SetVisibility(ESlateVisibility::Collapsed);
        return;
    }
    if (!bTrainingDialogueAnimationControl && !bSubtitleMessageActive)
        if (const auto* Training=TrainingDirector.Get(); Training && Subtitle && !Subtitle->IsA<UOBHudPanelWidget>())
            SetSubtitleDialogue(Training->GetSubtitleSpeaker(),Training->GetSubtitleText());
    RefreshInitialTrainingSkip();
    if (QAUIPreviewId)
    {
        ApplyPanelVisibility(static_cast<int32>(QAUIPreviewId<=3007 ? EOBOverlayElement::Guide : EOBOverlayElement::Result));
        if (SystemMessage) SystemMessage->SetVisibility(ESlateVisibility::Collapsed);
        return;
    }
    int32 Mask = StageElements;
    if (bStageBreach) Mask|=static_cast<int32>(EOBOverlayElement::Markers);
    if (bTrainingResultShown) Mask = static_cast<int32>(EOBOverlayElement::Result);
    else if (const auto* Training = TrainingDirector.Get())
    {
        Mask = 0;
        if (!Training->GetTrainingResult().bFinished)
        {
            if (Training->bPrototypePaused)
            {
                if (TrainingResult)
                {
                    if (!bPauseInteractionActive) TrainingResult->SetWarning(Training->GetTrainingResult());
                    Mask |= static_cast<int32>(EOBOverlayElement::Result);
                }
            }
            else
            {
                Mask |= static_cast<int32>(EOBOverlayElement::Life);
                if (Training->IsPrototypeGuideVisible()) Mask |= static_cast<int32>(EOBOverlayElement::Guide);
                if (!Training->GetSubtitleText().IsEmpty()) Mask |= static_cast<int32>(EOBOverlayElement::Subtitle);
                const bool bPractice = Training->CurrentStep != EOBTrainingStep::Welcome && Training->CurrentStep != EOBTrainingStep::Intro &&
                    Training->CurrentStep != EOBTrainingStep::Moving && Training->CurrentStep != EOBTrainingStep::MovingToB && Training->CurrentStep != EOBTrainingStep::MovingToC;
                if (bPractice) Mask |= static_cast<int32>(EOBOverlayElement::Mission);
                if (Training->GetRemainingSeconds()>0 && !(Training->bUseSplinePrototype && Training->GetPrototypeRouteIndex()==1))
                    Mask |= static_cast<int32>(EOBOverlayElement::Timer);
                if (Training->CurrentStep == EOBTrainingStep::AimHold || Training->CurrentStep == EOBTrainingStep::Breach || (TrainingMarkers && TrainingMarkers->HasAimCompletionFeedback())) Mask |= static_cast<int32>(EOBOverlayElement::Markers);
            }
        }
        Mask &= TrainingElements;
        if (Training->bPrototypePaused && TrainingResult && !Training->GetTrainingResult().bFinished)
            Mask |= static_cast<int32>(EOBOverlayElement::Result);
    }
    const bool bPauseActive=((Mask & static_cast<int32>(EOBOverlayElement::Pause))!=0)
        || (TrainingDirector.IsValid() && TrainingDirector->bPrototypePaused && !bTrainingResultShown);
    if (bSubtitleVisibilityOwned)
    {
        Mask &= ~static_cast<int32>(EOBOverlayElement::Subtitle);
        if (bSubtitleMessageActive && !bPauseActive && !bTrainingResultShown) Mask |= static_cast<int32>(EOBOverlayElement::Subtitle);
    }
    bPauseInteractionActive=bPauseActive;
    ApplyPanelVisibility(Mask);
    RefreshSystemMessage();
}
void UOBPlayerOverlayWidget::RefreshSystemMessage()
{
    if (SystemMessage && (bTrainingResultShown || (TrainingDirector.IsValid() && TrainingDirector->bPrototypePaused && TrainingResult)))
    {
        if (!SystemMessage->Message.IsEmpty()) SystemMessage->SetMessage(FText::GetEmpty());
        SystemMessage->SetVisibility(ESlateVisibility::Collapsed);
        return;
    }
    if (bTrainingDialogueAnimationControl) return;
    if (!SystemMessage) return;
    const auto* Training=TrainingDirector.Get();
    const bool bManaged=Training && Training->bUseSplinePrototype;
    if (bManaged || bSystemMessageManaged)
    {
        const FText Text=bManaged && bSystemMessageEnabled ? Training->GetSystemMessageText() : FText::GetEmpty();
        if (!SystemMessage->Message.EqualTo(Text)) SystemMessage->SetMessage(Text);
        SystemMessage->SetVisibility(Text.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
        // Gameplay owns the duration of these lines; legacy message fades must not hide them.
        if (!Text.IsEmpty()) SystemMessage->SetRenderOpacity(1.f);
    }
    bSystemMessageManaged=bManaged;
}
void UOBPlayerOverlayWidget::ApplyPanelVisibility(int32 Mask)
{
    VisibleElements = Mask;
    auto Show = [&](UWidget* Panel, EOBOverlayElement Element) {
        if (Panel) Panel->SetVisibility((Mask & static_cast<int32>(Element)) ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    };
    Show(Guide, EOBOverlayElement::Guide); Show(Subtitle, EOBOverlayElement::Subtitle); Show(Mission, EOBOverlayElement::Mission);
    Show(Timer, EOBOverlayElement::Timer); Show(Life, EOBOverlayElement::Life); Show(TrainingMarkers, EOBOverlayElement::Markers); Show(TrainingResult, EOBOverlayElement::Result);
    if (TrainingBackgroundBlur)
    {
        const auto* Training=TrainingDirector.Get();
        const bool bGuideSection=Training && Training->bUseSplinePrototype && !IsDesignTime() && !QAUIPreviewId
            ? Training->IsGuideSectionActive() : (Mask & static_cast<int32>(EOBOverlayElement::Guide))!=0;
        const bool bBlur=!bCinematicSubtitleMode && (bGuideSection || (Mask & static_cast<int32>(EOBOverlayElement::Result))!=0);
        TrainingBackgroundBlur->SetBlurStrength(TrainingBlurStrength);
        TrainingBackgroundBlur->SetVisibility(bBlur && TrainingBlurStrength>0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    }
    SetVisibility(ESlateVisibility::HitTestInvisible);
}

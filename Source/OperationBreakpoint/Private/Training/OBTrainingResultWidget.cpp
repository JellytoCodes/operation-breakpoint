#include "Training/OBTrainingResultWidget.h"
#include "Training/OBTrainingUIData.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/ScaleBox.h"
#include "Engine/Texture2D.h"
#include "Kismet/GameplayStatics.h"
#include "Core/OBPlayerController.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Widget/OBPlayerOverlayWidget.h"
#include "Training/OBTrainingDirector.h"
#if WITH_EDITOR
void UOBTrainingResultWidget::OnSelectedByDesigner()
{
    Super::OnSelectedByDesigner();
    if (auto* Tree=GetTypedOuter<UWidgetTree>())
        if (auto* Overlay=Cast<UOBPlayerOverlayWidget>(Tree->GetOuter())) Overlay->PreviewSelectedPanel(EOBOverlayElement::Result,true);
}
void UOBTrainingResultWidget::OnDeselectedByDesigner()
{
    Super::OnDeselectedByDesigner();
    if (auto* Tree=GetTypedOuter<UWidgetTree>())
        if (auto* Overlay=Cast<UOBPlayerOverlayWidget>(Tree->GetOuter())) Overlay->PreviewSelectedPanel(EOBOverlayElement::Result,false);
}
#endif

void FOBTrainingResultTrigger::Reset(bool bAlreadyHeld)
{
    HeldSeconds = 0; bHolding = bConsumed = false; bWaitingForRelease = bAlreadyHeld;
}
void FOBTrainingResultTrigger::Cancel() { bHolding = false; HeldSeconds = 0; bWaitingForRelease = true; }
EOBTrainingResultAction FOBTrainingResultTrigger::Change(bool bPressed, float LongPressSeconds)
{
    if (bConsumed) return EOBTrainingResultAction::None;
    if (bWaitingForRelease) { if (!bPressed) bWaitingForRelease = false; return EOBTrainingResultAction::None; }
    if (bPressed)
    {
        if (!bHolding) { bHolding = true; HeldSeconds = 0; }
        return EOBTrainingResultAction::None;
    }
    if (!bHolding) return EOBTrainingResultAction::None;
    bHolding = false; bConsumed = true;
    return HeldSeconds >= FMath::Max(.25f, LongPressSeconds) ? EOBTrainingResultAction::Retry : EOBTrainingResultAction::NextStage;
}
EOBTrainingResultAction FOBTrainingResultTrigger::Advance(float DeltaSeconds, float LongPressSeconds)
{
    if (!bHolding || bConsumed || !FMath::IsFinite(DeltaSeconds)) return EOBTrainingResultAction::None;
    HeldSeconds += FMath::Max(0.f, DeltaSeconds);
    if (HeldSeconds < FMath::Max(.25f, LongPressSeconds)) return EOBTrainingResultAction::None;
    bHolding = false; bConsumed = true; return EOBTrainingResultAction::Retry;
}
void UOBTrainingResultWidget::BuildDefaultLayout()
{
    if (!WidgetTree) WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transient);
    if (WidgetTree->RootWidget) return;
    auto* Backdrop = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ResultBackdrop"));
    Backdrop->SetPadding(FMargin(0)); Backdrop->SetBrushColor(FLinearColor(0,0,0,.65f));
    auto* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("ResultCanvas"));
    Backdrop->SetContent(Canvas); WidgetTree->RootWidget = Backdrop;
    auto Text = [&](const TCHAR* Name, int32 Size) {
        auto* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
        Label->SetColorAndOpacity(FLinearColor::White); Label->SetFont(FSlateFontInfo(Label->GetFont().FontObject, Size));
        Label->SetShadowColorAndOpacity(FLinearColor::Black); Label->SetShadowOffset(FVector2D(2,2)); return Label;
    };
    TitleText = Text(TEXT("TitleText"), 56); TitleText->SetText(FText::FromString(TEXT("훈련 종료"))); TitleText->SetJustification(ETextJustify::Center);
    auto* TitleSlot = Canvas->AddChildToCanvas(TitleText);
    TitleSlot->SetAnchors(FAnchors(.5f,.38f)); TitleSlot->SetAlignment(FVector2D(.5,.5)); TitleSlot->SetSize(FVector2D(650,100));
    StampImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("StampImage"));
    auto* StampSlot = Canvas->AddChildToCanvas(StampImage);
    StampSlot->SetAnchors(FAnchors(.5f,.57f)); StampSlot->SetAlignment(FVector2D(.5,.5)); StampSlot->SetSize(FVector2D(256,128));
    InteractionText = Text(TEXT("InteractionText"), 22); InteractionText->SetAutoWrapText(false); InteractionText->SetJustification(ETextJustify::Right);
    InteractionText->SetText(FText::FromString(TEXT("방아쇠 한 번: 다음 스테이지\n방아쇠 길게 (2초): 다시 훈련")));
    auto* PromptSlot = Canvas->AddChildToCanvas(InteractionText);
    PromptSlot->SetAnchors(FAnchors(1,1)); PromptSlot->SetAlignment(FVector2D(1,1)); PromptSlot->SetPosition(FVector2D(-48,-36)); PromptSlot->SetSize(FVector2D(500,100));
}
void UOBTrainingResultWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized(); BuildDefaultLayout(); Refresh();
}
void UOBTrainingResultWidget::SynchronizeProperties()
{
    Super::SynchronizeProperties();
    Refresh();
}
bool UOBTrainingResultWidget::IsScenarioPassed() const
{
    return bStageCompletion ? bStagePassed : Result.IsScenarioPassed();
}
void UOBTrainingResultWidget::SetTrainingDirector(AOBTrainingDirector* InDirector) { Director=InDirector; }
void UOBTrainingResultWidget::SetResult(const FOBTrainingResultData& Data)
{
    bStageCompletion=false;
    bWarningInteraction=false; LastWarningAction=EOBTrainingWarningAction::None;
    Result = Data; LastAction = EOBTrainingResultAction::None;
    CurrentUIId=Data.bAborted ? 3008 : IsScenarioPassed() ? 3009 : 3010;
    const auto* PC = Cast<AOBPlayerController>(GetOwningPlayer());
    Trigger.Reset(PC && PC->IsTriggerHeld()); Refresh();
}
void UOBTrainingResultWidget::SetStageCompletion(bool bPassed,bool bReportSaved)
{
    bStageCompletion=true; bStagePassed=bPassed; bWarningInteraction=false;
    bStageReportSaved=bReportSaved;
    CurrentUIId=0; LastAction=EOBTrainingResultAction::None;
    const auto* PC=Cast<AOBPlayerController>(GetOwningPlayer()); Trigger.Reset(PC && PC->IsTriggerHeld());
    if (TitleText) TitleText->SetText(FText::FromString(TEXT("훈련 종료")));
    if (BodyText) BodyText->SetText(FText::GetEmpty());
    Refresh();
}
void UOBTrainingResultWidget::SetWarning(const FOBTrainingResultData& Data)
{
    bStageCompletion=false;
    bWarningInteraction=true; LastWarningAction=EOBTrainingWarningAction::None;
    Result=Data; CurrentUIId=3008;
    const auto* PC=Cast<AOBPlayerController>(GetOwningPlayer());
    PrepareWarningInteraction(PC && PC->IsTriggerHeld());
}
void UOBTrainingResultWidget::PrepareWarningInteraction(bool bAlreadyHeld)
{
    if (!bWarningInteraction) return;
    Trigger.Reset(bAlreadyHeld); LastAction=EOBTrainingResultAction::None;
    LastWarningAction=EOBTrainingWarningAction::None; Refresh();
}
void UOBTrainingResultWidget::Refresh()
{
    if (ResultBorder) ResultBorder->SetVisibility(bStageCompletion ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
    if (ResultActions) ResultActions->SetVisibility(bStageCompletion ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
    if (StageResultPanel) StageResultPanel->SetVisibility(bStageCompletion ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    if (StageResultPanel)
        if (auto* FitSlot=Cast<UCanvasPanelSlot>(StageResultPanel->Slot))
        {
            FitSlot->SetAnchors(FAnchors(0,0,1,1));FitSlot->SetAlignment(FVector2D::ZeroVector);FitSlot->SetOffsets(FMargin(24));
        }
    const float HoldPercent=(Trigger.bHolding || (Trigger.bConsumed && Trigger.HeldSeconds>=FMath::Max(.25f,LongPressSeconds)))
        ? FMath::Clamp(Trigger.HeldSeconds/FMath::Max(.25f,LongPressSeconds),0.f,1.f) : 0.f;
    if (RetryHoldProgress) RetryHoldProgress->SetPercent(bStageCompletion ? 0.f : HoldPercent);
    if (StageMenuHoldProgress) StageMenuHoldProgress->SetPercent(bStageCompletion ? HoldPercent : 0.f);
    if (StageAccuracyText) StageAccuracyText->SetText(FText::FromString(Result.ShotsFired>0
        ? FString::Printf(TEXT("%.1f %%"),100.f*FMath::Clamp(Result.ValidHits,0,Result.ShotsFired)/Result.ShotsFired) : TEXT("—")));
    if (StageScoreText) StageScoreText->SetText(FText::FromString(Result.bHasStageScore ? FString::Printf(TEXT("%.2f / 100"),Result.StageScore) : TEXT("— / 100")));
    if (StageRankText) StageRankText->SetText(FText::FromString(Result.bHasStageScore ? Result.StageRank : TEXT("—")));
    if (StageSubtitleText) StageSubtitleText->SetText(FText::FromString(bStageCompletion ? (bStagePassed ? TEXT("인질 구출 성공") : TEXT("인질 구출 실패")) : TEXT("훈련 결과")));
    const int32 Seconds=FMath::IsFinite(Result.TotalSeconds) ? FMath::Max(0,FMath::FloorToInt(Result.TotalSeconds)) : 0;
    if (StageTimeText) StageTimeText->SetText(FText::FromString(FString::Printf(TEXT("%02d:%02d"),Seconds/60,Seconds%60)));
    if (StageReportText) StageReportText->SetText(FText::FromString(bStageReportSaved ? TEXT("훈련 상세 내역은\n자동 저장됩니다.") : TEXT("훈련 상세 내역을\n저장하지 못했습니다.")));
    if (StageMenuHint) StageMenuHint->SetText(FText::FromString(Trigger.bWaitingForRelease ? TEXT("방아쇠를 놓아주세요") : Trigger.bHolding
        ? FString::Printf(TEXT("길게 당기기  %.1f / %.0f초"),FMath::Min(Trigger.HeldSeconds,LongPressSeconds),LongPressSeconds)
        : FString::Printf(TEXT("방아쇠 %.0f초 길게 당기기"),LongPressSeconds)));
    if (!LoadedUIData) LoadedUIData=UIDataTable.LoadSynchronous();
    const auto* Row=LoadedUIData && LoadedUIData->GetRowStruct()==FOBTrainingUIRow::StaticStruct()
        ? LoadedUIData->FindRow<FOBTrainingUIRow>(FName(*FString::FromInt(CurrentUIId)),TEXT("Training result UI"),false) : nullptr;
    if (Row)
    {
        if (TitleText) TitleText->SetText(Row->Title);
        if (BodyText) BodyText->SetText(Row->Body);
        if (ResultBorder)
        {
            ResultBorder->SetBrushFromTexture(Row->Image.LoadSynchronous());
            ResultBorder->SetBrushColor(FLinearColor::White);
        }
    }
    if (bStageCompletion)
    {
        if (TitleText) TitleText->SetText(FText::FromString(TEXT("훈련 종료")));
        if (BodyText) BodyText->SetText(FText::GetEmpty());
    }
    if (StampImage)
    {
        StampImage->SetVisibility(bStageCompletion || CurrentUIId==3008 ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
        if (!bStageCompletion && CurrentUIId!=3008) StampImage->SetBrushFromTexture(IsScenarioPassed() ? PassStamp.LoadSynchronous() : FailStamp.LoadSynchronous(), false);
    }
    if (InteractionText)
    {
        const TCHAR* ShortAction=bWarningInteraction ? TEXT("속행") : bStageCompletion ? TEXT("재훈련") : TEXT("다음 스테이지");
        const TCHAR* LongAction=bWarningInteraction || bStageCompletion ? TEXT("메인화면") : TEXT("다시 훈련");
        const FString Hint = Trigger.bWaitingForRelease ? TEXT("방아쇠를 놓은 뒤 다시 당겨주세요.") : Trigger.bHolding ?
            FString::Printf(TEXT("놓으면 %s\n%s: %.1f / %.1f초"),ShortAction,LongAction,Trigger.HeldSeconds,LongPressSeconds) :
            FString::Printf(TEXT("방아쇠 한 번: %s\n방아쇠 길게 (%.0f초): %s"),ShortAction,LongPressSeconds,LongAction);
        InteractionText->SetText(FText::FromString(Hint));
    }
    if (NextActionText)
    {
        NextActionText->SetText(FText::FromString(bWarningInteraction ? TEXT("속행") : bStageCompletion ? TEXT("재훈련") : TEXT("미션 이동")));
        if (RetryActionText) RetryActionText->SetText(FText::FromString(bWarningInteraction || bStageCompletion ? TEXT("메인 메뉴") : TEXT("재훈련")));
        if (NextActionHint) NextActionHint->SetText(FText::FromString(TEXT("방아쇠 한 번 당기기")));
        if (RetryActionHint) RetryActionHint->SetText(FText::FromString(Trigger.bHolding ?
            FString::Printf(TEXT("길게 당기기  %.1f / %.0f초"),Trigger.HeldSeconds,LongPressSeconds) :
            FString::Printf(TEXT("방아쇠 %.0f초 길게 당기기"),LongPressSeconds)));
        if (InteractionText && !Trigger.bHolding && !Trigger.bWaitingForRelease)
            InteractionText->SetText(FText::FromString(bWarningInteraction ? TEXT("방아쇠로 속행 또는 메인 메뉴 이동을 선택하세요.") : bStageCompletion ?
                TEXT("짧게 당기면 재훈련, 2초간 당기면 메인화면으로 이동합니다.") : TEXT("방아쇠로 미션 이동 또는 재훈련을 선택하세요.")));
    }
}
void UOBTrainingResultWidget::HandleTrigger(bool bPressed) { const auto Action=Trigger.Change(bPressed,LongPressSeconds); Refresh(); Select(Action); }
void UOBTrainingResultWidget::CancelTrigger() { Trigger.Cancel(); Refresh(); }
void UOBTrainingResultWidget::AdvanceInteraction(float DeltaSeconds)
{
    const bool bWasHolding=Trigger.bHolding;
    const auto Action=Trigger.Advance(DeltaSeconds,LongPressSeconds);
    if (bWasHolding) Refresh();
    Select(Action);
}
void UOBTrainingResultWidget::NativeTick(const FGeometry& Geometry, float DeltaSeconds)
{
    Super::NativeTick(Geometry, DeltaSeconds);
    if (IsVisible() && !IsDesignTime())
    {
        auto* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
        if (Trigger.bHolding && Viewport && Viewport->Viewport && !Viewport->Viewport->HasFocus())
        {
            CancelTrigger();
            return;
        }
        AdvanceInteraction(DeltaSeconds);
    }
}
void UOBTrainingResultWidget::Select(EOBTrainingResultAction Action)
{
    if (bWarningInteraction)
    {
        if (Action==EOBTrainingResultAction::None || LastWarningAction!=EOBTrainingWarningAction::None) return;
        LastWarningAction=Action==EOBTrainingResultAction::NextStage ? EOBTrainingWarningAction::Continue : EOBTrainingWarningAction::ExitToMenu;
        OnWarningActionSelected.Broadcast(LastWarningAction);
        if (!bAutomaticNavigation || IsDesignTime()) return;
        if (LastWarningAction==EOBTrainingWarningAction::Continue) { if (Director.IsValid()) Director->ResumePrototype(); }
        else if (!MainMenu.IsNull()) UGameplayStatics::OpenLevelBySoftObjectPtr(this,MainMenu);
        return;
    }
    if (Action == EOBTrainingResultAction::None || LastAction != EOBTrainingResultAction::None) return;
    // The stage report follows its own printed prompts: tap retries, hold returns home.
    if (bStageCompletion) Action=Action==EOBTrainingResultAction::Retry ? EOBTrainingResultAction::NextStage : EOBTrainingResultAction::Retry;
    LastAction = Action; OnResultActionSelected.Broadcast(Action);
    if (!bAutomaticNavigation) return;
    if (Action == EOBTrainingResultAction::Retry)
    {
        if (auto* PC=Cast<AOBPlayerController>(GetOwningPlayer())) PC->RestartCurrentStage();
        else UGameplayStatics::OpenLevel(this,FName(*UGameplayStatics::GetCurrentLevelName(this,true)));
    }
    else if (bStageCompletion)
    {
        if (auto* PC=Cast<AOBPlayerController>(GetOwningPlayer())) PC->ReturnToMainMenu();
        else if (!MainMenu.IsNull()) UGameplayStatics::OpenLevelBySoftObjectPtr(this,MainMenu);
    }
    else if (!NextStage.IsNull()) UGameplayStatics::OpenLevelBySoftObjectPtr(this, NextStage);
}

#include "Widget/OBHudPanelWidget.h"
#include "Training/OBTrainingUIData.h"
#include "Engine/Texture2D.h"
#include "Training/OBTrainingDirector.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Spacer.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Widget/OBUIVideo.h"
#include "MediaSource.h"
#include "Materials/MaterialInterface.h"
#include "Animation/WidgetAnimation.h"

namespace
{
void SetText(UTextBlock* Label, const FText& Text)
{
    if (Label && !Label->GetText().EqualTo(Text)) Label->SetText(Text);
}
}
void UOBHudPanelWidget::SetTrainingDirector(AOBTrainingDirector* InDirector)
{
    Director = InDirector;
    if (Director.IsValid()) UpdateTrainingData();
}
void UOBHudPanelWidget::BuildDefaultLayout(EOBHudPanelRole InRole)
{
    Role = InRole;
    if (!WidgetTree) WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transient);
    if (WidgetTree->RootWidget) return;
    auto Text = [&](const TCHAR* WidgetName, int32 Size, const TCHAR* Sample) {
        auto* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), WidgetName);
        Label->SetText(FText::FromString(Sample)); Label->SetAutoWrapText(false);
        Label->SetColorAndOpacity(FLinearColor::White);
        Label->SetFont(FSlateFontInfo(Label->GetFont().FontObject, Size));
        Label->SetShadowColorAndOpacity(FLinearColor::Black); Label->SetShadowOffset(FVector2D(1,1));
        return Label;
    };
    if (Role == EOBHudPanelRole::Subtitle)
    {
        auto* Content=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("SubtitleRow"));
        auto* Alignment=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("SubtitleAlignment"));
        Alignment->SetBrushColor(FLinearColor::Transparent); Alignment->SetPadding(FMargin(0));
        Alignment->SetHorizontalAlignment(HAlign_Center); Alignment->SetVerticalAlignment(VAlign_Bottom);
        Alignment->SetContent(Content); WidgetTree->RootWidget=Alignment;
        Name=Text(TEXT("Name"),22,TEXT("교관")); Name->SetJustification(ETextJustify::Left); Name->SetAutoWrapText(false);
        Script=Text(TEXT("Script"),26,TEXT("교관의 명령에 따라 훈련을 진행합니다.")); Script->SetJustification(ETextJustify::Left);
        Content->AddChildToHorizontalBox(Name)->SetVerticalAlignment(VAlign_Top);
        auto* Gap=WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass(),TEXT("DialogueGap"));
        Gap->SetSize(FVector2D(24,1)); Content->AddChildToHorizontalBox(Gap);
        auto* ScriptSlot=Content->AddChildToHorizontalBox(Script);
        ScriptSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); ScriptSlot->SetVerticalAlignment(VAlign_Top);
        return;
    }
    auto* Border = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PanelBackground"));
    Border->SetBrushColor(Role == EOBHudPanelRole::Pause ? FLinearColor(0.08f,0.02f,0.02f,0.98f) : FLinearColor(0.02f,0.04f,0.07f,0.94f));
    Border->SetPadding(FMargin(Role == EOBHudPanelRole::Guide ? 30 : 16));
    auto* Content = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PanelContent"));
    Border->SetContent(Content); WidgetTree->RootWidget = Border;
    TitleText = Text(TEXT("TitleText"), 26, Role == EOBHudPanelRole::Guide ? TEXT("훈련 안내 1/5") : Role == EOBHudPanelRole::Pause ? TEXT("기초 훈련 일시중단") : Role == EOBHudPanelRole::Timer ? TEXT("훈련 시간") : Role == EOBHudPanelRole::Life ? TEXT("LIFE") : TEXT("기초 훈련"));
    Content->AddChildToVerticalBox(TitleText)->SetPadding(FMargin(0,0,0,16));
    if (Role == EOBHudPanelRole::Mission)
    {
        const TCHAR* Labels[]={TEXT("총기 확인"),TEXT("조준"),TEXT("엄폐"),TEXT("장전"),TEXT("영점 사격")};
        for (int32 I=0;I<5;++I)
            Content->AddChildToVerticalBox(Text(*FString::Printf(TEXT("BodyText_%d"),I+1),20,Labels[I]))->SetPadding(FMargin(0,0,0,6));
        return;
    }
    if (Role == EOBHudPanelRole::Timer)
    {
        ValueText = Text(TEXT("ValueText"), 28, TEXT("02 : 00")); Content->AddChildToVerticalBox(ValueText); return;
    }
    BodyText = Text(TEXT("BodyText"), Role == EOBHudPanelRole::Guide ? 26 : 20, Role == EOBHudPanelRole::Mission ? TEXT("□ 총기 확인\n□ 조준\n□ 엄폐·장전\n□ 영점 사격") : Role == EOBHudPanelRole::Pause ? TEXT("훈련이 일시중단되었습니다.") : TEXT("총기 상태 확인, 조준, 엄폐·장전과 영점 사격을 배웁니다."));
    Content->AddChildToVerticalBox(BodyText)->SetPadding(FMargin(0,0,0,18));
    if (Role == EOBHudPanelRole::Guide)
    {
        auto* GuideImage=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("BodyImage"));
        Content->AddChildToVerticalBox(GuideImage);
        auto* GuideTrigger=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("TriggerImage"));
        GuideTrigger->SetVisibility(ESlateVisibility::Hidden);
        Content->AddChildToVerticalBox(GuideTrigger);
    }
    if (Role == EOBHudPanelRole::Pause)
    {
        PromptText=Text(TEXT("PromptText"),22,TEXT("방아쇠 한 번: 속행\n방아쇠 길게 (2초): 메인 메뉴"));
        Content->AddChildToVerticalBox(PromptText);
    }
}
void UOBHudPanelWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    Role = GetPanelRole();
}
AOBTrainingDirector* UOBHudPanelWidget::GetTrainingDirector() const { return Director.Get(); }
void UOBHudPanelWidget::SetInteractionPrompt(const FText& Text) { SetText(PromptText,Text); }
void UOBHudPanelWidget::SetPanelTitle(const FText& Text) { SetText(TitleText,Text); }
void UOBHudPanelWidget::SetDialogue(const FText& Speaker, const FText& Dialogue)
{
    SetText(Name,Speaker); SetText(Script,Dialogue);
}
void UOBHudPanelWidget::SetMessage(const FText& Message) { SetText(GetPanelRole()==EOBHudPanelRole::Subtitle ? Script.Get() : BodyText.Get(), Message); }
void UOBHudPanelWidget::SetRemainingSeconds(float Seconds)
{
    RemainingSeconds = FMath::IsFinite(Seconds) ? FMath::Max(0.f, Seconds) : 0;
    const int32 Whole = FMath::CeilToInt(RemainingSeconds);
    SetText(ValueText, FText::FromString(FString::Printf(TEXT("%02d : %02d"), Whole / 60, Whole % 60)));
}
void UOBHudPanelWidget::StartCountdown(float Seconds) { SetRemainingSeconds(Seconds); bCountdownRunning = RemainingSeconds > 0; }
void UOBHudPanelWidget::StopCountdown() { bCountdownRunning = false; }
void UOBHudPanelWidget::SetZoneProgress(int32 Current, int32 Total)
{
    SetText(TitleText, FText::FromString(FString::Printf(TEXT("ZONE %d / %d"), FMath::Max(0, Current), FMath::Max(0, Total))));
}
void UOBHudPanelWidget::SetHealth(float Current, float Maximum)
{
    // Preserve the authored full-heart default until gameplay supplies a health range.
    const float Fraction = Maximum <= 0 ? 1.f : FMath::IsFinite(Current) && FMath::IsFinite(Maximum)
        ? FMath::Clamp(Current / Maximum, 0.f, 1.f) : 0.f;
    const int32 Count = FMath::CeilToInt(Fraction * 5);
    auto* CurrentText=Cast<UTextBlock>(GetWidgetFromName(TEXT("Text_Life")));
    if (!CurrentText) CurrentText=TitleText;
    SetText(CurrentText,FText::AsNumber(Count));
    if (CurrentText) CurrentText->SetColorAndOpacity(FSlateColor(FLinearColor(FColor::FromHex(TEXT("2E5D85FF")))));
    if (HealthProgress)
    {
        HealthProgress->SetPercent(Fraction);
        return;
    }
    // The current authored HUD uses one heart and a numeric count; retain support for the older five-icon layout.
    if (LIFE_01 && !LIFE_02 && !LIFE_03 && !LIFE_04 && !LIFE_05)
    {
        auto* Texture=LoadObject<UTexture2D>(nullptr,Count>0 ? TEXT("/Game/Assets/UI/Life/HP_Full.HP_Full") : TEXT("/Game/Assets/UI/Life/HP_None.HP_None"));
        if (Texture && LIFE_01->GetBrush().GetResourceObject()!=Texture) LIFE_01->SetBrushFromTexture(Texture,false);
        LIFE_01->SetRenderOpacity(1.f);
        return;
    }
    int32 Index = 0;
    for (UImage* Icon : {LIFE_01.Get(), LIFE_02.Get(), LIFE_03.Get(), LIFE_04.Get(), LIFE_05.Get()})
        if (Icon) Icon->SetRenderOpacity(++Index <= Count ? 1.f : 0.15f);
}
void UOBHudPanelWidget::NativeTick(const FGeometry& Geometry, float DeltaSeconds)
{
    Super::NativeTick(Geometry, DeltaSeconds);
    if (Director.IsValid()) UpdateTrainingData();
    else if (Role == EOBHudPanelRole::Timer && bCountdownRunning)
    {
        SetRemainingSeconds(RemainingSeconds - DeltaSeconds); if (RemainingSeconds <= 0) bCountdownRunning = false;
    }
}
void UOBHudPanelWidget::UpdateTrainingData()
{
    const auto* Training = Director.Get();
    const auto Result = Training->GetTrainingResult();
    const bool bBasic = Training->bUseSplinePrototype ? Training->GetPrototypeRouteIndex() == 1 : Training->GetCurrentZoneIndex() == 0;
    if (Role == EOBHudPanelRole::Guide)
    {
        if (auto* Guide=Cast<UOBGuideWidget>(this)) Guide->SetGuidePage(Training->GetGuideUIId(),Training->bGuideAdvanceReady,false);
    }
    else if (Role == EOBHudPanelRole::Subtitle) SetDialogue(Training->GetSubtitleSpeaker(), Training->GetSubtitleText());
    else if (Role == EOBHudPanelRole::Timer)
    {
        SetText(TitleText, FText::FromString(bBasic ? TEXT("기초 훈련") : TEXT("모의 훈련")));
        SetRemainingSeconds(Training->GetRemainingSeconds());
    }
    else if (Role == EOBHudPanelRole::Mission)
    {
        if (auto* Mission=Cast<UOBMissionWidget>(this)) Mission->UpdateTrainingObjectives(Result,bBasic);
    }
}

void UOBGuideWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    if (TriggerImage) TriggerImage->SetBrushFromMaterial(LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Blueprints/Widget/Media/M_UIVideo.M_UIVideo")));
    SetTriggerReady(false);
}
void UOBGuideWidget::NativePreConstruct()
{
    Super::NativePreConstruct();
    if (IsDesignTime())
    {
        if (TriggerImage) TriggerImage->SetBrushFromMaterial(LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Blueprints/Widget/Media/M_UIVideo.M_UIVideo")));
        SetGuidePage(3001,false);
    }
}
void UOBGuideWidget::SetGuidePage(int32 UIId,bool bCanAdvance,bool bAutoShow)
{
    if (UIId!=CurrentUIId || (!LoadedUIData && UIId>=3001 && UIId<=3007))
    {
        CancelGuideTransitions();
        SetTriggerReady(false);
        CurrentUIId=UIId;
        if (UIId>=3001 && UIId<=3007)
        {
            if (!LoadedUIData) LoadedUIData=UIDataTable.LoadSynchronous();
            const auto* Row=LoadedUIData && LoadedUIData->GetRowStruct()==FOBTrainingUIRow::StaticStruct()
                ? LoadedUIData->FindRow<FOBTrainingUIRow>(FName(*FString::FromInt(UIId)),TEXT("Training guide"),false) : nullptr;
            SetPanelTitle(Row ? Row->Title : FText::GetEmpty());
            SetMessage(Row ? Row->Body : FText::GetEmpty());
            if (BodyImage) BodyImage->SetBrushFromTexture(Row ? Row->Image.LoadSynchronous() : nullptr,true);
            if (!Row) UE_LOG(LogTemp,Warning,TEXT("Training UI row missing | Guide=%d"),UIId);
        }
    }
    if (UIId>=3001 && UIId<=3007 && bAutoShow && !bGuideShown && !bGuideHiding) PlayGuideFadeIn();
    SetTriggerReady(UIId>=3001 && UIId<=3007 && bCanAdvance && !bGuideHiding);
}
void UOBGuideWidget::PlayGuideFadeIn()
{
    if (CurrentUIId<3001 || CurrentUIId>3007 || bGuideShown || bGuideHiding) return;
    bGuideShown=true; SetRenderOpacity(1.f);
    // The current WBP_Guide labels are reversed: Out shows the page, In hides it.
    if (GuideFadeOut && !IsDesignTime()) PlayAnimation(GuideFadeOut);
}
void UOBGuideWidget::DismissGuide()
{
    if (bGuideHiding) return;
    if (GuideFadeOut) StopAnimation(GuideFadeOut);
    bGuideHiding=true; SetTriggerReady(false);
    GuideAnimationTag=FName(TEXT("OBGuide"),++GuideAnimationRequest);
    if (GuideFadeIn && !IsDesignTime())
    {
        FWidgetAnimationDynamicEvent Finished;Finished.BindDynamic(this,&ThisClass::HandleGuideFadeOutFinished);
        BindToAnimationEvent(GuideFadeIn,Finished,EWidgetAnimationEvent::Finished,GuideAnimationTag);
        auto Handle=PlayAnimation(GuideFadeIn);Handle.SetUserTag(GuideAnimationTag);
    }
    else HandleGuideFadeOutFinished();
}
void UOBGuideWidget::HandleGuideFadeOutFinished()
{
    if (!bGuideHiding) return;
    bGuideHiding=bGuideShown=false; SetRenderOpacity(0.f);
    OnGuideHidden.Broadcast();
}
void UOBGuideWidget::CancelGuideTransitions()
{
    bGuideShown=bGuideHiding=false;
    if (GuideFadeIn)
    {
        FWidgetAnimationDynamicEvent Finished;Finished.BindDynamic(this,&ThisClass::HandleGuideFadeOutFinished);
        UnbindFromAnimationFinished(GuideFadeIn,Finished);StopAnimation(GuideFadeIn);
    }
    if (GuideFadeOut) StopAnimation(GuideFadeOut);
    SetRenderOpacity(IsDesignTime() ? 1.f : 0.f);
}
void UOBGuideWidget::SetTriggerReady(bool bReady)
{
    if (auto* Prompt=GetWidgetFromName(TEXT("AdvancePrompt"))) Prompt->SetVisibility(bReady ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    if (TriggerImage) TriggerImage->SetVisibility(bReady ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
    if (auto* Hint=GetWidgetFromName(TEXT("ContinueHint"))) Hint->SetVisibility(bReady ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
    if (bTriggerReady==bReady) return;
    bTriggerReady=bReady;
    if (!bReady) { if (TriggerVideo) TriggerVideo->Close(); return; }
    if (IsDesignTime() || !TriggerImage) return;
    if (!TriggerVideo)
    {
        TriggerVideo=NewObject<UOBUIVideo>(this);
        if (!TriggerVideo->Initialize(TriggerImage)) { TriggerVideo=nullptr; return; }
    }
    if (!TriggerVideo->Open(TriggerSource.LoadSynchronous(),true,true))
        UE_LOG(LogTemp,Warning,TEXT("GUIDE_VIDEO TriggerPress could not open"));
}
void UOBGuideWidget::NativeDestruct()
{
    CancelGuideTransitions();
    SetTriggerReady(false);
    if (TriggerVideo) TriggerVideo->Close();
    Super::NativeDestruct();
}

UOBMissionWidget::UOBMissionWidget()
{
    Role=EOBHudPanelRole::Mission;
}
void UOBMissionWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    PendingTextColors.Reset();
    for (auto* Row : {BodyText_1.Get(),BodyText_2.Get(),BodyText_3.Get(),BodyText_4.Get(),BodyText_5.Get()})
        PendingTextColors.Add(Row ? Row->GetColorAndOpacity() : FSlateColor(FLinearColor::White));
    for (auto* Strike : {Strike_1.Get(),Strike_2.Get(),Strike_3.Get(),Strike_4.Get(),Strike_5.Get()})
        if (Strike) Strike->SetVisibility(ESlateVisibility::Hidden);
}
void UOBMissionWidget::SetMissionObjectives(const FText& Heading,const TArray<FOBMissionObjective>& Objectives)
{
    SetPanelTitle(Heading);
    SetText(MissionTitleText,Heading);
    const TArray<UTextBlock*> Rows={BodyText_1.Get(),BodyText_2.Get(),BodyText_3.Get(),BodyText_4.Get(),BodyText_5.Get()};
    const TArray<UImage*> Strikes={Strike_1.Get(),Strike_2.Get(),Strike_3.Get(),Strike_4.Get(),Strike_5.Get()};
    const TArray<UTextBlock*> Outcomes={OutcomeText_1.Get(),OutcomeText_2.Get(),OutcomeText_3.Get(),OutcomeText_4.Get(),OutcomeText_5.Get()};
    const FSlateBrush NoStrike;
    for (int32 I=0;I<Rows.Num();++I)
    {
        const bool bUsed=Objectives.IsValidIndex(I) && !Objectives[I].Text.IsEmpty();
        const bool bCompleted=bUsed && Objectives[I].bCompleted;
        const auto Outcome=bUsed ? (Objectives[I].Outcome==EOBTrainingOutcome::NotRun && bCompleted ? EOBTrainingOutcome::Passed : Objectives[I].Outcome) : EOBTrainingOutcome::NotRun;
        const bool bPassed=Outcome==EOBTrainingOutcome::Passed;
        const bool bFailed=Outcome==EOBTrainingOutcome::Failed || Outcome==EOBTrainingOutcome::Aborted;
        if (auto* Row=Rows[I])
        {
            SetText(Row,bUsed ? Objectives[I].Text : FText::GetEmpty());
            Row->SetVisibility(bUsed ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
            if (!(Row->GetStrikeBrush()==NoStrike)) Row->SetStrikeBrush(NoStrike);
            const FSlateColor Color=bPassed || bFailed ? FSlateColor(FLinearColor(FColor::FromHex(TEXT("596A7A")))) :
                PendingTextColors.IsValidIndex(I) ? PendingTextColors[I] : Row->GetColorAndOpacity();
            if (!(Row->GetColorAndOpacity()==Color)) Row->SetColorAndOpacity(Color);
        }
        if (auto* Strike=Strikes[I])
        {
            Strike->SetColorAndOpacity(FLinearColor(FColor::FromHex(bFailed ? TEXT("D65B67") : TEXT("8A99A8"))));
            Strike->SetVisibility(bPassed || bFailed ? ESlateVisibility::HitTestInvisible : bUsed ? ESlateVisibility::Hidden : ESlateVisibility::Collapsed);
        }
        if (auto* Status=Outcomes[I])
        {
            SetText(Status,FText::FromString(bPassed ? TEXT("Pass") : bFailed ? TEXT("Fail") : TEXT("")));
            Status->SetColorAndOpacity(FLinearColor(FColor::FromHex(bFailed ? TEXT("BD3D4D") : TEXT("218555"))));
            Status->SetVisibility(bUsed ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
        }
    }
}
void UOBMissionWidget::UpdateTrainingObjectives(const FOBTrainingResultData& Result,bool bBasic)
{
    TArray<FOBMissionObjective> Objectives;
    auto Add=[&](const FString& Text,EOBTrainingStep Step,bool bForcePassed=false) {
        FOBMissionObjective Item; Item.Text=FText::FromString(Text);
        if (const auto* Found=Result.Steps.FindByPredicate([&](const auto& S){return S.Step==Step;})) Item.Outcome=Found->Outcome;
        if (bForcePassed) Item.Outcome=EOBTrainingOutcome::Passed;
        Item.bCompleted=Item.Outcome==EOBTrainingOutcome::Passed; Objectives.Add(Item);
    };
    if (bBasic)
    {
        Add(TEXT("총기 확인"),EOBTrainingStep::WeaponCheck);
        Add(TEXT("조준 훈련"),EOBTrainingStep::AimHold);
        Add(TEXT("엄폐 훈련"),EOBTrainingStep::Cover);
        Add(TEXT("재장전 훈련"),EOBTrainingStep::Reload);
        Add(TEXT("영점 사격"),EOBTrainingStep::Accuracy);
    }
    else
    {
        Add(FString::Printf(TEXT("표적 명중 %d/%d"),Result.UniqueTargetsHit,Result.TotalTargets),EOBTrainingStep::TimedTargets,
            Result.TotalTargets>0 && Result.UniqueTargetsHit>=Result.TotalTargets);
        Add(TEXT("브리칭"),EOBTrainingStep::Breach);
    }
    SetMissionObjectives(FText::FromString(bBasic ? TEXT("훈련 목표") : TEXT("모의 훈련")),Objectives);
}

void UOBPauseWidget::PrepareInteraction(bool bAlreadyHeld)
{
    Trigger.Reset(bAlreadyHeld); LastAction=EOBTrainingPauseAction::None; RefreshPrompt();
}
void UOBPauseWidget::RefreshPrompt()
{
    const FString Hint=Trigger.bWaitingForRelease ? TEXT("방아쇠를 놓은 뒤 다시 당겨주세요.") : Trigger.bHolding ?
        FString::Printf(TEXT("놓으면 속행\n메인 메뉴: %.1f / %.1f초"),Trigger.HeldSeconds,LongPressSeconds) :
        FString::Printf(TEXT("방아쇠 한 번: 속행\n방아쇠 길게 (%.1f초): 메인 메뉴"),LongPressSeconds);
    SetInteractionPrompt(FText::FromString(Hint));
}
void UOBPauseWidget::HandleTrigger(bool bPressed) { Select(Trigger.Change(bPressed,LongPressSeconds)); RefreshPrompt(); }
void UOBPauseWidget::AdvanceInteraction(float DeltaSeconds) { Select(Trigger.Advance(DeltaSeconds,LongPressSeconds)); RefreshPrompt(); }
void UOBPauseWidget::CancelInteraction() { Trigger.Cancel(); RefreshPrompt(); }
void UOBPauseWidget::NativePreConstruct() { Super::NativePreConstruct(); RefreshPrompt(); }
void UOBPauseWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);
    if (IsDesignTime() || !IsVisible()) return;
    auto* Viewport=GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
    if (Trigger.bHolding && Viewport && Viewport->Viewport && !Viewport->Viewport->HasFocus()) { CancelInteraction(); return; }
    AdvanceInteraction(DeltaSeconds);
}
void UOBPauseWidget::Select(EOBTrainingResultAction Gesture)
{
    if (Gesture==EOBTrainingResultAction::None || LastAction!=EOBTrainingPauseAction::None) return;
    LastAction=Gesture==EOBTrainingResultAction::NextStage ? EOBTrainingPauseAction::Continue : EOBTrainingPauseAction::ExitToMenu;
    OnActionSelected.Broadcast(LastAction);
    if (!bAutomaticActions || IsDesignTime()) return;
    if (LastAction==EOBTrainingPauseAction::Continue) { if (auto* Training=GetTrainingDirector()) Training->ResumePrototype(); }
    else if (!MainMenu.IsNull()) UGameplayStatics::OpenLevelBySoftObjectPtr(this,MainMenu);
}

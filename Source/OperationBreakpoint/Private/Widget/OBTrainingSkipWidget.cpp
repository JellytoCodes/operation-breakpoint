#include "Widget/OBTrainingSkipWidget.h"
#include "Widget/OBUIVideo.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "Core/OBPlayerController.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "MediaSource.h"

void UOBTrainingSkipWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    if (TitleText) TitleText->SetText(FText::FromString(TEXT("기초훈련 건너뛰기")));
    if (BodyText) BodyText->SetText(FText::FromString(FString::Printf(TEXT("%.0f초간 방아쇠를 눌러주세요"),HoldSeconds)));
    ResetHold(); SetVisibility(ESlateVisibility::Collapsed);
}
void UOBTrainingSkipWidget::ResetHold() { HeldSeconds=0; bHolding=false; if (ProgressBar) ProgressBar->SetPercent(0); }
void UOBTrainingSkipWidget::SetAvailable(bool bEnabled,bool bAlreadyHeld)
{
    if (bAvailable==bEnabled) return;
    bAvailable=bEnabled; bWaitingForRelease=bAlreadyHeld; bTriggered=false; ResetHold();
    SetVisibility(bEnabled ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    if (bEnabled)
    {
        bVideoBound=false;
        if (TriggerLoopVideo)
        {
            TriggerLoopVideo->SetVisibility(ESlateVisibility::HitTestInvisible);
            TriggerLoopVideo->SetColorAndOpacity(FLinearColor::White);
            if (auto* Poster=LoadObject<UTexture2D>(nullptr,TEXT("/Game/Assets/UI/TriggerPress_Poster.TriggerPress_Poster")))
                TriggerLoopVideo->SetBrushFromTexture(Poster);
        }
        if (!Video)
        {
            Video=NewObject<UOBUIVideo>(this);
            if (!Video->Initialize(TriggerLoopVideo,false)) { Video=nullptr; return; }
        }
        if (!Video->Open(TriggerSource.LoadSynchronous(),true,true)) UE_LOG(LogTemp,Warning,TEXT("TRAINING_SKIP TriggerPress could not open"));
    }
    else if (Video) { Video->Close(); bVideoBound=false; }
}
bool UOBTrainingSkipWidget::HandleTrigger(bool bPressed)
{
    if (!bAvailable) return false;
    if (bWaitingForRelease) { if (!bPressed) bWaitingForRelease=false; return true; }
    if (!bPressed) ResetHold();
    else if (!bHolding && !bTriggered) { bHolding=true; HeldSeconds=0; }
    return true;
}
void UOBTrainingSkipWidget::AdvanceHold(float DeltaSeconds)
{
    if (!bAvailable || !bHolding || bTriggered || !FMath::IsFinite(DeltaSeconds)) return;
    HeldSeconds+=FMath::Max(0.f,DeltaSeconds);
    if (ProgressBar) ProgressBar->SetPercent(GetHoldProgress());
    if (HeldSeconds<FMath::Max(.1f,HoldSeconds)) return;
    bTriggered=true; bHolding=false;
    OnSkipRequested.Broadcast();
}
void UOBTrainingSkipWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);
    if (!bAvailable || IsDesignTime()) return;
    if (!bVideoBound && Video && Video->IsReadyToDisplay())
    { Video->BindToImage(TriggerLoopVideo); bVideoBound=true; }
    auto* PC=Cast<AOBPlayerController>(GetOwningPlayer());
    auto* Client=GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
    auto* Viewport=Client ? Client->Viewport : nullptr;
    if (!PC || !PC->IsTriggerHeld() || !Viewport || !Viewport->HasFocus() || !Viewport->IsForegroundWindow())
    { ResetHold(); return; }
    AdvanceHold(DeltaSeconds);
}
void UOBTrainingSkipWidget::NativeDestruct()
{
    bAvailable=false; ResetHold();
    if (Video) Video->Close();
    Super::NativeDestruct();
}

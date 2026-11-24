#include "Stage/OBStageCinematicWidget.h"
#include "Widget/OBUIVideo.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "MediaPlayer.h"
#include "MediaSoundComponent.h"

void UOBStageCinematicWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    auto* Canvas=WidgetTree->ConstructWidget<UCanvasPanel>();
    WidgetTree->RootWidget=Canvas;
    auto Fill=[Canvas](UWidget* W) { auto* S=Canvas->AddChildToCanvas(W);S->SetAnchors(FAnchors(0,0,1,1));S->SetOffsets(FMargin(0)); };
    auto* Black=WidgetTree->ConstructWidget<UImage>();Black->SetColorAndOpacity(FLinearColor::Black);Fill(Black);
    VideoImage=WidgetTree->ConstructWidget<UImage>();Fill(VideoImage);
    Video=NewObject<UOBUIVideo>(this);
    if (Video->Initialize(VideoImage))
    {
        auto* Player=Video->GetPlayer();
        Player->OnMediaOpened.AddDynamic(this,&ThisClass::HandleOpened);
        Player->OnMediaOpenFailed.AddDynamic(this,&ThisClass::HandleFailed);
        Player->OnEndReached.AddDynamic(this,&ThisClass::HandleFinished);
    }
    SetVisibility(ESlateVisibility::HitTestInvisible);
}
UMediaPlayer* UOBStageCinematicWidget::GetMediaPlayer() const { return Video ? Video->GetPlayer() : nullptr; }
bool UOBStageCinematicWidget::OpenVideo(UMediaSource* Source,bool bVideoAudio,float AudioVolumeMultiplier)
{
    bFinished=false;VideoImage->SetVisibility(ESlateVisibility::HitTestInvisible);
    if (bVideoAudio && GetOwningPlayer())
    {
        MediaSound=NewObject<UMediaSoundComponent>(GetOwningPlayer());
        MediaSound->SetMediaPlayer(GetMediaPlayer());MediaSound->RegisterComponent();
        MediaSound->CreateAudioComponent();
        MediaSound->SetVolumeMultiplier(FMath::Max(0.f,AudioVolumeMultiplier));
    }
    if (Video && Video->Open(Source,false,true)) return true;
    HandleFinished();return false;
}
void UOBStageCinematicWidget::HandleOpened(FString URL) { OnVideoOpened.Broadcast(); }
void UOBStageCinematicWidget::HandleFailed(FString URL) { HandleFinished(); }
void UOBStageCinematicWidget::HandleFinished()
{
    if (bFinished) return;
    bFinished=true;
    if (VideoImage) VideoImage->SetVisibility(ESlateVisibility::Collapsed);
    OnVideoFinished.Broadcast();
}
void UOBStageCinematicWidget::CloseVideo()
{
    if (Video) Video->Close();
    if (MediaSound) { MediaSound->DestroyComponent();MediaSound=nullptr; }
}
void UOBStageCinematicWidget::NativeDestruct() { CloseVideo();Super::NativeDestruct(); }

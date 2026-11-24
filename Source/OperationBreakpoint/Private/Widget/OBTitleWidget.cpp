#include "Widget/OBTitleWidget.h"
#include "Core/OBGameInstance.h"
#include "Training/OBTrainingProgressSubsystem.h"
#include "Engine/GameInstance.h"
#include "Widget/OBUIVideo.h"
#include "MediaPlayer.h"
#include "MediaSource.h"
#include "FileMediaSource.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Input/Reply.h"
#include "Sound/SoundBase.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Input/OBOpenFireSubsystem.h"
#include "Input/OBDeviceSettingsSubsystem.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "UnrealClient.h"

void UOBTitleWidget::NativeConstruct()
{
    Super::NativeConstruct();
    if (IsDesignTime()) return;
    bOpenFireStartArmed=false; LastOpenFireSequence=-1; OpenFireSession.Reset();
    if (auto* Receiver=GetGameInstance()?GetGameInstance()->GetSubsystem<UOBOpenFireSubsystem>():nullptr)
        Receiver->OnInputReceived.AddUObject(this,&ThisClass::HandleOpenFireInput);
    bStartRequested=bMediaOpened=bSeeking=bEntrancePlaying=bNavigationRequested=bPlaybackFailed=false; IdleLoopCount=0;
    bEntranceVideoFinished=bEntranceVoiceFinished=false;EntranceVoiceFinishedAt=TrainingTravelAt=0;
    IdleStartSeconds=IdleEndSeconds=EntranceStartSeconds=EntranceEndSeconds=0;
    if (BackgroundVideo) BackgroundVideo->SetColorAndOpacity(FLinearColor::White);
    ActiveClip=BlendClip=INDEX_NONE;
    if (!BlendImage && BackgroundVideo && WidgetTree)
        if (auto* Canvas=Cast<UCanvasPanel>(BackgroundVideo->GetParent()))
        {
            BlendImage=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("BackgroundVideoBlend"));
            auto* BlendSlot=Canvas->AddChildToCanvas(BlendImage);
            if (auto* BaseSlot=Cast<UCanvasPanelSlot>(BackgroundVideo->Slot)) { BlendSlot->SetLayout(BaseSlot->GetLayout()); BlendSlot->SetZOrder(BaseSlot->GetZOrder()); }
            BlendImage->SetVisibility(ESlateVisibility::HitTestInvisible);
        }
    if (BlendImage) BlendImage->SetRenderOpacity(0);
    if (!ShortcutStateText && BackgroundVideo && WidgetTree)
        if (auto* Canvas=Cast<UCanvasPanel>(BackgroundVideo->GetParent()))
        {
            ShortcutStateText=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("StageShortcutStatus"));
            auto* StatusSlot=Canvas->AddChildToCanvas(ShortcutStateText);
            StatusSlot->SetAnchors(FAnchors(1.f,0.f));StatusSlot->SetAlignment(FVector2D(1.f,0.f));
            StatusSlot->SetPosition(FVector2D(-36.f,28.f));StatusSlot->SetAutoSize(true);StatusSlot->SetZOrder(100);
            auto Font=ShortcutStateText->GetFont();Font.Size=10;Font.TypefaceFontName=TEXT("Regular");
            ShortcutStateText->SetFont(Font);ShortcutStateText->SetJustification(ETextJustify::Right);
            ShortcutStateText->SetShadowOffset(FVector2D(1,1));ShortcutStateText->SetShadowColorAndOpacity(FLinearColor::Black);
            ShortcutStateText->SetColorAndOpacity(FLinearColor(1.f,1.f,1.f,.55f));
        }
    if (PressText)
    {
        PressText->SetText(FText::FromString(TEXT("방아쇠를 당겨 시작")));
        PressText->SetVisibility(ESlateVisibility::HitTestInvisible);
    }
    // UMG loop count 0 repeats indefinitely; use the animation authored in WBP_Title.
    if (PressTextFadeInOut) PlayAnimation(PressTextFadeInOut,0.f,0,EUMGSequencePlayMode::Forward,1.f,false);
    if (LogoImage)
    {
        auto* Logo = LoadObject<UTexture2D>(nullptr,TEXT("/Game/Assets/UI/Logo.Logo"));
        auto* Title = LoadObject<UTexture2D>(nullptr,TEXT("/Game/Assets/UI/Logo_Title.Logo_Title"));
        if (Logo && Title)
        {
            // The current title blueprint has one image for its combined logo and name.
            if (!TitleImage && WidgetTree)
                if (auto* Canvas=Cast<UCanvasPanel>(LogoImage->GetParent()))
                {
                    TitleImage=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("OperationBreakpointTitleArt"));
                    auto* TitleSlot=Canvas->AddChildToCanvas(TitleImage);
                    if (auto* LogoSlot=Cast<UCanvasPanelSlot>(LogoImage->Slot)) TitleSlot->SetZOrder(LogoSlot->GetZOrder());
                }
            auto SetTitleArt=[](UImage* Image,UTexture2D* Texture,FVector2D Size,float Y) {
                Image->SetBrushFromTexture(Texture);
                Image->SetColorAndOpacity(FLinearColor::White);
                if (auto* Slot=Cast<UCanvasPanelSlot>(Image->Slot))
                {
                    Slot->SetAnchors(FAnchors(.5f,.5f));
                    Slot->SetAlignment(FVector2D(.5f,.5f));
                    Slot->SetAutoSize(false);
                    Slot->SetPosition(FVector2D(0,Y));
                    Slot->SetSize(Size);
                }
            };
            if (TitleImage)
            {
                SetTitleArt(LogoImage,Logo,FVector2D(320,320),-285.f);
                // The wordmark's visible aspect differs from its padded texture dimensions.
                SetTitleArt(TitleImage,Title,FVector2D(720,150),-95.f);
            }
        }
    }
    for (auto* Image : {LogoImage.Get(),TitleImage.Get()}) if (Image) Image->SetVisibility(Image->GetBrush().GetResourceObject() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
    OpenVideo();
}
UMediaSource* UOBTitleWidget::ResolvePortableMovie(UMediaSource* Source, const TCHAR* BuiltinAsset, const TCHAR* MovieName)
{
    // Older cooked packages can still contain the importer's Downloads path.
    // Repair only our two built-in movies, without changing the authored asset.
    auto* File = Cast<UFileMediaSource>(Source);
    if (!File || File->GetPathName() != BuiltinAsset || File->Validate()) return Source;
    const FString LocalPath = FPaths::ProjectContentDir() / TEXT("Movies") / MovieName;
    if (!IFileManager::Get().FileExists(*LocalPath)) return Source;
    auto* Portable = DuplicateObject<UFileMediaSource>(File, this);
    Portable->ClearFlags(RF_Public | RF_Standalone);
    Portable->SetFlags(RF_Transient);
    Portable->SetFilePath(FString(TEXT("./Movies/")) + MovieName);
    PlaybackSources.Add(Portable);
    UE_LOG(LogTemp, Display, TEXT("TITLE_VIDEO portable_source=%s"), MovieName);
    return Portable;
}

void UOBTitleWidget::OpenVideo()
{
    bPlaybackFailed=false;
    if (BackgroundVideo) BackgroundVideo->SetColorAndOpacity(FLinearColor::White);
    for (auto& Clip : Clips)
    {
        auto* Player=Clip->GetPlayer();
        Player->OnMediaOpened.RemoveAll(this); Player->OnSeekCompleted.RemoveAll(this);
        Player->OnEndReached.RemoveAll(this); Player->OnMediaOpenFailed.RemoveAll(this); Clip->Close();
    }
    Clips.Reset(); PlaybackSources.Reset(); Video=nullptr; ActiveClip=BlendClip=INDEX_NONE;
    auto* Source=ResolvePortableMovie(EntranceSource.LoadSynchronous(),TEXT("/Game/Assets/UI/TravleToTraining.TravleToTraining"),TEXT("TravleToTraining.mp4"));
    auto* Idle=ResolvePortableMovie(TitleSource.LoadSynchronous(),TEXT("/Game/Assets/UI/TitleLoop.TitleLoop"),TEXT("TitleLoop.mp4"));
    for (int32 Index=0;Index<3;++Index)
    {
        auto* Clip=NewObject<UOBUIVideo>(this);
        if (!Clip->Initialize(BackgroundVideo,false)) { HandleMediaFailed(TEXT("UI video material")); return; }
        Clips.Add(Clip); ClipStates[Index]=EClipState::Opening;
        auto* Player=Clip->GetPlayer();
        if (Index==0)
        { Player->OnMediaOpened.AddDynamic(this,&ThisClass::HandleMediaOpened); Player->OnSeekCompleted.AddDynamic(this,&ThisClass::HandleSeekCompleted); }
        else if (Index==1)
        { Player->OnMediaOpened.AddDynamic(this,&ThisClass::HandleStandbyOpened); Player->OnSeekCompleted.AddDynamic(this,&ThisClass::HandleStandbySeekCompleted); }
        else
        { Player->OnMediaOpened.AddDynamic(this,&ThisClass::HandleEntranceOpened); Player->OnSeekCompleted.AddDynamic(this,&ThisClass::HandleEntranceSeekCompleted); }
        if (Index==2) Player->OnEndReached.AddDynamic(this,&ThisClass::HandleMediaEnded);
        Player->OnMediaOpenFailed.AddDynamic(this,&ThisClass::HandleMediaFailed);
        if (!Clip->Open(Index==2 ? Source : Idle,false,false)) { HandleMediaFailed(TEXT("Title media source")); return; }
    }
    bSeeking=true;
}
void UOBTitleWidget::NativeDestruct()
{
    if (auto* Receiver=GetGameInstance()?GetGameInstance()->GetSubsystem<UOBOpenFireSubsystem>():nullptr)
        Receiver->OnInputReceived.RemoveAll(this);
    bEntrancePlaying=false;TrainingTravelAt=0;
    if (PressTextFadeInOut) StopAnimation(PressTextFadeInOut);
    for (auto& Clip : Clips)
    {
        if (auto* Player=Clip->GetPlayer())
        {
            Player->OnMediaOpened.RemoveAll(this); Player->OnSeekCompleted.RemoveAll(this);
            Player->OnEndReached.RemoveAll(this); Player->OnMediaOpenFailed.RemoveAll(this);
        }
        Clip->Close();
    }
    Clips.Reset(); PlaybackSources.Reset(); Video=nullptr;
    if (BlendImage) BlendImage->SetRenderOpacity(0);
    if (Voice) { Voice->OnAudioFinished.RemoveDynamic(this,&ThisClass::HandleEntranceVoiceFinished);Voice->Stop(); } Voice=nullptr;
    Super::NativeDestruct();
}
void UOBTitleWidget::HandleMediaOpened(FString) { ClipOpened(0); }
void UOBTitleWidget::HandleStandbyOpened(FString) { ClipOpened(1); }
void UOBTitleWidget::HandleEntranceOpened(FString) { ClipOpened(2); }
void UOBTitleWidget::HandleSeekCompleted() { ClipSeekCompleted(0); }
void UOBTitleWidget::HandleStandbySeekCompleted() { ClipSeekCompleted(1); }
void UOBTitleWidget::HandleEntranceSeekCompleted() { ClipSeekCompleted(2); }
void UOBTitleWidget::ClipOpened(int32 Index)
{
    if (!Clips.IsValidIndex(Index)) return;
    const double End=Clips[Index]->GetPlayer()->GetDuration().GetTotalSeconds();
    if (End<=0) { HandleMediaFailed(TEXT("Title media has no duration"));return; }
    if (Index==2) EntranceEndSeconds=End;else IdleEndSeconds=End;
    PrepareClip(Index);
}
void UOBTitleWidget::PrepareClip(int32 Index)
{
    // Seeking and decoder warm-up only happen offscreen. The visible player keeps advancing.
    if (!Clips.IsValidIndex(Index) || ActiveClip==Index || BlendClip==Index) return;
    auto* Player=Clips[Index]->GetPlayer(); Player->Pause(); ClipStates[Index]=EClipState::Seeking;
    PrepareDeadline[Index]=FPlatformTime::Seconds()+2;
    const double Target=Index==2 ? EntranceStartSeconds : IdleStartSeconds;
    if (!Player->Seek(FTimespan::FromSeconds(FMath::Max(0.,Target-2./24.))))
        HandleMediaFailed(TEXT("TitleLoop seek failed"));
}
void UOBTitleWidget::ClipSeekCompleted(int32 Index)
{
    if (!Clips.IsValidIndex(Index) || ClipStates[Index]!=EClipState::Seeking) return;
    // WMF can dispatch seek-completed before GetTime reflects the requested position.
    ClipStates[Index]=EClipState::AwaitingSeekClock;
    // Keep the hidden decoder running until its new timeline and first frame arrive.
    Clips[Index]->GetPlayer()->Play();
}
void UOBTitleWidget::BeginBlend(int32 Index)
{
    if (BlendClip!=INDEX_NONE || !Clips.IsValidIndex(Index) || ClipStates[Index]!=EClipState::Ready) return;
    ClipStates[Index]=EClipState::Playing;
    Clips[Index]->GetPlayer()->Play(); bMediaOpened=true; bSeeking=false;
    if (Index==2 && !bEntrancePlaying)
    {
        bEntrancePlaying=true;
        bEntranceVideoFinished=bEntranceVoiceFinished=false;EntranceVoiceFinishedAt=TrainingTravelAt=0;
        Voice=UGameplayStatics::CreateSound2D(this,EntranceVoice.LoadSynchronous());
        if (Voice) { Voice->OnAudioFinished.AddDynamic(this,&ThisClass::HandleEntranceVoiceFinished);Voice->Play(); }
        else HandleEntranceVoiceFinished();
        UE_LOG(LogTemp,Display,TEXT("TITLE_VIDEO entrance_started time=%.3f voice=%s"),Clips[Index]->GetPlayer()->GetTime().GetTotalSeconds(),*GetNameSafe(Voice));
    }
    if (ActiveClip==INDEX_NONE)
    { ActiveClip=Index; Video=Clips[Index]; Video->BindToImage(BackgroundVideo); return; }
    BlendClip=Index; BlendStartedAt=FPlatformTime::Seconds();
    if (BlendImage) { Clips[Index]->BindToImage(BlendImage); BlendImage->SetRenderOpacity(0); }
    // Switch immediately to the separate entrance movie.
    if (Index==2 || !BlendImage || LoopBlendSeconds<=0) FinishBlend();
}
void UOBTitleWidget::FinishBlend()
{
    const int32 Previous=ActiveClip;
    const double PreviousTime=Clips[Previous]->GetPlayer()->GetTime().GetTotalSeconds();
    ActiveClip=BlendClip; BlendClip=INDEX_NONE; Video=Clips[ActiveClip]; Video->BindToImage(BackgroundVideo);
    if (BlendImage) BlendImage->SetRenderOpacity(0);
    if (ActiveClip!=2)
    {
        ++IdleLoopCount;
        UE_LOG(LogTemp,Display,TEXT("TITLE_VIDEO idle_loop=%d outgoing=%.3f incoming=%.3f prepared_swap=1"),IdleLoopCount,PreviousTime,Video->GetPlayer()->GetTime().GetTotalSeconds());
        PrepareClip(Previous);
    }
    else Clips[Previous]->GetPlayer()->Pause();
}
void UOBTitleWidget::HandleMediaEnded()
{
    if (!bEntrancePlaying || bEntranceVideoFinished) return;
    bEntranceVideoFinished=true;
    if (Video) Video->GetPlayer()->Pause();
    if (BackgroundVideo)
    {
        // Detach the movie material as well: no decoder's final frame can leak through.
        BackgroundVideo->SetBrushFromTexture(nullptr,false);
        BackgroundVideo->SetColorAndOpacity(FLinearColor::Black);
    }
    if (BlendImage) BlendImage->SetRenderOpacity(0);
    UE_LOG(LogTemp,Display,TEXT("TITLE_VIDEO entrance_video_finished black_hold=1 voice_finished=%d"),bEntranceVoiceFinished);
}
void UOBTitleWidget::HandleEntranceVoiceFinished()
{
    if (!bEntrancePlaying || bEntranceVoiceFinished || bNavigationRequested) return;
    bEntranceVoiceFinished=true;EntranceVoiceFinishedAt=FPlatformTime::Seconds();
    TrainingTravelAt=EntranceVoiceFinishedAt+FMath::Max(0.f,TrainingTravelDelaySeconds);
    UE_LOG(LogTemp,Display,TEXT("TITLE_VIDEO entrance_voice_finished travel_delay=%.3f"),TrainingTravelDelaySeconds);
}
void UOBTitleWidget::HandleMediaFailed(FString URL)
{
    bPlaybackFailed=true;
    if (Voice) { Voice->OnAudioFinished.RemoveDynamic(this,&ThisClass::HandleEntranceVoiceFinished);Voice->Stop(); } Voice=nullptr;
    bEntranceVoiceFinished=bEntranceVideoFinished=false;TrainingTravelAt=0;
    bMediaOpened=bSeeking=bEntrancePlaying=bStartRequested=false;
    if (PressText) { PressText->SetVisibility(ESlateVisibility::HitTestInvisible); PressText->SetText(FText::FromString(TEXT("방아쇠를 당겨 시작"))); }
    UE_LOG(LogTemp,Error,TEXT("TITLE_VIDEO failed: %s"),*URL);
}
void UOBTitleWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);
    if (ShortcutStateText)
    {
        static const FText OnText = FText::FromString(TEXT("F11 : Shortcut Mode ON"));
        static const FText OffText = FText::FromString(TEXT("F11 : Shortcut Mode OFF"));
        const auto* Instance = GetGameInstance<UOBGameInstance>();
        const FText& Label = Instance && Instance->IsStageShortcutEnabled() ? OnText : OffText;
        if (!ShortcutStateText->GetText().IdenticalTo(Label)) ShortcutStateText->SetText(Label);
        ShortcutStateText->SetVisibility(bStartRequested ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
    }
    const double Now=FPlatformTime::Seconds();
    if (const auto* Settings=GetGameInstance()?GetGameInstance()->GetSubsystem<UOBDeviceSettingsSubsystem>():nullptr; Settings && Settings->IsOpen())
    { if (DeviceSettingsPausedAt==0) DeviceSettingsPausedAt=Now; return; }
    if (DeviceSettingsPausedAt>0)
    {
        const double Delay=Now-DeviceSettingsPausedAt;
        for (double& Deadline:PrepareDeadline) if (Deadline>0) Deadline+=Delay;
        if (BlendStartedAt>0) BlendStartedAt+=Delay;
        if (TrainingTravelAt>0) TrainingTravelAt+=Delay;
        if (EntranceVoiceFinishedAt>0) EntranceVoiceFinishedAt+=Delay;
        DeviceSettingsPausedAt=0;
    }
    if (Clips.Num()!=3 || bNavigationRequested || bPlaybackFailed) return;
    if (IsEntranceTravelReady(FPlatformTime::Seconds())) { FinishEntrance();return; }
    for (int32 Index=0;Index<3;++Index)
    {
        auto* Player=Clips[Index]->GetPlayer();
        const double Target=Index==2 ? EntranceStartSeconds : IdleStartSeconds;
        const double Position=Player->GetTime().GetTotalSeconds();
        // Seek completion can already be a few decoded frames beyond the seek target.
        // Accept that valid warm-up range rather than waiting forever on a missed frame.
        if (ClipStates[Index]==EClipState::AwaitingSeekClock && Position>=FMath::Max(0.,Target-2./24.)-.04 && Position<=Target+.5)
        { ClipStates[Index]=EClipState::Warming; ReadyAfterFrame[Index]=GFrameCounter+2; }
        if ((ClipStates[Index]==EClipState::Seeking || ClipStates[Index]==EClipState::AwaitingSeekClock) && FPlatformTime::Seconds()>PrepareDeadline[Index])
        { PrepareClip(Index); continue; }
        if (ClipStates[Index]==EClipState::Warming && Position>Target+.5)
        { PrepareClip(Index); continue; }
        if (ClipStates[Index]==EClipState::Warming && GFrameCounter>=ReadyAfterFrame[Index] && Player->GetTime().GetTotalSeconds()>=Target)
        { Player->Pause(); ClipStates[Index]=EClipState::Settling; ReadyAfterFrame[Index]=GFrameCounter+2; }
        if (ClipStates[Index]==EClipState::Settling && GFrameCounter>=ReadyAfterFrame[Index])
        { ClipStates[Index]=EClipState::Ready; UE_LOG(LogTemp,Display,TEXT("TITLE_VIDEO clip_ready=%d time=%.3f"),Index,Player->GetTime().GetTotalSeconds()); }
    }
    if (BlendClip!=INDEX_NONE)
    {
        const float Alpha=FMath::Clamp(static_cast<float>((FPlatformTime::Seconds()-BlendStartedAt)/FMath::Max(.001f,FMath::Clamp(LoopBlendSeconds,0.f,.25f))),0.f,1.f);
        if (BlendImage) BlendImage->SetRenderOpacity(Alpha);
        if (Alpha>=1) FinishBlend();
    }
    if (bStartRequested && !bEntrancePlaying && BlendClip==INDEX_NONE && ClipStates[2]==EClipState::Ready) BeginBlend(2);
    if (ActiveClip==INDEX_NONE && ClipStates[0]==EClipState::Ready) BeginBlend(0);
    if (!Video) return;
    const double Position=Video->GetPlayer()->GetTime().GetTotalSeconds();
    if (bEntrancePlaying && !bEntranceVideoFinished && Clips[2]->GetPlayer()->GetTime().GetTotalSeconds()>=EntranceEndSeconds) HandleMediaEnded();
    // WMF duration can include a short audio/container tail after the final
    // decoded video frame. Start the prepared swap before that tail freezes.
    else if (!bEntrancePlaying && BlendClip==INDEX_NONE &&
        (Position>=IdleEndSeconds-FMath::Clamp(LoopBlendSeconds,0.f,.25f)-.15 ||
         (!Video->GetPlayer()->IsPlaying() && Position>IdleStartSeconds+.5)))
        BeginBlend(1-ActiveClip);
}
void UOBTitleWidget::HandleOpenFireStart(const FOBOpenFireState& State,bool bFocused)
{
    if (const auto* Settings=GetGameInstance()?GetGameInstance()->GetSubsystem<UOBDeviceSettingsSubsystem>():nullptr; Settings && Settings->IsOpen())
    { bOpenFireStartArmed=false; return; }
    if (!bFocused || !State.IsFresh(FPlatformTime::Seconds())) { bOpenFireStartArmed=false; return; }
    if (OpenFireSession!=State.Frame.Session)
    { OpenFireSession=State.Frame.Session; LastOpenFireSequence=-1; bOpenFireStartArmed=false; }
    if (LastOpenFireSequence==State.Frame.Sequence) return;
    LastOpenFireSequence=State.Frame.Sequence;
    if (!bOpenFireStartArmed) { bOpenFireStartArmed=!State.Frame.bInteractionTrigger; return; }
    if (State.Events.bInteractionPressed && !bStartRequested)
    {
        UE_LOG(LogTemp,Display,TEXT("OPENFIRE_TITLE_START sequence=%d"),State.Frame.Sequence);
        RequestStart();
    }
}
void UOBTitleWidget::HandleOpenFireInput(const FOBOpenFireEvents&)
{
    const auto* Receiver=GetGameInstance()?GetGameInstance()->GetSubsystem<UOBOpenFireSubsystem>():nullptr;
    const auto* Player=GetOwningPlayer();
    const auto* Local=Player?Player->GetLocalPlayer():nullptr;
    const bool bFocused=Local && Local->ViewportClient && Local->ViewportClient->Viewport &&
        Local->ViewportClient->Viewport->IsForegroundWindow();
    if (Receiver) HandleOpenFireStart(Receiver->GetState(),bFocused);
}
void UOBTitleWidget::FinishEntrance()
{
    if (!IsEntranceTravelReady(FPlatformTime::Seconds())) return;
    bNavigationRequested=true;
    Video->GetPlayer()->Pause();
    UE_LOG(LogTemp,Display,TEXT("TITLE_VIDEO entrance_finished time=%.3f after_voice=%.3f video_finished=%d"),Video->GetPlayer()->GetTime().GetTotalSeconds(),FPlatformTime::Seconds()-EntranceVoiceFinishedAt,bEntranceVideoFinished);
    OnStartRequested.Broadcast(); OnTrainingRequested.Broadcast();
    UGameplayStatics::OpenLevelBySoftObjectPtr(this,TrainingMap);
}
bool UOBTitleWidget::IsEntranceTravelReady(double Now) const
{
    return bEntrancePlaying && bEntranceVoiceFinished && !bNavigationRequested && Now>=TrainingTravelAt;
}
FReply UOBTitleWidget::NativeOnPreviewMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event)
{
    if (Event.GetEffectingButton()==EKeys::LeftMouseButton) { RequestStart(); return FReply::Handled(); }
    return Super::NativeOnPreviewMouseButtonDown(Geometry,Event);
}

void UOBTitleWidget::RequestStart()
{
    if (bStartRequested || bNavigationRequested) return;
    bStartRequested=true;
    if (PressTextFadeInOut) StopAnimation(PressTextFadeInOut);
    for (auto* Image : {LogoImage.Get(),TitleImage.Get()}) if (Image) Image->SetVisibility(ESlateVisibility::Hidden);
    if (PressText) PressText->SetVisibility(ESlateVisibility::Hidden);
    if (bPlaybackFailed || Clips.Num()!=3) OpenVideo();
    else if (BlendClip==INDEX_NONE && ClipStates[2]==EClipState::Ready) BeginBlend(2);
}

void UOBTitleWidget::RequestQuit()
{
    OnQuitRequested.Broadcast();
}
bool UOBTitleWidget::CanStartMission() const
{
    const auto* Instance = GetGameInstance();
    const auto* Progress = Instance ? Instance->GetSubsystem<UOBTrainingProgressSubsystem>() : nullptr;
    return Progress && Progress->HasCompletedTraining();
}
void UOBTitleWidget::RequestMission() { if (CanStartMission()) OnMissionRequested.Broadcast(); }
void UOBTitleWidget::RequestTraining() { RequestStart(); }

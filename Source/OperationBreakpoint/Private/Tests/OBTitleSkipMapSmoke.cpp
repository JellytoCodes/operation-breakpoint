#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Widget/OBTitleWidget.h"
#include "Widget/OBUIVideo.h"
#include "Widget/OBTrainingSkipWidget.h"
#include "Widget/OBHUD.h"
#include "Widget/OBPlayerOverlayWidget.h"
#include "Core/OBPlayerController.h"
#include "Training/OBTrainingDirector.h"
#include "Blueprint/WidgetTree.h"
#include "MediaPlayer.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "InputKeyEventArgs.h"
#include "UnrealClient.h"
#include "Editor.h"
#include "LevelEditor.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Slate/WidgetRenderer.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "RenderingThread.h"
#include "UObject/StrongObjectPtr.h"

class FOBTitleSkipMapSmoke
{
    struct FState { double Started=FPlatformTime::Seconds(),SkipSeenAt=0,StoppedSince=0; int32 Phase=0; bool bVoice=false,bFade=false,bTitleCaptured=false,bSkipCaptured=false; };
    static void Capture(UUserWidget* Source,const TCHAR* Name,FIntPoint Size)
    {
        TStrongObjectPtr<UUserWidget> Clone(NewObject<UUserWidget>(GetTransientPackage(),Source->GetClass()));
        Clone->SetDesignerFlags(EWidgetDesignFlags::Designing|EWidgetDesignFlags::ExecutePreConstruct); Clone->Initialize();
        const auto Slate=Clone->TakeWidget(); Clone->SetVisibility(ESlateVisibility::HitTestInvisible);
        Clone->WidgetTree->ForEachWidget([&](UWidget* W) {
            auto* Live=Source->WidgetTree->FindWidget(W->GetFName()); if (!Live) return;
            W->SetVisibility(Live->GetVisibility());
            if (auto* Image=Cast<UImage>(W)) Image->SetBrush(CastChecked<UImage>(Live)->GetBrush());
            if (auto* Text=Cast<UTextBlock>(W)) Text->SetText(CastChecked<UTextBlock>(Live)->GetText());
            if (auto* Bar=Cast<UProgressBar>(W)) Bar->SetPercent(.5f);
        });
        FWidgetRenderer Renderer(true); Renderer.DrawWidget(Slate,FVector2D(Size)); FlushRenderingCommands();
        auto* Target=Renderer.DrawWidget(Slate,FVector2D(Size)); TArray<FColor> Pixels;
        if (!Target || !Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels)) return;
        TArray64<uint8> PNG; FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);
        FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/TEXT("Tests/TitleSkip")/Name));
    }
    static void Finish(bool bOK)
    {
        UE_LOG(LogTemp,Display,TEXT("TITLE_SKIP_MAP outcome=%s"),bOK ? TEXT("PASS") : TEXT("FAIL"));
        if (GEditor && GEditor->PlayWorld) GEditor->RequestEndPlayMap();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([bOK](float) {
            FPlatformMisc::RequestExitWithStatus(false,bOK ? 0 : 1); return false;
        }),1.f);
    }
public:
    static void Start()
    {
        if (!GEditor) return;
        const bool bTutorialOnly=FParse::Param(FCommandLine::Get(),TEXT("OBTestTutorialSkipOnly"));
        FRequestPlaySessionParams Params;
        Params.GlobalMapOverride=bTutorialOnly ? TEXT("/Game/Maps/TutorialMap") : TEXT("/Game/Maps/TitleMap");
        Params.bAllowOnlineSubsystem=false;
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport(); GEditor->RequestPlaySession(Params);
        auto State=MakeShared<FState>();
        if (bTutorialOnly) State->Phase=2;
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float) {
            if (FPlatformTime::Seconds()-State->Started>90) { UE_LOG(LogTemp,Error,TEXT("TITLE_SKIP_MAP timeout phase=%d"),State->Phase); Finish(false); return false; }
            auto* World=GEditor ? GEditor->PlayWorld.Get() : nullptr; if (!World) return true;
            auto* PC=Cast<AOBPlayerController>(World->GetFirstPlayerController()); if (!PC || !PC->GetOBHUD()) return true;
            const FString Map=UWorld::RemovePIEPrefix(World->GetOutermost()->GetName());
            if (State->Phase<=1)
            {
                if (Map==TEXT("/Game/Maps/TutorialMap"))
                {
                    if (State->Phase!=1 || !State->bVoice) { Finish(false); return false; }
                    State->Phase=2; UE_LOG(LogTemp,Display,TEXT("TITLE_SKIP_MAP entered_tutorial voice=%d"),State->bVoice);
                }
                else
                {
                    auto* Title=PC->GetOBHUD()->GetTitleWidget(); if (!Title || !Title->Video) return true;
                    if (!State->Phase && Title->Video && !Title->bEntrancePlaying && Title->ActiveClip!=INDEX_NONE
                        && Title->Video->GetPlayer()->GetTime().GetTotalSeconds()>Title->IdleEndSeconds+.15)
                    { UE_LOG(LogTemp,Error,TEXT("TITLE_SKIP_MAP visible idle video passed the loop boundary")); Finish(false); return false; }
                    if (!State->Phase && Title->Video && !Title->bEntrancePlaying)
                    {
                        if (!Title->Video->GetPlayer()->IsPlaying())
                        {
                            if (!State->StoppedSince) State->StoppedSince=FPlatformTime::Seconds();
                            // A rate-change event may arrive one frame after Play; a seek stall must not.
                            if (FPlatformTime::Seconds()-State->StoppedSince>.2)
                            { UE_LOG(LogTemp,Error,TEXT("TITLE_SKIP_MAP visible idle video paused for over 200ms: time=%.3f duration=%.3f active=%d blend=%d standby_state=%d"),Title->Video->GetPlayer()->GetTime().GetTotalSeconds(),Title->Video->GetPlayer()->GetDuration().GetTotalSeconds(),Title->ActiveClip,Title->BlendClip,static_cast<int32>(Title->ClipStates[1-Title->ActiveClip])); Finish(false); return false; }
                        }
                        else State->StoppedSince=0;
                    }
                    if (!State->Phase && Title->IdleLoopCount>=3 && !Title->bSeeking && Title->BlendClip==INDEX_NONE)
                    {
                        if (!State->bTitleCaptured) { Capture(Title,TEXT("TitleIdle.png"),FIntPoint(1920,1080)); State->bTitleCaptured=true; }
                        // Capture idle on one frame before injecting the actual widget mouse handler.
                        if (Title->Video->GetPlayer()->GetTime().GetTotalSeconds()<Title->IdleStartSeconds+.15) return true;
                        FPointerEvent Click(0,FVector2D(960,540),FVector2D(960,540),TSet<FKey>{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState());
                        Title->NativeOnPreviewMouseButtonDown(FGeometry(),Click); Title->RequestStart(); State->Phase=1;
                        const double EntranceTime=Title->Clips[2]->GetPlayer()->GetTime().GetTotalSeconds();
                        const double EntranceStart=Title->EntranceStartSeconds;
                        if (!Title->bEntrancePlaying || Title->ActiveClip!=2 || EntranceTime<EntranceStart-.04 || EntranceTime>EntranceStart+.15)
                        { UE_LOG(LogTemp,Error,TEXT("TITLE_SKIP_MAP entrance did not start at %.3fs: %.3f"),EntranceStart,EntranceTime); Finish(false); return false; }
                        UE_LOG(LogTemp,Display,TEXT("TITLE_SKIP_MAP immediate_noise_start=%.3f ping_pong_cycles=%d"),EntranceTime,Title->IdleLoopCount);
                    }
                    if (State->Phase==1 && Title->bEntrancePlaying)
                        State->bVoice |= Title->Voice && Title->Voice->IsPlaying() && Title->EntranceSource.ToSoftObjectPath().GetAssetName()==TEXT("TravleToTraining");
                    return true;
                }
            }
            if (State->Phase==2)
            {
                for (TActorIterator<AOBTrainingDirector> It(World);It;++It)
                {
                    auto* D=*It; State->bFade |= D->bPrototypeIntroPending && D->IntroBrightness<1;
                    if (D->CurrentDialogueId==1) { Finish(false); return false; }
                    if (D->CurrentDialogueId!=2 || !D->IsInitialSkipAvailable() || !D->Overlay) return true;
                    auto* Skip=Cast<UOBTrainingSkipWidget>(D->Overlay->WidgetTree->FindWidget(TEXT("TrainingSkip")));
                    if (!Skip || !Skip->IsAvailable()) return true;
                    const auto* Body=Cast<UTextBlock>(Skip->WidgetTree->FindWidget(TEXT("BodyText")));
                    if (!Body || !Body->GetText().ToString().StartsWith(TEXT("2초간")))
                    { UE_LOG(LogTemp,Error,TEXT("TITLE_SKIP_MAP two-second prompt missing")); Finish(false); return false; }
                    if (!State->SkipSeenAt) State->SkipSeenAt=FPlatformTime::Seconds();
                    if (FPlatformTime::Seconds()-State->SkipSeenAt<1) return true;
                    if (!State->bSkipCaptured) { Capture(Skip,TEXT("TrainingSkip.png"),FIntPoint(240,330)); State->bSkipCaptured=true; }
                    if (FPlatformTime::Seconds()-State->SkipSeenAt<1.25) return true;
                    auto* Viewport=World->GetGameViewport()->Viewport;
                    auto Key=[&](EInputEvent Event) { PC->InputKey(FInputKeyEventArgs(Viewport,FInputDeviceId::CreateFromInternalId(0),EKeys::LeftMouseButton,Event,FPlatformTime::Cycles64())); };
                    Key(IE_Pressed); Skip->AdvanceHold(1.99f);
                    if (!D->bInitialSkipOpen || Skip->GetHoldProgress()<=0.f) { Finish(false); return false; }
                    Key(IE_Released);
                    if (Skip->GetHoldProgress()!=0) { Finish(false); return false; }
                    Key(IE_Pressed); Skip->AdvanceHold(2.f);
                    if (D->bInitialSkipOpen || D->GetTrainingResult().bPassed || !State->bFade) { Finish(false); return false; }
                    State->Phase=3;
                    UE_LOG(LogTemp,Display,TEXT("TITLE_SKIP_MAP hold_completed fade_preserved=%d"),State->bFade);
                    return true;
                }
            }
            if (State->Phase==3 && Map==TEXT("/Game/Maps/Stage_01")) { Finish(true); return false; }
            return true;
        }),.05f);
    }
};
static FAutoConsoleCommand GTitleSkipMapSmoke(TEXT("OB.UI.TitleSkipPIE"),TEXT("Verify title media loop, mouse start, entry audio, TutorialMap fade and hold-to-skip travel, then exit."),FConsoleCommandDelegate::CreateStatic(&FOBTitleSkipMapSmoke::Start));
#endif

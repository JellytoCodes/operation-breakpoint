#if WITH_DEV_AUTOMATION_TESTS && !WITH_EDITOR && !UE_BUILD_SHIPPING && !UE_BUILD_TEST
#include "Widget/OBTitleWidget.h"
#include "Widget/OBUIVideo.h"
#include "Widget/OBHUD.h"
#include "Core/OBPlayerController.h"
#include "Training/OBTrainingDirector.h"
#include "MediaPlayer.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "UnrealClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "AudioMixerBlueprintLibrary.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProperties.h"
#include "Misc/Paths.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/InputSettings.h"
#include "Scalability.h"

// Opt-in integration check: never runs unless explicitly requested on the console.
// Input travels through Slate hit testing; RequestStart is not called by the test.
class FOBTitlePackageSmoke
{
    struct FState
    {
        double Started=FPlatformTime::Seconds(),StoppedSince=0,ClickedAt=0,VoiceFinishedAt=0;
        int32 Phase=0;
        bool bForward=false,bReverse=false,bVoice=false,bRecording=false,bTutorialShot=false,bFullscreenChecked=false,bTravelChecked=false;
        float AudioPeak=0;
    };
    static FString OutputDir()
    { return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Tests/TitlePackage")); }
    static void Shot(const TCHAR* Name)
    { FScreenshotRequest::RequestScreenshot(OutputDir()/Name,true,false); }
    static bool HasShot(const TCHAR* Name)
    { return IFileManager::Get().FileSize(*(OutputDir()/Name))>4096; }
    static bool Finish(bool bOK,const TCHAR* Reason)
    {
        UE_LOG(LogTemp,Display,TEXT("TITLE_PACKAGE_SMOKE outcome=%s reason=%s cooked=%d"),bOK ? TEXT("PASS") : TEXT("FAIL"),Reason,FPlatformProperties::RequiresCookedData());
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([bOK](float) {
            FPlatformMisc::RequestExitWithStatus(false,bOK ? 0 : 1); return false;
        }),1.f);
        return false;
    }
public:
    static void Start()
    {
        if (!FPlatformProperties::RequiresCookedData()) { Finish(false,TEXT("Requires an actual cooked executable")); return; }
        IFileManager::Get().MakeDirectory(*OutputDir(),true);
        auto State=MakeShared<FState>();
        UE_LOG(LogTemp,Display,TEXT("TITLE_PACKAGE_SMOKE started output=%s"),*OutputDir());
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float) {
            const double Now=FPlatformTime::Seconds();
            if (Now-State->Started>180) return Finish(false,TEXT("Timeout"));
            UWorld* World=nullptr;
            for (const auto& Context : GEngine->GetWorldContexts())
                if (Context.WorldType==EWorldType::Game && Context.World()) { World=Context.World(); break; }
            if (!World || !World->HasBegunPlay()) return true;
            auto* PC=Cast<AOBPlayerController>(World->GetFirstPlayerController());
            if (!PC || !PC->GetOBHUD()) return true;
            if (!State->bFullscreenChecked)
            {
                auto Window=World->GetGameViewport()->GetWindow();
                auto* Settings=GEngine->GetGameUserSettings();
                const auto* Input=GetDefault<UInputSettings>();
                if (!Window || !Settings || Window->GetWindowMode()!=EWindowMode::WindowedFullscreen || Settings->GetFullscreenMode()!=EWindowMode::WindowedFullscreen)
                    return Finish(false,TEXT("Game did not start in fixed borderless fullscreen"));
                if (Input->bAltEnterTogglesFullscreen || Input->bF11TogglesFullscreen)
                    return Finish(false,TEXT("Fullscreen toggle shortcuts remain enabled"));
                if (World->GetGameViewport()->TryToggleFullscreenOnInputKey(EKeys::F11,IE_Pressed) || Window->GetWindowMode()!=EWindowMode::WindowedFullscreen)
                    return Finish(false,TEXT("F11 changed the fullscreen window"));
                UE_LOG(LogTemp,Display,TEXT("TITLE_PACKAGE_SMOKE borderless=1 saved_mode=%d window_mode=%d shortcuts_blocked=1"),static_cast<int32>(Settings->GetFullscreenMode()),static_cast<int32>(Window->GetWindowMode()));
                Scalability::FQualityLevels Medium; Medium.SetFromSingleQualityLevel(1);
                const auto Actual=Scalability::GetQualityLevels();
                if (Settings->GetOverallScalabilityLevel()!=1 || Actual.GetSingleQualityLevel()!=1 || !FMath::IsNearlyEqual(Actual.ResolutionQuality,Medium.ResolutionQuality,.01f))
                    return Finish(false,TEXT("Medium preset was not applied to settings and live quality groups"));
                auto* Cloud=IConsoleManager::Get().FindConsoleVariable(TEXT("r.VolumetricCloud.ViewRaySampleMaxCount"));
                auto* CloudShadow=IConsoleManager::Get().FindConsoleVariable(TEXT("r.VolumetricCloud.Shadow.ViewRaySampleMaxCount"));
                if (!Cloud || !CloudShadow || Cloud->GetInt()!=48 || CloudShadow->GetInt()!=6)
                    return Finish(false,TEXT("Medium cloud overrides did not apply"));
                UE_LOG(LogTemp,Display,TEXT("TITLE_PACKAGE_SMOKE medium=1 stored_quality=1 live_quality=1 resolution_quality=%.3f cloud=48 cloud_shadow=6"),Actual.ResolutionQuality);
                State->bFullscreenChecked=true;
            }
            const FString Map=World->GetMapName();
            if (Map==TEXT("TutorialMap"))
            {
                if (!State->bTravelChecked)
                {
                    const double Delay=Now-State->VoiceFinishedAt;
                    if (!State->VoiceFinishedAt || Delay<.95 || Delay>3)
                        return Finish(false,TEXT("Travel did not follow entrance voice completion plus one second"));
                    UE_LOG(LogTemp,Display,TEXT("TITLE_PACKAGE_SMOKE voice_to_tutorial=%.3f"),Delay);
                    State->bTravelChecked=true;
                }
                if (Scalability::GetQualityLevels().GetSingleQualityLevel()!=1) return Finish(false,TEXT("Medium preset changed during map travel"));
                if (State->Phase!=1 || !State->bVoice || State->bRecording || State->AudioPeak<.00001f)
                    return Finish(false,TEXT("Tutorial arrived without verified entrance audio"));
                for (TActorIterator<AOBTrainingDirector> It(World);It;++It)
                {
                    if (It->CurrentDialogueId!=2 || !It->IsInitialSkipAvailable()) return true;
                    if (!State->bTutorialShot) { Shot(TEXT("TutorialMap.png")); State->bTutorialShot=true; return true; }
                    if (!HasShot(TEXT("TutorialMap.png"))) return true;
                    UE_LOG(LogTemp,Display,TEXT("TITLE_PACKAGE_SMOKE tutorial_ready dialogue=%d peak=%.6f"),It->CurrentDialogueId,State->AudioPeak);
                    return Finish(true,TEXT("3 full TitleLoop cycles, separate entrance movie, rendered audio and TutorialMap"));
                }
                return true;
            }
            if (Map!=TEXT("TitleMap")) return Finish(false,TEXT("Unexpected startup map"));
            auto* Title=PC->GetOBHUD()->GetTitleWidget();
            if (!Title) return true;
            if (Title->bPlaybackFailed) return Finish(false,TEXT("Title media failed to open"));
            if (!Title->Video) return true;
            const double Position=Title->Video->GetPlayer()->GetTime().GetTotalSeconds();
            if (!State->Phase)
            {
                if (!Title->Video->GetPlayer()->IsPlaying())
                {
                    if (!State->StoppedSince) State->StoppedSince=Now;
                    if (Now-State->StoppedSince>.2) return Finish(false,TEXT("Visible idle player paused for over 200ms"));
                }
                else State->StoppedSince=0;
                if (Position>Title->IdleEndSeconds+.15) return Finish(false,TEXT("Missed idle loop boundary"));
                if (!State->bForward && Position>Title->IdleStartSeconds+.5)
                { Shot(TEXT("IdleForward.png")); State->bForward=true; }
                if (!State->bReverse && Position>Title->IdleEndSeconds*.6)
                { Shot(TEXT("IdleReverse.png")); State->bReverse=true; }
                if (Title->IdleLoopCount<3 || Title->BlendClip!=INDEX_NONE || Title->ClipStates[2]!=UOBTitleWidget::EClipState::Ready) return true;
                if (!HasShot(TEXT("IdleForward.png")) || !HasShot(TEXT("IdleReverse.png"))) return Finish(false,TEXT("Idle screenshots missing"));
                auto Window=World->GetGameViewport()->GetWindow();
                if (!Window || !Window->GetNativeWindow()) return Finish(false,TEXT("No game window for Slate input"));
                const FVector2D Cursor=Window->GetPositionInScreen()+Window->GetSizeInScreen()*.5;
                FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::SetDirectly);
                UAudioMixerBlueprintLibrary::StartRecordingOutput(World,2.f);
                UAudioMixerBlueprintLibrary::StartAnalyzingOutput(World);
                State->bRecording=true; State->ClickedAt=Now;
                FPointerEvent Down(0,Cursor,Cursor,TSet<FKey>{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState());
                FPointerEvent Up(0,Cursor,Cursor,TSet<FKey>{},EKeys::LeftMouseButton,0,FModifierKeysState());
                FSlateApplication::Get().ProcessMouseButtonDownEvent(Window->GetNativeWindow(),Down);
                FSlateApplication::Get().ProcessMouseButtonUpEvent(Up);
                const double EntranceTime=Title->Clips[2]->GetPlayer()->GetTime().GetTotalSeconds();
                if (!Title->bStartRequested || !Title->bEntrancePlaying || Title->ActiveClip!=2 || EntranceTime<0 || EntranceTime>.5 || Title->EntranceSource.ToSoftObjectPath().GetAssetName()!=TEXT("TravleToTraining"))
                    return Finish(false,TEXT("Slate trigger did not start the separate entrance movie"));
                UE_LOG(LogTemp,Display,TEXT("TITLE_PACKAGE_SMOKE slate_trigger=1 cycles=%d entrance=%.3f voice=%s"),Title->IdleLoopCount,EntranceTime,Title->Voice ? *GetPathNameSafe(Title->Voice->Sound) : TEXT("missing"));
                State->Phase=1; return true;
            }
            State->bVoice |= Title->Voice && Title->Voice->IsPlaying();
            if (Title->bEntranceVoiceFinished) State->VoiceFinishedAt=Title->EntranceVoiceFinishedAt;
            if (State->bRecording)
            {
                TArray<float> Magnitudes;
                UAudioMixerBlueprintLibrary::GetMagnitudeForFrequencies(World,{100,250,500,1000,2000,4000},Magnitudes);
                for (float Magnitude : Magnitudes) State->AudioPeak=FMath::Max(State->AudioPeak,Magnitude);
            }
            if (State->bRecording && Now-State->ClickedAt>1)
            {
                Shot(TEXT("EntranceNoise.png"));
                UAudioMixerBlueprintLibrary::StopRecordingOutput(World,EAudioRecordingExportType::WavFile,TEXT("TrainingEntrance"),OutputDir());
                UAudioMixerBlueprintLibrary::StopAnalyzingOutput(World);
                State->bRecording=false;
                UE_LOG(LogTemp,Display,TEXT("TITLE_PACKAGE_SMOKE audio_rendered peak=%.6f playing=%d"),State->AudioPeak,State->bVoice);
            }
            return true;
        }),.05f);
    }
};
static FAutoConsoleCommand GTitlePackageSmoke(TEXT("OB.UI.TitlePackageSmoke"),TEXT("Verify cooked title loop, Slate trigger, entrance video/audio and TutorialMap; record evidence and exit."),FConsoleCommandDelegate::CreateStatic(&FOBTitlePackageSmoke::Start));
#endif

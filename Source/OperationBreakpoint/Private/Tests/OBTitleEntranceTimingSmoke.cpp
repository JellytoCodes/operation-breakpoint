#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Widget/OBTitleWidget.h"
#include "Widget/OBHUD.h"
#include "Widget/OBUIVideo.h"
#include "Core/OBPlayerController.h"
#include "MediaPlayer.h"
#include "Components/AudioComponent.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Editor.h"
#include "LevelEditor.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "Input/Reply.h"

class FOBTitleEntranceTimingSmoke
{
    struct FState
    {
        double Started=FPlatformTime::Seconds(),VoiceEnded=0,TravelAt=0;
        int32 Phase=0;bool VoiceObserved=false,BlackObserved=false,CheckedHold=false;
        TWeakObjectPtr<UOBTitleWidget> Title;
    };
    static bool Finish(bool OK,const TCHAR* Why)
    {
        UE_LOG(LogTemp,Display,TEXT("TITLE_TIMING_PIE outcome=%s reason=%s"),OK?TEXT("PASS"):TEXT("FAIL"),Why);
        if(GEditor && GEditor->PlayWorld)GEditor->RequestEndPlayMap();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([OK](float){FPlatformMisc::RequestExitWithStatus(false,OK?0:1);return false;}),1.f);return false;
    }
public:
    static void Start()
    {
        FRequestPlaySessionParams Params;Params.GlobalMapOverride=TEXT("/Game/Maps/TitleMap");Params.bAllowOnlineSubsystem=false;
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();GEditor->RequestPlaySession(Params);
        auto S=MakeShared<FState>();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float){
            const double Now=FPlatformTime::Seconds();if(Now-S->Started>100)return Finish(false,TEXT("Timeout"));
            auto* W=GEditor?GEditor->PlayWorld.Get():nullptr;if(!W)return true;
            if(UWorld::RemovePIEPrefix(W->GetOutermost()->GetName()).EndsWith(TEXT("TutorialMap")))
            {
                if(!S->VoiceObserved || !S->CheckedHold || S->VoiceEnded<=0 || Now<S->TravelAt-.02)return Finish(false,TEXT("Travel occurred before one second after actual audio completion"));
                UE_LOG(LogTemp,Display,TEXT("TITLE_TIMING_PIE travel_after_voice=%.3f early_black_test=%d"),Now-S->VoiceEnded,S->BlackObserved);
                return Finish(true,TEXT("Full TitleLoop, separate entrance movie and actual voice completion plus one second"));
            }
            auto* PC=Cast<AOBPlayerController>(W->GetFirstPlayerController());auto* Title=PC && PC->GetOBHUD()?PC->GetOBHUD()->GetTitleWidget():nullptr;
            if(!Title)return true;if(Title->bPlaybackFailed)return Finish(false,TEXT("Media open failed"));
            if(S->Phase==0)
            {
                if(!Title->Video || Title->IdleLoopCount<1 || Title->ClipStates[2]!=UOBTitleWidget::EClipState::Ready)return true;
                if(!Title->Video->GetPlayer()->GetUrl().Contains(TEXT("TitleLoop.mp4")) || Title->IdleStartSeconds!=0 || Title->IdleEndSeconds<19)return Finish(false,TEXT("Idle is not the full new TitleLoop"));
                UE_LOG(LogTemp,Display,TEXT("TITLE_TIMING_PIE idle_source=%s full_duration=%.3f loops=%d entrance=%s"),*Title->Video->GetPlayer()->GetUrl(),Title->IdleEndSeconds,Title->IdleLoopCount,*Title->Clips[2]->GetPlayer()->GetUrl());
                FPointerEvent Click(0,FVector2D(960,540),FVector2D(960,540),TSet<FKey>{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState());
                Title->NativeOnPreviewMouseButtonDown(FGeometry(),Click);S->Title=Title;S->Phase=1;return true;
            }
            if(S->Phase==1)
            {
                if(!Title->bEntrancePlaying || !Title->Video->GetPlayer()->GetUrl().Contains(TEXT("TravleToTraining.mp4")))return Finish(false,TEXT("Trigger did not select TravleToTraining"));
                S->VoiceObserved|=Title->Voice && Title->Voice->IsPlaying();
                if(Title->LogoImage->IsVisible() || Title->TitleImage->IsVisible() || Title->PressText->IsVisible())return Finish(false,TEXT("Title overlay remained visible after trigger"));
                // The real new movie is longer than the current narration. Exercise
                // the other required ordering by simulating an earlier decoder end.
                if(S->VoiceObserved && Title->Video->GetPlayer()->GetTime().GetTotalSeconds()>.5 && !Title->bEntranceVoiceFinished)
                {
                    Title->HandleMediaEnded();
                    if(!Title->bEntranceVideoFinished || Title->BackgroundVideo->GetColorAndOpacity()!=FLinearColor::Black || Title->BackgroundVideo->GetBrush().GetResourceObject()!=nullptr || Title->bNavigationRequested)return Finish(false,TEXT("Early movie end did not hold black while voice plays"));
                    S->BlackObserved=true;UE_LOG(LogTemp,Display,TEXT("TITLE_TIMING_PIE simulated_early_end=1 black_hold=1 voice_still_playing=1"));S->Phase=2;
                }
                return true;
            }
            if(S->Phase==2)
            {
                if(!Title->bEntranceVoiceFinished)return true;
                S->VoiceEnded=Title->EntranceVoiceFinishedAt;S->TravelAt=Title->TrainingTravelAt;
                if(!FMath::IsNearlyEqual(S->TravelAt-S->VoiceEnded,1.,.001))return Finish(false,TEXT("Audio-to-map delay is not one second"));
                if(Now<S->TravelAt-.1)
                {
                    if(Title->bNavigationRequested || Title->BackgroundVideo->GetColorAndOpacity()!=FLinearColor::Black)return Finish(false,TEXT("Black hold was lost during post-voice delay"));
                    S->CheckedHold=true;
                }
                return true;
            }
            return true;
        }),0.f);
    }
};
static FAutoConsoleCommand GTitleEntranceTimingSmoke(TEXT("OB.UI.TitleTimingPIE"),TEXT("Verify separate title movies, voice-driven delay and early-video black hold in PIE."),FConsoleCommandDelegate::CreateStatic(&FOBTitleEntranceTimingSmoke::Start));
#endif

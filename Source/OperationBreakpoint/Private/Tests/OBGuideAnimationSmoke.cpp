#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Training/OBTrainingDirector.h"
#include "Widget/OBPlayerOverlayWidget.h"
#include "Widget/OBHudPanelWidget.h"
#include "Core/OBPlayerController.h"
#include "Animation/WidgetAnimation.h"
#include "Components/AudioComponent.h"
#include "Editor.h"
#include "LevelEditor.h"
#include "EngineUtils.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"

// Uses the user's saved Guide animations and real narration, input and callbacks.
class FOBGuideAnimationSmoke
{
    struct FState { double Start=FPlatformTime::Seconds(),OutStarted=0; int32 Phase=0; float OutDuration=0; bool OutObserved=false; };
    static bool Finish(bool OK,const TCHAR* Why)
    {
        UE_LOG(LogTemp,Display,TEXT("GUIDE_ANIMATION_PIE outcome=%s reason=%s"),OK?TEXT("PASS"):TEXT("FAIL"),Why);
        if(GEditor && GEditor->PlayWorld)GEditor->RequestEndPlayMap();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([OK](float){FPlatformMisc::RequestExitWithStatus(false,OK?0:1);return false;}),1.f);
        return false;
    }
public:
    static void Start()
    {
        FRequestPlaySessionParams Params;Params.GlobalMapOverride=TEXT("/Game/Maps/TutorialMap");Params.bAllowOnlineSubsystem=false;
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();GEditor->RequestPlaySession(Params);
        auto S=MakeShared<FState>();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float){
            const double Now=FPlatformTime::Seconds();if(Now-S->Start>100)return Finish(false,TEXT("Timeout"));
            auto* W=GEditor?GEditor->PlayWorld.Get():nullptr;if(!W)return true;
            AOBTrainingDirector* D=nullptr;for(TActorIterator<AOBTrainingDirector> It(W);It;++It)D=*It;
            if(!D || !D->IsReadyForQA() || !D->Overlay || !D->Controller)return true;
            auto* Guide=D->Overlay->GetTrainingGuideWidget();if(!Guide || !Guide->GuideFadeIn || !Guide->GuideFadeOut)return Finish(false,TEXT("Saved GuideFadeIn/GuideFadeOut not bound"));
            auto* ShowAnimation=Guide->GuideFadeOut.Get();
            auto* HideAnimation=Guide->GuideFadeIn.Get();
            if(S->Phase==0)
            {
                if(!FMath::IsNearlyEqual(D->GetPrototypeIntroDuration(),2.f))return Finish(false,TEXT("Saved map intro is not two seconds"));
                if(D->bPrototypeIntroPending)return true;
                const double Elapsed=W->GetTimeSeconds()-D->PrototypeIntroStartedAt;
                if(Elapsed<1.95 || Elapsed>2.5 || D->CurrentDialogueId==1)return Finish(false,TEXT("Intro timing or first instructor row wrong"));
                UE_LOG(LogTemp,Display,TEXT("GUIDE_ANIMATION_PIE intro_config=2 actual=%.3f first_row=%d"),Elapsed,D->CurrentDialogueId);
                D->bInitialSkipOpen=false;D->GuideTriggerDelaySeconds=.05f;
                D->PlayPrototypeLine(3);S->Phase=1;return true;
            }
            if(S->Phase==1)
            {
                if(!Guide->IsAnimationPlaying(ShowAnimation))return true;
                if(D->CurrentDialogueId!=3 || Guide->CurrentUIId!=3001 || !D->PrototypeNarrationAudio || !D->PrototypeNarrationAudio->IsPlaying())return Finish(false,TEXT("Show animation did not begin with narration"));
                S->OutDuration=HideAnimation->GetEndTime()-HideAnimation->GetStartTime();
                UE_LOG(LogTemp,Display,TEXT("GUIDE_ANIMATION_PIE fade_in_with_voice=1 in_seconds=%.3f out_seconds=%.3f"),ShowAnimation->GetEndTime()-ShowAnimation->GetStartTime(),S->OutDuration);
                S->Phase=2;return true;
            }
            if(S->Phase==2)
            {
                if(!D->bGuideAdvanceReady)return true;
                D->Controller->FireAtCursor();
                if(!Guide->IsAnimationPlaying(HideAnimation) || D->CurrentDialogueId!=3 || D->bGuideAdvanceReady)return Finish(false,TEXT("Trigger did not defer progression to the hide animation"));
                S->OutStarted=Now;S->Phase=3;return true;
            }
            if(S->Phase==3)
            {
                if(Guide->IsAnimationPlaying(HideAnimation))
                {
                    S->OutObserved=true;
                    if(D->CurrentDialogueId!=3 || Guide->CurrentUIId!=3001 || D->PrototypeNarrationAudio)return Finish(false,TEXT("Next content/voice started before the hide animation completed"));
                    D->Controller->FireAtCursor();return true;
                }
                if(D->CurrentDialogueId!=4)return true;
                if(!S->OutObserved || Now-S->OutStarted<S->OutDuration-.05 || Guide->CurrentUIId!=3002)return Finish(false,TEXT("Guide page changed before its animation duration"));
                if(!Guide->IsAnimationPlaying(ShowAnimation))return true;
                if(!D->PrototypeNarrationAudio || !D->PrototypeNarrationAudio->IsPlaying())return Finish(false,TEXT("Next guide animation/voice not synchronized"));
                UE_LOG(LogTemp,Display,TEXT("GUIDE_ANIMATION_PIE page_3001_to_3002 deferred=1 repeated_trigger_ignored=1 elapsed=%.3f"),Now-S->OutStarted);
                return Finish(true,TEXT("Two-second intro, authored fades with voice, progression after fade-out"));
            }
            return true;
        }),0.f);
    }
};
static FAutoConsoleCommand GGuideAnimationSmoke(TEXT("OB.UI.GuideAnimationPIE"),TEXT("Verify saved Guide fades, voice/trigger ordering and two-second Tutorial intro."),FConsoleCommandDelegate::CreateStatic(&FOBGuideAnimationSmoke::Start));
#endif

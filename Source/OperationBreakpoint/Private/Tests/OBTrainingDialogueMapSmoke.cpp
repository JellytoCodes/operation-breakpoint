#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Training/OBTrainingDirector.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Core/OBPlayerController.h"
#include "Widget/OBPlayerOverlayWidget.h"
#include "Widget/OBTrainingSkipWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/AudioComponent.h"
#include "Animation/WidgetAnimation.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "LevelEditor.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "TimerManager.h"

// Runs the saved TutorialMap, authored UMG tracks and real audio together through entry to A practice.
class FOBTrainingDialogueMapSmoke
{
public:
    static void Finish(bool bSuccess)
    {
        UE_LOG(LogTemp,Display,TEXT("DIALOGUE_MAP outcome=%s"),bSuccess ? TEXT("PASS") : TEXT("FAIL"));
        if (GEditor && GEditor->PlayWorld) GEditor->RequestEndPlayMap();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([bSuccess](float) {
            FPlatformMisc::RequestExitWithStatus(false,bSuccess ? 0 : 1); return false;
        }),1.f);
    }
    struct FState
    {
        double StartedAt=0;
        bool bInitialSnap=false, bFadeChecked=false;
        TSet<int32> FadeIns, Voices, FadeOuts;
        int32 TriggerId=0;
        FTimerHandle Timer;
    };
    static void Run(UWorld* World)
    {
        auto State=MakeShared<FState>(); State->StartedAt=World->GetTimeSeconds();
        TWeakObjectPtr<UWorld> WeakWorld=World;
        World->GetTimerManager().SetTimer(State->Timer,FTimerDelegate::CreateLambda([State,WeakWorld]() {
            auto* W=WeakWorld.Get(); if (!W) return;
            AOBTrainingDirector* D=nullptr;
            for (TActorIterator<AOBTrainingDirector> It(W);It;++It) { D=*It; break; }
            auto Fail=[&]() { W->GetTimerManager().ClearTimer(State->Timer); Finish(false); };
            if (W->GetTimeSeconds()-State->StartedAt>120) { Fail(); return; }
            if (!D || !D->bInitialized || !D->Overlay) return;
            if (!State->bInitialSnap)
            {
                const float Yaw=D->Controller->GetPawn()->GetActorRotation().Yaw;
                State->bInitialSnap=D->bPrototypeIntroPending && !D->bPrototypeTurning &&
                    FMath::Abs(FMath::FindDeltaAngleDegrees(Yaw,D->PrototypeSignArrivalYaw-90.f))<.01f;
                UE_LOG(LogTemp,Display,TEXT("DIALOGUE_MAP initial_snap=%d yaw=%.2f fade_duration=%.2f"),State->bInitialSnap,Yaw,D->GetPrototypeIntroDuration());
                if (!State->bInitialSnap) { Fail(); return; }
            }
            if (D->bPrototypeIntroPending)
            {
                if (D->CurrentDialogueId!=0 || D->PrototypeNarrationAudio || D->bPrototypeTurning) { Fail(); return; }
                if (W->GetTimeSeconds()-D->PrototypeIntroStartedAt>D->IntroFadeSeconds) State->bFadeChecked=true;
                return;
            }
            const int32 Id=D->CurrentDialogueId;
            if (Id==1) { Fail(); return; }
            auto* O=D->Overlay.Get();
            const auto Phase=D->bPrototypeMessageUsesSystem ? O->SystemMessagePhase : O->SubtitlePhase;
            using EPhase=UOBPlayerOverlayWidget::ESystemMessagePhase;
            if (Id>=2 && Id<=8)
            {
                if (Phase==EPhase::FadingIn)
                {
                    State->FadeIns.Add(Id);
                    if (D->PrototypeNarrationAudio) { Fail(); return; }
                }
                if (Phase==EPhase::Holding && D->PrototypeNarrationAudio && D->PrototypeNarrationAudio->IsPlaying()) State->Voices.Add(Id);
                if (Phase==EPhase::FadingOut) State->FadeOuts.Add(Id);
                if (D->bGuideAdvanceReady && State->TriggerId!=Id)
                {
                    if (Id==3)
                    {
                        auto* Skip=Cast<UOBTrainingSkipWidget>(O->WidgetTree->FindWidget(TEXT("TrainingSkip")));
                        if (!Skip || Skip->IsAvailable() || D->IsInitialSkipAvailable()) { Fail(); return; }
                        UE_LOG(LogTemp,Display,TEXT("DIALOGUE_MAP initial_skip_closed_at_guide_ready=1"));
                    }
                    State->TriggerId=Id;
                    D->Controller->OnTriggerPressed.Broadcast();
                    if (D->CurrentDialogueId!=Id || D->bGuideAdvanceReady) { Fail(); return; }
                }
            }
            if (D->CurrentStep==EOBTrainingStep::WeaponCheck && D->PrototypeRouteIndex==1)
            {
                const bool bSuccess=State->bInitialSnap && State->bFadeChecked && State->FadeIns.Num()==7 && State->Voices.Num()==7 && State->FadeOuts.Num()==7;
                UE_LOG(LogTemp,Display,TEXT("DIALOGUE_MAP entered_practice fade_ins=%d voices=%d fade_outs=%d doubled_intro=%d point=%d"),
                    State->FadeIns.Num(),State->Voices.Num(),State->FadeOuts.Num(),State->bFadeChecked,D->Spline->GetCurrentPointIndex());
                W->GetTimerManager().ClearTimer(State->Timer); Finish(bSuccess);
            }
        }),.05f,true);
    }
};
static FAutoConsoleCommand GOBDialogueMapPIE(TEXT("OB.Training.DialoguePIE"),TEXT("Verify saved TutorialMap intro snap and authored dialogue fade/audio lifecycle in PIE; exits its process."),
    FConsoleCommandDelegate::CreateLambda([]() {
        if (!GEditor) return;
        FRequestPlaySessionParams Params; Params.GlobalMapOverride=TEXT("/Game/Maps/TutorialMap");
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        Params.bAllowOnlineSubsystem=false; GEditor->RequestPlaySession(Params);
        const double Start=FPlatformTime::Seconds();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Start](float) {
            if (GEditor && GEditor->PlayWorld) { FOBTrainingDialogueMapSmoke::Run(GEditor->PlayWorld); return false; }
            if (FPlatformTime::Seconds()-Start>30) { FPlatformMisc::RequestExitWithStatus(false,1); return false; }
            return true;
        }),.05f);
    }));
#endif

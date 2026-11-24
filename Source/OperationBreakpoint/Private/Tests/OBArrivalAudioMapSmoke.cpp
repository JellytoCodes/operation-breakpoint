#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Editor.h"
#include "LevelEditor.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Character/OBActionComponent.h"
#include "Character/OBEnemyCharacter.h"
#include "Components/AudioComponent.h"
#include "Stage/OBStageFlowComponent.h"

class FOBArrivalAudioMapSmoke
{
public:
    static void Start()
    {
        FRequestPlaySessionParams Params;Params.GlobalMapOverride=TEXT("/Game/Maps/Stage_01");Params.bAllowOnlineSubsystem=false;
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        GEditor->RequestPlaySession(Params);
        const double Began=FPlatformTime::Seconds();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Began](float) {
            auto* W=GEditor ? GEditor->PlayWorld.Get() : nullptr;
            auto* PC=W ? W->GetFirstPlayerController() : nullptr;
            auto* P=PC ? Cast<AOBPlayerCharacter>(PC->GetPawn()) : nullptr;
            if (P && W->GetTimeSeconds()>.8f && P->GetPlayerSplineComponent()->GetRailPath()) { Run(W,P);return false; }
            if (FPlatformTime::Seconds()-Began>100) { FPlatformMisc::RequestExitWithStatus(false,1);return false; }
            return true;
        }),.05f);
    }
private:
    static void Run(UWorld* W,AOBPlayerCharacter* P)
    {
        struct FState { FTimerHandle Timer;TMap<int32,float> CueAt,CombatAt;TSet<int32> Arrived;bool OK=true,Breached=false;int32 PreviousId=0;float Zone6TravelAt=-1;FString Report; };
        auto S=MakeShared<FState>();TWeakObjectPtr<UWorld> WeakW=W;TWeakObjectPtr<AOBPlayerCharacter> WeakP=P;
        const FString Out=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Tests/ArrivalAudio/PIE"));
        IFileManager::Get().MakeDirectory(*Out,true);
        P->SetCanBeDamaged(false);P->GetStageFlowComponent()->FinishOpening();P->GetStageFlowComponent()->StageRecord.bUsedTestInputs=true;
        W->GetTimerManager().SetTimer(S->Timer,FTimerDelegate::CreateLambda([S,WeakW,WeakP,Out]() {
            auto* World=WeakW.Get();auto* Player=WeakP.Get();if (!World || !Player) return;
            auto* F=Player->GetStageFlowComponent();auto* R=Player->GetPlayerSplineComponent();auto* A=Player->GetActionComponent();
            auto Check=[&](bool OK,const FString& Message) { S->OK &= OK;S->Report+=(OK ? TEXT("PASS ") : TEXT("FAIL "))+Message+TEXT("\n"); };
            auto Finish=[&]() {
                S->Report+=FString::Printf(TEXT("outcome=%s arrivals=%d\n"),S->OK ? TEXT("PASS") : TEXT("FAIL"),S->Arrived.Num());
                FFileHelper::SaveStringToFile(S->Report,*(Out/TEXT("verification.txt")));
                UE_LOG(LogTemp,Display,TEXT("ARRIVAL_AUDIO_PIE\n%s"),*S->Report);
                World->GetTimerManager().ClearTimer(S->Timer);GEditor->RequestEndPlayMap();
                const bool OK=S->OK;
                FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([OK](float) {
                    if (GEditor && GEditor->PlayWorld) return true;
                    FPlatformMisc::RequestExitWithStatus(false,OK ? 0 : 1);return false;
                }),.25f);
            };
            if (World->GetTimeSeconds()>420 || F->Phase==EOBStagePhase::Ending || F->Phase==EOBStagePhase::Complete)
            { Check(false,TEXT("Normal six-zone approach deadline"));Finish();return; }
            const float Now=World->GetTimeSeconds();const int32 Id=F->ActiveScriptId;
            if (S->Zone6TravelAt<0 && R->IsMoving() && R->GetCurrentPointIndex()==83) S->Zone6TravelAt=Now;
            if (F->bNarrationStarted && Id>=2006 && Id<=2016 && Id%2==0 && Id!=S->PreviousId)
            {
                const int32 Zone=(Id-2004)/2;
                Check(!S->CueAt.Contains(Zone),FString::Printf(TEXT("Zone%d arrival plays once"),Zone));
                S->CueAt.Add(Zone,Now);
                Check(R->IsMoving() && F->Phase==EOBStagePhase::Travel,FString::Printf(TEXT("Zone%d real arrival begins while moving"),Zone));
                Check(F->NarrationVoice && F->NarrationVoice->IsPlaying(),FString::Printf(TEXT("Zone%d production arrival AudioComponent is playing"),Zone));
                if (Zone==2 || Zone==5) FScreenshotRequest::RequestScreenshot(Out/FString::Printf(TEXT("Zone%d-Approach.png"),Zone),true,false);
            }
            S->PreviousId=Id;
            const int32 Zone=F->Phase==EOBStagePhase::Zone6Breach ? 6 : F->GetCurrentZone();
            if (((F->Phase==EOBStagePhase::Combat && Zone<6) || F->Phase==EOBStagePhase::Zone6Breach) && !S->Arrived.Contains(Zone))
            {
                S->Arrived.Add(Zone);S->CombatAt.Add(Zone,Now);
                const float* Cue=S->CueAt.Find(Zone);const float Lead=Cue ? Now-*Cue : -1.f;
                // The authored 83-to-85 segment is shorter than two seconds; preserve the preceding hostage dialogue.
                const float MinimumLead=Zone==6 && S->Zone6TravelAt>=0 ? FMath::Min(1.5f,Now-S->Zone6TravelAt-.1f) : 1.5f;
                Check(Cue && Lead>=MinimumLead && Lead<=4.5f,FString::Printf(TEXT("Zone%d cue precedes arrival by %.3f seconds (available travel %.3f)"),Zone,Lead,Zone==6 ? Now-S->Zone6TravelAt : -1.f));
                for (AOBEnemyCharacter* E : A->GetEncounterEnemies()) if (IsValid(E))
                    Check(E->EnemyVoiceAudio && FMath::IsNearlyEqual(E->EnemyVoiceAudio->VolumeMultiplier,3.f),FString::Printf(TEXT("Zone%d spawned enemy voice gain=3"),Zone));
                FFileHelper::SaveStringToFile(S->Report,*(Out/TEXT("progress.txt")));
            }
            if (F->Phase==EOBStagePhase::Combat && Zone<6 && S->CombatAt.Contains(Zone) && Now-S->CombatAt[Zone]>2.f)
            {
                const auto Enemies=A->GetEncounterEnemies();
                for (AOBEnemyCharacter* E : Enemies) if (IsValid(E)) { FDamageEvent Damage;E->TakeDamage(E->GetMaxHealth()+1.f,Damage,World->GetFirstPlayerController(),Player); }
            }
            if (F->Phase==EOBStagePhase::Zone6Breach && !S->Breached)
            { S->Breached=F->AttachBreachChargeForQA(); }
            if (F->Phase==EOBStagePhase::Zone6Intro)
            { Check(S->Arrived.Num()==6,TEXT("All arrival cues preserve normal Zone6 breach and combat entry"));Finish(); }
        }),.025f,true);
    }
};
static FAutoConsoleCommand GOBArrivalAudioPIE(TEXT("OB.Stage.ArrivalAudioPIE"),TEXT("Measure real moving arrival narration and enemy voice gain across six zones."),FConsoleCommandDelegate::CreateLambda([](){FOBArrivalAudioMapSmoke::Start();}));
#endif

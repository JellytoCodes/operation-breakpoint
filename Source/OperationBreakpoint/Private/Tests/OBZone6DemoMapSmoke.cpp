#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Editor.h"
#include "LevelEditor.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "Engine/DamageEvents.h"
#include "EngineUtils.h"
#include "Core/OBGameViewportClient.h"
#include "Core/OBPlayerController.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Character/OBActionComponent.h"
#include "Character/OBEnemyCharacter.h"
#include "Character/OBHostageCharacter.h"
#include "Stage/OBStageFlowComponent.h"
#include "Stage/OBStageCinematicWidget.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "MediaPlayer.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/GameModeBase.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

// Exercise the operator key through real viewport input and fresh level travel.
// Only the shots are synthetic; production intro, rescue timer and endings run normally.
class FOBZone6DemoMapSmoke
{
public:
    static void Start()
    {
        FRequestPlaySessionParams Params;Params.GlobalMapOverride=TEXT("/Game/Maps/TitleMap");Params.bAllowOnlineSubsystem=false;
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        GEditor->RequestPlaySession(Params);
        struct FState
        {
            int32 Step=0,Run=0;bool OK=true;
            double Started=FPlatformTime::Seconds(),IntroAt=0;
            FString Log;FGuid PreviousSession;
            uint64 OutcomeCaptureFrame=0;
            TWeakObjectPtr<UWorld> PreviousWorld;
            TWeakObjectPtr<UMediaPlayer> PreviousMovie;
        };
        auto S=MakeShared<FState>();
        const FString Out=FPaths::ProjectSavedDir()/TEXT("Tests/Zone6Demo/PIE");IFileManager::Get().MakeDirectory(*Out,true);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S,Out](float) {
            auto Check=[&](bool OK,const TCHAR* Label) {
                S->OK &= OK;S->Log+=FString::Printf(TEXT("%s run=%d %s\n"),OK ? TEXT("PASS") : TEXT("FAIL"),S->Run,Label);
                FFileHelper::SaveStringToFile(S->Log,*(Out/TEXT("progress.txt")));
            };
            auto Finish=[&]() {
                S->Log+=S->OK ? TEXT("outcome=PASS\n") : TEXT("outcome=FAIL\n");
                FFileHelper::SaveStringToFile(S->Log,*(Out/TEXT("verification.txt")));
                UE_LOG(LogTemp,Display,TEXT("ZONE6_DEMO_PIE\n%s"),*S->Log);
                GEditor->RequestEndPlayMap();const bool OK=S->OK;
                FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([OK](float) {
                    if (GEditor && GEditor->PlayWorld) return true;
                    FPlatformMisc::RequestExitWithStatus(false,OK ? 0 : 1);return false;
                }),.25f);
            };
            if (FPlatformTime::Seconds()-S->Started>240.) { Check(false,TEXT("Replay scenarios meet deadline"));Finish();return false; }
            auto* W=GEditor ? GEditor->PlayWorld.Get() : nullptr;
            auto* PC=W ? Cast<AOBPlayerController>(W->GetFirstPlayerController()) : nullptr;
            if (!PC || W->GetTimeSeconds()<.2f) return true;
            auto Key=[&](FKey K,EInputEvent Event) {
                auto* View=PC->GetLocalPlayer() ? PC->GetLocalPlayer()->ViewportClient.Get() : nullptr;
                if (!Cast<UOBGameViewportClient>(View)) { Check(false,TEXT("Actual viewport owns operator shortcut"));return; }
                View->InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),K,Event,FPlatformTime::Cycles64()));
            };
            auto* P=Cast<AOBPlayerCharacter>(PC->GetPawn());
            auto* Flow=P ? P->GetStageFlowComponent() : nullptr;
            auto Replay=[&]() {
                S->PreviousWorld=W;
                if (Flow) { S->PreviousSession=Flow->GetStageRecord().SessionId;S->PreviousMovie=Flow->Cinematic ? Flow->Cinematic->GetMediaPlayer() : nullptr; }
                Key(EKeys::F10,IE_Pressed);Key(EKeys::F10,IE_Released);S->Step=1;
            };
            if (S->Step==0) { Replay();return true; }
            if (S->Step==4)
            {
                if (W==S->PreviousWorld.Get()) return true;
                Check(UGameplayStatics::GetCurrentLevelName(W,true)==TEXT("TitleMap"),TEXT("F5 returns to ordinary title"));
                const auto* Mode=W->GetAuthGameMode();
                Check(Mode && UGameplayStatics::ParseOption(Mode->OptionsString,TEXT("OBZone6Demo")).IsEmpty(),TEXT("Normal navigation drops demo option"));
                Finish();return false;
            }
            if (S->Step==5)
            {
                // Let the outcome screenshot render before navigation closes its movie/UI.
                if (GFrameCounter<=S->OutcomeCaptureFrame) return true;
                Replay();return true;
            }
            if (!Flow || !Flow->IsStageActive()) return true;
            if (S->Step==1)
            {
                if (W==S->PreviousWorld.Get() || Flow->GetPhase()!=EOBStagePhase::Zone6Intro) return true;
                Check(Flow->GetCurrentZone()==6 && P->GetPlayerSplineComponent()->GetCurrentPointIndex()==86 && Flow->bBreachCompleted,
                    TEXT("F10 reloads directly into the post-breach Zone6 intro"));
                Check(Flow->IsInputBlocked() && FMath::IsNearlyEqual(Flow->GetRemainingSeconds(),10.f),TEXT("Three-second intro preserves full rescue timer"));
                Check(Flow->GetStageRecord().SessionId!=S->PreviousSession && Flow->GetStageRecord().bUsedTestInputs && !Flow->GetStageRecord().bFinished,
                    TEXT("Replay has a new demo-tagged unfinished session"));
                Check(PC->GetCurrentAmmo()==20 && PC->GetMagazineCapacity()==20 && !PC->IsTriggerHeld() && !UGameplayStatics::IsGamePaused(W),
                    TEXT("Reload restores 20/20 ammunition and clears pause/held trigger"));
                Check(!S->PreviousMovie.IsValid() || !S->PreviousMovie->IsPlaying(),TEXT("Old ending movie is stopped"));
                int32 Hostages=0;bool Healthy=true;
                for (TActorIterator<AOBHostageCharacter> It(W);It;++It) { ++Hostages;Healthy &= It->GetCurrentHealth()>0 && It->CanBeDamaged(); }
                Check(Hostages==2 && Healthy && P->GetActionComponent()->HasEncounterEnemies(),TEXT("Both hostages and live combatants respawn"));
                Check(PC->GetViewTarget()==P && PC->PlayerCameraManager && !PC->PlayerCameraManager->bEnableFading,TEXT("Native rescue camera is visible without a stale blackout"));
                if (S->Run<3) FScreenshotRequest::RequestScreenshot(Out/FString::Printf(TEXT("Replay-%d.png"),S->Run),true,false);
                if (S->Run==3)
                {
                    PC->InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),EKeys::LeftMouseButton,IE_Pressed,FPlatformTime::Cycles64()));
                    PC->SetPause(true);Check(UGameplayStatics::IsGamePaused(W),TEXT("Paused replay fixture is actually paused"));
                    S->Run=4;Replay();return true;
                }
                if (S->Run==4) { S->PreviousWorld=W;Key(EKeys::F5,IE_Pressed);S->Step=4;return true; }
                Key(EKeys::F10,IE_Repeat);S->IntroAt=W->GetTimeSeconds();S->Step=2;return true;
            }
            if (S->Step==2 && Flow->GetPhase()==EOBStagePhase::Combat)
            {
                Check(W->GetTimeSeconds()-S->IntroAt>=2.f && Flow->GetRemainingSeconds()>9.f,TEXT("Intro finishes and shooting unlocks before the rescue countdown"));
                auto Hit=[&](AOBEnemyCharacter* Enemy) {
                    TInlineComponentArray<UBoxComponent*> Boxes(Enemy);
                    for (auto* Box:Boxes) if (Box->ComponentHasTag(TEXT("HitZone.Head")))
                    { FHitResult Contact(Enemy,Box,Box->GetComponentLocation(),FVector::UpVector);UGameplayStatics::ApplyPointDamage(Enemy,1000,FVector::ForwardVector,Contact,PC,P,nullptr);return; }
                };
                if (S->Run==0) { for (TActorIterator<AOBHostageCharacter> It(W);It;++It) { Hit(*It);break; } }
                if (S->Run==1) { const auto Enemies=P->GetActionComponent()->GetEncounterEnemies();for (AOBEnemyCharacter* Enemy:Enemies) if (IsValid(Enemy)) Hit(Enemy); }
                S->Step=3;return true;
            }
            if (S->Step==3)
            {
                if (S->Run==1 && Flow->GetPhase()!=EOBStagePhase::Complete) return true;
                if (S->Run!=1 && (Flow->GetPhase()!=EOBStagePhase::Ending || !Flow->Cinematic || !Flow->Cinematic->GetMediaPlayer()->IsPlaying())) return true;
                if (S->Run!=1 && Flow->Cinematic->GetMediaPlayer()->GetTime().GetTotalSeconds()<.5) return true;
                Check(Flow->DidSucceed()==(S->Run==1),S->Run==1 ? TEXT("Enemy clear reaches actual success result") : S->Run==0 ? TEXT("Hostage hit plays actual failure ending") : TEXT("Timeout plays actual failure ending"));
                const auto& Record=Flow->GetStageRecord();
                if (S->Run!=1) Check(Record.StageZones.IsValidIndex(5) && Record.StageZones[5].FailureReason==(S->Run==0 ? TEXT("hostage_hit") : TEXT("time_expired")),TEXT("Failure reason matches this fresh attempt"));
                FScreenshotRequest::RequestScreenshot(Out/FString::Printf(TEXT("Outcome-%d.png"),S->Run),true,false);
                ++S->Run;S->OutcomeCaptureFrame=GFrameCounter;S->Step=5;return true;
            }
            return true;
        }),.05f);
    }
};
static FAutoConsoleCommand GOBZone6DemoPIE(TEXT("OB.Stage.Zone6DemoPIE"),TEXT("Verify F10 post-breach replay, failure/success/timeout, pause reset and F5 in real PIE, then exit."),FConsoleCommandDelegate::CreateStatic(&FOBZone6DemoMapSmoke::Start));
#endif

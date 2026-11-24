#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Editor.h"
#include "LevelEditor.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "EngineUtils.h"
#include "Engine/DamageEvents.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/OBEnemyAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Core/OBPlayerController.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Character/OBActionComponent.h"
#include "Character/OBEnemyCharacter.h"
#include "Stage/OBStageFlowComponent.h"
#include "AI/OBAIController.h"
#include "AI/OBEnemyArchetype.h"
#include "World/OBRPGRocketActor.h"
#include "Sound/SoundBase.h"

// Disposable PIE fixture only. No editor actors or map packages are saved.
class FOBStageNarrationRPGMapSmoke
{
public:
    static void Start()
    {
        FRequestPlaySessionParams Params;
        Params.GlobalMapOverride=TEXT("/Game/Maps/Stage_01");Params.bAllowOnlineSubsystem=false;
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        GEditor->RequestPlaySession(Params);
        const double Began=FPlatformTime::Seconds();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Began](float) {
            auto* W=GEditor ? GEditor->PlayWorld.Get() : nullptr;
            auto* PC=W ? Cast<AOBPlayerController>(W->GetFirstPlayerController()) : nullptr;
            auto* P=PC ? Cast<AOBPlayerCharacter>(PC->GetPawn()) : nullptr;
            if (P && W->GetTimeSeconds()>.7f) { Run(W,P);return false; }
            if (FPlatformTime::Seconds()-Began>100) { FPlatformMisc::RequestExitWithStatus(false,1);return false; }
            return true;
        }),.1f);
    }
private:
    static void Run(UWorld* W,AOBPlayerCharacter* P)
    {
        struct FState
        {
            int32 Step=0;double Began=0,StepTime=0;bool OK=true;
            bool bSawFirstAudio=false,bSawSecondAudio=false,bSawRocket=false;
            bool bSawRPGBody=false,bSawRPGWeapon=false,bAimCaptured=false,bReloadCaptured=false;
            TWeakObjectPtr<ACameraActor> Camera;
            TArray<int32> FinalIds;TWeakObjectPtr<AOBEnemyCharacter> RPG;
            int32 SupportId=0,ShotSnapshot=0;FString Log,Output=TEXT("Build/Testing/Verification/StageNarrationRPG");FTimerHandle Timer;
        };
        auto S=MakeShared<FState>();S->Began=S->StepTime=W->GetTimeSeconds();
        FParse::Value(FCommandLine::Get(),TEXT("StageFlowOutput="),S->Output);
        auto* F=P->GetStageFlowComponent();
        F->ClearPresentation();F->RestoreOpeningMovement();F->Phase=EOBStagePhase::Travel;
        P->GetPlayerSplineComponent()->SetStageMovementBlocked(true);
        P->GetCharacterMovement()->DisableMovement();
        P->SetCanBeDamaged(false);
        TWeakObjectPtr<UWorld> Weak=W;
        W->GetTimerManager().SetTimer(S->Timer,FTimerDelegate::CreateLambda([S,Weak]() {
            auto* World=Weak.Get();if (!World) return;
            auto* PC=Cast<AOBPlayerController>(World->GetFirstPlayerController());
            auto* Player=PC ? Cast<AOBPlayerCharacter>(PC->GetPawn()) : nullptr;if (!Player) return;
            auto* Flow=Player->GetStageFlowComponent();
            const auto KeepAlive=S;
            auto Check=[&](bool Good,const TCHAR* Label) { S->OK &= Good;S->Log+=FString::Printf(TEXT("%s %s\n"),Good ? TEXT("PASS") : TEXT("FAIL"),Label); };
            auto Finish=[&]() {
                if (S->RPG.IsValid()) S->RPG->Destroy();
                if (S->Camera.IsValid()) S->Camera->Destroy();
                S->Log+=FString::Printf(TEXT("outcome=%s\n"),S->OK ? TEXT("PASS") : TEXT("FAIL"));
                FFileHelper::SaveStringToFile(S->Log,*(FPaths::ProjectDir()/S->Output/TEXT("narration-rpg-pie.txt")));
                UE_LOG(LogTemp,Display,TEXT("STAGE_NARRATION_RPG_PIE\n%s"),*S->Log);
                World->GetTimerManager().ClearTimer(S->Timer);GEditor->RequestEndPlayMap();
                const bool OK=S->OK;
                FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([OK](float) {
                    if (GEditor && GEditor->PlayWorld) return true;
                    FPlatformMisc::RequestExitWithStatus(false,OK ? 0 : 1);return false;
                }),.5f);
            };
            if (World->GetTimeSeconds()-S->Began>100) { Check(false,TEXT("Scenario deadline"));Finish();return; }
            if (S->Step==0)
            {
                Check(Flow->IsStageActive(),TEXT("Real Stage_01 component is active"));
                for (int32 Id=2005;Id<=2021;++Id)
                {
                    const auto* Row=Flow->FindScript(Id);
                    Check(Row && Row->Sound.LoadSynchronous(),*FString::Printf(TEXT("Narration row %d has an existing voice"),Id));
                }
                Flow->CurrentZone=1;
                Flow->PlayAutomaticScript(2005,1);Flow->PlayAutomaticScript(2006,1);Flow->PlayAutomaticScript(2006,1);
                Check(Flow->ActiveScriptId==2005 && Flow->NarrationQueue.Num()==1,TEXT("Arrival queues after movement without duplicates"));
                S->Step=1;return;
            }
            if (S->Step==1)
            {
                if (Flow->ActiveScriptId==2005 && Flow->NarrationVoice && Flow->NarrationVoice->IsPlaying()) S->bSawFirstAudio=true;
                if (Flow->ActiveScriptId==2006 && Flow->NarrationVoice && Flow->NarrationVoice->IsPlaying()) S->bSawSecondAudio=true;
                if (Flow->ActiveScriptId || !Flow->NarrationQueue.IsEmpty()) return;
                Check(S->bSawFirstAudio && S->bSawSecondAudio,TEXT("Both movement and arrival use actual playing AudioComponents"));
                Check(World->GetTimeSeconds()-S->StepTime>=7.f,TEXT("Normal narration is not cut off before voice duration"));
                Flow->PlayScript(2005);Flow->Phase=EOBStagePhase::Combat;Flow->StartSupport();
                S->SupportId=Flow->LastSupportScript;
                Check(Flow->ActiveScriptId==S->SupportId && Flow->NarrationQueue.IsEmpty(),TEXT("Support voice interrupts normal dialogue immediately"));
                S->Step=2;return;
            }
            if (S->Step==2)
            {
                if (Flow->ActiveScriptId) return;
                Flow->Phase=EOBStagePhase::Travel;Flow->HandleActionPoint(83);
                Check(Flow->ActiveScriptId==2017 && Flow->NarrationQueue.Num()==1,TEXT("Stop 83 queues the hostage then terrorist dialogue"));
                S->Step=3;return;
            }
            if (S->Step==3)
            {
                if (Flow->ActiveScriptId && (S->FinalIds.IsEmpty() || S->FinalIds.Last()!=Flow->ActiveScriptId)) S->FinalIds.Add(Flow->ActiveScriptId);
                if (Flow->ActiveScriptId || !Flow->NarrationQueue.IsEmpty()) return;
                Check(S->FinalIds==TArray<int32>({2017,2018}),TEXT("The two pre-breach voices finish in the requested situation order"));
                Flow->ClearPresentation();Flow->RestoreOpeningMovement();
                Flow->Phase=EOBStagePhase::Combat;Flow->CurrentZone=3;Flow->RemainingSeconds=120;Flow->RemainingLives=5;
                Player->GetActionComponent()->SetExternalActionState(true);
                Player->SetActorLocation(FVector(100000,100000,5000));Player->GetCharacterMovement()->DisableMovement();
                Player->SetCanBeDamaged(true);
                const float StandingHalfHeight=Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
                Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
                Player->GetCharacterMovement()->Crouch(false);
                Player->GetCharacterMovement()->DisableMovement();
                Check(Player->bIsCrouched && Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()<StandingHalfHeight,
                    TEXT("Cover fixture uses a physically shortened crouch capsule"));
                FDamageEvent Ordinary;Player->TakeDamage(1.f,Ordinary,nullptr,nullptr);
                Check(Flow->RemainingLives==5,TEXT("Ordinary rifle damage still respects cover"));
                auto* Class=LoadClass<AOBEnemyCharacter>(nullptr,TEXT("/Game/Blueprints/Enemy/BP_RPGTrooper.BP_RPGTrooper_C"));
                Check(Class!=nullptr,TEXT("Saved RPG Blueprint loads"));
                if (!Class) { Finish();return; }
                auto* E=World->SpawnActor<AOBEnemyCharacter>(Class,Player->GetActorLocation()+FVector(700,0,0),FRotator(0,180,0));
                S->RPG=E;E->GetCharacterMovement()->DisableMovement();
                Check(E->IsRPGTrooper() && E->GetMaxHealth()==150.f,TEXT("RPG Blueprint initializes 150 HP and RPG role"));
                Check(E->DirectAimMinPercent==2 && E->DirectAimMaxPercent==5,TEXT("Saved RPG initializes the production integer 2-5 percent aim range"));
                // Fixture-only override: prove deterministic collision damage independently of random shot selection.
                E->DirectAimMinPercent=E->DirectAimMaxPercent=100;
                if (!E->GetController()) E->SpawnDefaultController();
                auto* AI=Cast<AOBAIController>(E->GetController());
                Check(AI!=nullptr,TEXT("RPG uses the existing enemy controller"));
                if (!AI) { Finish();return; }
                E->EncounterBehavior.EntryMode=EOBEnemyEntryMode::WaitAtSpawn;
                AI->PrepareEncounterEntry(E->GetActorTransform(),E->GetActorTransform(),0.f);
                E->StartEncounterBehavior();
                Check(E->IsRPGAttackActive(),TEXT("A staged RPG begins its dedicated attack cadence"));
                Check(E->RPGShotReloadAnimation && E->RPGShotReloadAnimation->GetSkeleton()==E->GetMesh()->GetSkinnedAsset()->GetSkeleton(),
                    TEXT("RPG shot/reload animation is retargeted to the live soldier skeleton"));
                Check(E->RPGWeaponMesh && E->RPGWeaponMesh->GetSkeletalMeshAsset() && E->RPGWeaponShotReloadAnimation,
                    TEXT("RPG Blueprint uses the imported weapon rig and matching reload clip"));
                auto* Camera=World->SpawnActor<ACameraActor>();S->Camera=Camera;
                Camera->SetActorLocation(E->GetActorLocation()+FVector(-250,310,70));
                Camera->SetActorRotation((E->GetActorLocation()+FVector(0,0,40)-Camera->GetActorLocation()).Rotation());
                Camera->GetCameraComponent()->SetFieldOfView(55.f);PC->SetViewTarget(Camera);
                S->StepTime=World->GetTimeSeconds();S->Step=4;return;
            }
            if (S->Step==4)
            {
                auto* E=S->RPG.Get();if (!E) { Check(false,TEXT("RPG remains alive for cadence verification"));Finish();return; }
                const double Elapsed=World->GetTimeSeconds()-S->StepTime;
                if (auto* Anim=Cast<UOBEnemyAnimInstance>(E->GetMesh()->GetAnimInstance()))
                    S->bSawRPGBody|=Anim->GetRPGPoseSequence()==E->RPGShotReloadAnimation && Anim->GetRPGPoseTime()>.05f;
                if (E->RPGWeaponMesh && E->RPGWeaponMesh->GetSingleNodeInstance())
                    S->bSawRPGWeapon|=E->RPGWeaponMesh->GetSingleNodeInstance()->IsPlaying();
                if (!S->bAimCaptured && Elapsed>1.1)
                { FScreenshotRequest::RequestScreenshot(FPaths::ProjectDir()/S->Output/TEXT("rpg-aim.png"),true,false);S->bAimCaptured=true; }
                if (!S->bReloadCaptured && Elapsed>4.6)
                { FScreenshotRequest::RequestScreenshot(FPaths::ProjectDir()/S->Output/TEXT("rpg-reload.png"),true,false);S->bReloadCaptured=true; }
                for (TActorIterator<AOBRPGRocketActor> It(World);It;++It) if (It->GetOwner()==E) S->bSawRocket=true;
                if (Elapsed<1.8) Check(E->GetRPGShotsFired()==0,TEXT("RPG waits before its first launch"));
                if (Elapsed<5.8) return;
                Check(S->bSawRocket && E->GetRPGShotsFired()==1,TEXT("RPG launches one real rocket after the two-second delay"));
                Check(Flow->RemainingLives==3,TEXT("Real rocket collision removes two hearts through cover"));
                Check(E->GetRPGVisualMesh() && E->GetRPGVisualMesh()->IsVisible(),TEXT("RPG launcher visual is visible"));
                Check(S->bSawRPGBody && S->bSawRPGWeapon,TEXT("The live soldier and RPG weapon both play their matching shot/reload animations"));
                if (auto* AI=Cast<AOBAIController>(E->GetController())) AI->StopStateTree();
                Check(!E->IsRPGAttackActive(),TEXT("AI stop cancels the independent RPG timer"));
                S->ShotSnapshot=E->GetRPGShotsFired();S->StepTime=World->GetTimeSeconds();S->Step=5;return;
            }
            if (S->Step==5)
            {
                if (World->GetTimeSeconds()-S->StepTime<4.3) return;
                Check(S->RPG.IsValid() && S->RPG->GetRPGShotsFired()==S->ShotSnapshot,TEXT("Stopped RPG cannot fire a delayed extra round"));
                Flow->Phase=EOBStagePhase::Travel;Flow->PlayScript(2005);Flow->SuspendForQA();
                Check(Flow->ActiveScriptId==0 && Flow->NarrationQueue.IsEmpty() && !Flow->NarrationVoice,TEXT("QA clears narration, audio and pending dialogue"));
                Finish();
            }
        }),.05f,true);
    }
};
static FAutoConsoleCommand GOBStageNarrationRPGPIE(TEXT("OB.Stage.NarrationRPGPIE"),
    TEXT("Disposable Stage_01 narration/audio and RPG projectile PIE verification. Exits this editor."),
    FConsoleCommandDelegate::CreateStatic(&FOBStageNarrationRPGMapSmoke::Start));
#endif

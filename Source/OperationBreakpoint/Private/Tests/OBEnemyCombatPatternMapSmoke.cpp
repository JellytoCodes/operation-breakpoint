#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Editor.h"
#include "LevelEditor.h"
#include "Containers/Ticker.h"
#include "AI/OBAIController.h"
#include "AI/OBEnemyCombatPattern.h"
#include "Animation/OBEnemyAnimInstance.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Character/OBEnemyCharacter.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Character/OBActionComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/OBPlayerController.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Stage/OBStageFlowComponent.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "Widget/OBHUD.h"
#include "Widget/OBPlayerOverlayWidget.h"

// Production Blueprints, StateTreeAIComponents, animation evaluation and projectiles in a disposable PIE world.
class FOBEnemyCombatPatternMapSmoke
{
public:
    static void Start()
    {
        FRequestPlaySessionParams Params;Params.GlobalMapOverride=TEXT("/Game/Maps/Stage_01");Params.bAllowOnlineSubsystem=false;
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        GEditor->RequestPlaySession(Params);
        const double Started=FPlatformTime::Seconds();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Started](float) {
            auto* W=GEditor ? GEditor->PlayWorld.Get() : nullptr;
            auto* P=W ? Cast<AOBPlayerCharacter>(W->GetFirstPlayerController()->GetPawn()) : nullptr;
            if (P && W->GetTimeSeconds()>.8f) { Run(W,P);return false; }
            if (FPlatformTime::Seconds()-Started>100.) { FPlatformMisc::RequestExitWithStatus(false,1);return false; }
            return true;
        }),.1f);
    }
private:
    static void Run(UWorld* W,AOBPlayerCharacter* P)
    {
        struct FSample
        {
            TWeakObjectPtr<AOBEnemyCharacter> Enemy;
            EOBEnemyCombatPattern Pattern;
            EOBEnemyPatternAction Previous=EOBEnemyPatternAction::Posture;
            int32 BurstStart=0,Bursts=0,DeathShots=0;
            bool CrouchSeen=false,StandSeen=false,Captured=false,CoverCaptured=false;
            float HeadHeight=0,ActionSince=0;
        };
        struct FState { bool OK=true,Killed=false;float Started=0,KilledAt=0;FString Log;TArray<FSample> Samples;TWeakObjectPtr<ACameraActor> Camera;FTimerHandle Timer; };
        auto S=MakeShared<FState>();S->Started=W->GetTimeSeconds();
        const FString Output=FPaths::ProjectDir()/TEXT("Build/Testing/Verification/EnemyCombatPatterns");IFileManager::Get().MakeDirectory(*Output,true);
        auto* Flow=P->GetStageFlowComponent();Flow->ClearPresentation();Flow->RestoreOpeningMovement();Flow->RestoreSlowMotion();
        Flow->Phase=EOBStagePhase::Combat;Flow->CurrentZone=2;Flow->RemainingSeconds=999.f;
        Flow->SetComponentTickEnabled(false);
        P->GetPlayerSplineComponent()->SetComponentTickEnabled(false);P->GetCharacterMovement()->DisableMovement();P->SetCanBeDamaged(false);
        P->GetActionComponent()->ResetEncounterForQA();P->GetActionComponent()->SetExternalActionState(false);
        P->SetActorLocation(FVector(3200,30000,100));
        auto* PC=Cast<AOBPlayerController>(P->GetController());
        if (PC->GetOBHUD() && PC->GetOBHUD()->GetPlayerOverlayWidget()) PC->GetOBHUD()->GetPlayerOverlayWidget()->SetVisibility(ESlateVisibility::Collapsed);
        auto* Floor=W->SpawnActor<AStaticMeshActor>(FVector(1900,30000,-10),FRotator::ZeroRotator);
        Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
        Floor->GetStaticMeshComponent()->SetCanEverAffectNavigation(false);
        Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        Floor->SetActorScale3D(FVector(40,40,.2));Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
        auto* Camera=W->SpawnActor<ACameraActor>();S->Camera=Camera;Camera->GetCameraComponent()->SetFieldOfView(60.f);PC->SetViewTarget(Camera);
        int32 Index=0;
        for (const auto Pattern : {EOBEnemyCombatPattern::Crouch,EOBEnemyCombatPattern::Prone,EOBEnemyCombatPattern::Stand,EOBEnemyCombatPattern::WindowCover,EOBEnemyCombatPattern::RPG})
        {
            const bool RPG=Pattern==EOBEnemyCombatPattern::RPG;
            auto* Class=LoadClass<AOBEnemyCharacter>(nullptr,RPG ? TEXT("/Game/Blueprints/Enemy/BP_RPGTrooper.BP_RPGTrooper_C") : TEXT("/Game/Blueprints/Enemy/BP_EnemyCharacter.BP_EnemyCharacter_C"));
            FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            auto* E=W->SpawnActor<AOBEnemyCharacter>(Class,FVector(1800,29200+400*Index++,100),FRotator::ZeroRotator,Spawn);
            if (!E) { S->OK=false;continue; }
            E->SetCanBeDamaged(true);E->EncounterBehavior.EntryMode=EOBEnemyEntryMode::WaitAtSpawn;
            E->EncounterBehavior.CoverMode=EOBEnemyCoverMode::None;E->EncounterBehavior.CombatPattern=Pattern;E->EncounterBehavior.bFacePlayer=true;
            E->GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
            E->GetMesh()->bEnableUpdateRateOptimizations=false;E->GetMesh()->SetForcedLOD(1);
            if (!E->GetController()) E->SpawnDefaultController();
            auto* AI=Cast<AOBAIController>(E->GetController());
            if (!AI) { S->OK=false;continue; }
            AI->PrepareEncounterEntry(E->GetActorTransform(),E->GetActorTransform());E->StartEncounterBehavior();
            S->OK &= !AI->IsCombatTreeRunning() && !E->IsCombatPatternActive();
            FSample Sample;Sample.Enemy=E;Sample.Pattern=Pattern;S->Samples.Add(Sample);
        }
        P->GetActionComponent()->SetExternalActionState(true);
        for (auto& Sample : S->Samples) Sample.Enemy->StartEncounterBehavior();
        TWeakObjectPtr<UWorld> World=W;
        W->GetTimerManager().SetTimer(S->Timer,FTimerDelegate::CreateLambda([S,World,Output]() {
            auto* W=World.Get();if (!W) return;
            const float Now=W->GetTimeSeconds();bool All=S->Samples.Num()==5,IssuedScreenshot=false;
            for (auto& Sample : S->Samples)
            {
                auto* E=Sample.Enemy.Get();if (!E) { S->OK=false;continue; }
                auto* AI=Cast<AOBAIController>(E->GetController());
                if (S->Killed) { S->OK &= !E->IsCombatPatternActive() && E->GetPatternShotsFired()==Sample.DeathShots;continue; }
                S->OK &= AI && AI->IsCombatTreeRunning() && E->IsCombatPatternActive();
                const auto Action=E->GetPatternAction();const auto Posture=E->GetCombatPosture();
                if (Action!=Sample.Previous) Sample.ActionSince=Now;
                Sample.CrouchSeen|=Posture==EOBEnemyPosture::Crouch;Sample.StandSeen|=Posture==EOBEnemyPosture::Stand;
                if (Sample.Pattern==EOBEnemyCombatPattern::Prone) S->OK &= Posture==EOBEnemyPosture::Prone;
                if (Sample.Pattern==EOBEnemyCombatPattern::Crouch || Sample.Pattern==EOBEnemyCombatPattern::RPG) S->OK &= Posture==EOBEnemyPosture::Crouch;
                if (Sample.Pattern==EOBEnemyCombatPattern::Stand) S->OK &= Posture==EOBEnemyPosture::Stand;
                if (Sample.Previous==EOBEnemyPatternAction::Fire && Action==EOBEnemyPatternAction::Reload)
                {
                    const int32 Count=E->GetPatternShotsFired()-Sample.BurstStart;
                    S->OK &= Count==(Sample.Pattern==EOBEnemyCombatPattern::RPG ? 1 : 5);
                    Sample.BurstStart=E->GetPatternShotsFired();++Sample.Bursts;
                    S->Log+=FString::Printf(TEXT("pattern=%s burst=%d projectiles=%d posture=%s\n"),*UEnum::GetValueAsString(Sample.Pattern),Sample.Bursts,Count,*UEnum::GetValueAsString(Posture));
                }
                if (!IssuedScreenshot && Now-S->Started>2.f && Now-Sample.ActionSince>.2f && (!Sample.Captured && Action==EOBEnemyPatternAction::Aim
                    || Sample.Pattern==EOBEnemyCombatPattern::WindowCover && !Sample.CoverCaptured && Posture==EOBEnemyPosture::Crouch))
                {
                    const FString Name=UEnum::GetValueAsString(Sample.Pattern).Replace(TEXT("EOBEnemyCombatPattern::"),TEXT(""));
                    const FVector Target=E->GetActorLocation()+FVector(0,0,-30);
                    S->Camera->SetActorLocation(Target+FVector(350,-350,160));S->Camera->SetActorRotation((Target-S->Camera->GetActorLocation()).Rotation());
                    const bool Cover=Posture==EOBEnemyPosture::Crouch && Sample.Pattern==EOBEnemyCombatPattern::WindowCover;
                    FScreenshotRequest::RequestScreenshot(Output/(Name+(Cover ? TEXT("-Cover.png") : TEXT("-Aim.png"))),true,false);
                    if (Cover) Sample.CoverCaptured=true;else
                    {
                        Sample.Captured=true;Sample.HeadHeight=E->GetMesh()->GetSocketLocation(TEXT("head")).Z;
                        S->OK &= !E->GetCharacterMovement()->IsFalling() && Sample.HeadHeight>0 && Sample.HeadHeight<250.f;
                        if (Sample.Pattern==EOBEnemyCombatPattern::Crouch || Sample.Pattern==EOBEnemyCombatPattern::Prone || Sample.Pattern==EOBEnemyCombatPattern::RPG)
                            S->OK &= E->bIsCrouched;
                        S->Log+=FString::Printf(TEXT("pattern=%s aim_head_height_cm=%.3f sequence=%s\n"),*Name,Sample.HeadHeight,*GetNameSafe(E->GetPostureSequence()));
                    }
                    IssuedScreenshot=true;
                }
                Sample.Previous=Action;All &= Sample.Bursts>=2 && Sample.Captured;
                if (Sample.Pattern==EOBEnemyCombatPattern::WindowCover) All &= Sample.CrouchSeen && Sample.StandSeen && Sample.CoverCaptured;
            }
            if (!S->Killed && All)
            {
                float StandHeight=0,CrouchHeight=0,ProneHeight=0;
                for (const auto& Sample : S->Samples)
                {
                    if (Sample.Pattern==EOBEnemyCombatPattern::Stand) StandHeight=Sample.HeadHeight;
                    if (Sample.Pattern==EOBEnemyCombatPattern::Crouch) CrouchHeight=Sample.HeadHeight;
                    if (Sample.Pattern==EOBEnemyCombatPattern::Prone) ProneHeight=Sample.HeadHeight;
                }
                const bool PoseHeights=StandHeight>CrouchHeight+20.f && CrouchHeight>ProneHeight+20.f;
                S->OK &= PoseHeights;
                S->Log+=FString::Printf(TEXT("grounded_pose_heights_valid=%d stand=%.3f crouch=%.3f prone=%.3f\n"),PoseHeights,StandHeight,CrouchHeight,ProneHeight);
                for (auto& Sample : S->Samples)
                {
                    Sample.DeathShots=Sample.Enemy->GetPatternShotsFired();FDamageEvent Damage;
                    Sample.Enemy->TakeDamage(10000.f,Damage,nullptr,nullptr);
                }
                S->Killed=true;S->KilledAt=Now;return;
            }
            if (Now-S->Started<45.f && (!S->Killed || Now-S->KilledAt<1.f)) return;
            S->OK &= S->Killed;S->Log+=S->OK ? TEXT("outcome=PASS\n") : TEXT("outcome=FAIL\n");
            FFileHelper::SaveStringToFile(S->Log,*(Output/TEXT("patterns-pie.txt")));UE_LOG(LogTemp,Display,TEXT("ENEMY_PATTERN_PIE\n%s"),*S->Log);
            W->GetTimerManager().ClearTimer(S->Timer);GEditor->RequestEndPlayMap();
            const bool OK=S->OK;FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([OK](float) {
                if (GEditor && GEditor->PlayWorld) return true;FPlatformMisc::RequestExitWithStatus(false,OK ? 0 : 1);return false;
            }),.5f);
        }),.05f,true);
    }
};
static FAutoConsoleCommand GOBEnemyCombatPatternsPIECommand(TEXT("OB.AI.PatternsPIE"),TEXT("Verify all five production enemy StateTrees, poses and shot counts in PIE, then close this editor."),FConsoleCommandDelegate::CreateStatic(&FOBEnemyCombatPatternMapSmoke::Start));
#endif

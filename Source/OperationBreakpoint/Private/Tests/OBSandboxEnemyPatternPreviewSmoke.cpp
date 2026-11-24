#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Editor.h"
#include "LevelEditor.h"
#include "EditorViewportClient.h"
#include "IAssetViewport.h"
#include "Containers/Ticker.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "AI/OBAIController.h"
#include "Character/OBEnemyCharacter.h"
#include "World/OBEnemyPatternPreview.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "ImageUtils.h"
#include "Animation/OBEnemyAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Character/OBWeaponGripComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"

class FOBSandboxEnemyPatternPreviewSmoke
{
public:
    static void Start()
    {
        FRequestPlaySessionParams Params;Params.GlobalMapOverride=TEXT("/Game/Maps/SandboxMap");Params.bAllowOnlineSubsystem=false;
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        GEditor->RequestPlaySession(Params);
        const double Started=FPlatformTime::Seconds();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Started](float) {
            auto* W=GEditor ? GEditor->PlayWorld.Get() : nullptr;
            if (W && W->GetTimeSeconds()>1.f)
                for (TActorIterator<AOBEnemyPatternPreview> It(W);It;++It)
                    if (It->HasStartedPreview()) { Run(W,*It);return false; }
            if (FPlatformTime::Seconds()-Started>60.) { UE_LOG(LogTemp,Error,TEXT("SANDBOX_PREVIEW bootstrap failed"));FPlatformMisc::RequestExitWithStatus(false,1);return false; }
            return true;
        }),.1f);
    }
private:
    static void Run(UWorld* W,AOBEnemyPatternPreview* Preview)
    {
        struct FState
        {
            bool OK=true,RequestedEject=false,Ejected=false,CameraMoved=false,Captured=false;
            float Started=0,EjectedAt=0;
            TArray<int32> ShotsAtEject;
            TArray<int32> LastShotCounts;
            TArray<float> LastShotTimes;
            TArray<float> BurstStarts;
            TArray<float> MaximumAnimationTimes;
            TArray<int32> CompletedAnimations;
            TArray<EOBEnemyPatternAction> LastActions;
            float MinimumRifleGap=MAX_flt;
            float MaximumRifleGap=0.f,RifleFireLength=0.f,MaximumFrameDelta=0.f;
            float MaximumRifleBurstSpan=0.f;
            int32 RifleIntervals=0,AnimationStarts=0,AnimationCompletions=0;
            FTimerHandle Timer;
        };
        auto S=MakeShared<FState>();S->Started=W->GetTimeSeconds();
        S->LastShotCounts.Init(0,Preview->Entries.Num());S->LastShotTimes.Init(-1.f,Preview->Entries.Num());
        S->BurstStarts.Init(-1.f,Preview->Entries.Num());
        S->MaximumAnimationTimes.Init(0.f,Preview->Entries.Num());S->CompletedAnimations.Init(0,Preview->Entries.Num());
        S->LastActions.Init(EOBEnemyPatternAction::Posture,Preview->Entries.Num());
        const FString Output=FPaths::ProjectDir()/TEXT("Build/Testing/Verification/SandboxEnemyPatterns");
        TWeakObjectPtr<AOBEnemyPatternPreview> Manager=Preview;TWeakObjectPtr<UWorld> World=W;
        W->GetTimerManager().SetTimer(S->Timer,FTimerDelegate::CreateLambda([S,Manager,World,Output]() {
            auto* W=World.Get();auto* Preview=Manager.Get();if (!W || !Preview) return;
            const float Now=W->GetTimeSeconds();
            S->MaximumFrameDelta=FMath::Max(S->MaximumFrameDelta,W->GetDeltaSeconds());
            S->OK &= Preview->Entries.Num()==5;
            bool AllAdvanced=true,AllRifleCyclesComplete=true;
            for (int32 Index=0;Index<Preview->Entries.Num();++Index)
            {
                auto* Enemy=Preview->Entries[Index].Enemy.Get();
                auto* AI=Enemy ? Cast<AOBAIController>(Enemy->GetController()) : nullptr;
                S->OK &= AI && AI->IsCombatTreeRunning() && Enemy->IsCombatPatternActive()
                    && AI->GetActiveCombatTree()==Preview->Entries[Index].StateTree.Get();
                if (Enemy && !Enemy->IsRPGTrooper() && Enemy->GetPatternShotsFired()>S->LastShotCounts[Index])
                {
                    S->OK &= Enemy->GetPatternShotsFired()-S->LastShotCounts[Index]==1;
                    const int32 Shots=Enemy->GetPatternShotsFired();
                    S->OK &= Enemy->GetLastShotAimChancePercent()>=2 && Enemy->GetLastShotAimChancePercent()<=5;
                    if ((Shots-1)%5==0) S->BurstStarts[Index]=Now;
                    else if (S->LastShotTimes[Index]>=0.f)
                    {
                        S->MinimumRifleGap=FMath::Min(S->MinimumRifleGap,Now-S->LastShotTimes[Index]);
                        S->MaximumRifleGap=FMath::Max(S->MaximumRifleGap,Now-S->LastShotTimes[Index]);
                        S->OK &= S->MaximumAnimationTimes[Index]>=S->RifleFireLength-.06f;
                        ++S->AnimationCompletions;++S->CompletedAnimations[Index];
                        ++S->RifleIntervals;
                    }
                    S->MaximumAnimationTimes[Index]=0.f;++S->AnimationStarts;
                    if (Shots%5==0 && S->BurstStarts[Index]>=0.f)
                        S->MaximumRifleBurstSpan=FMath::Max(S->MaximumRifleBurstSpan,Now-S->BurstStarts[Index]);
                    S->LastShotCounts[Index]=Enemy->GetPatternShotsFired();S->LastShotTimes[Index]=Now;
                }
                if (Enemy && !Enemy->IsRPGTrooper())
                {
                    const auto Action=Enemy->GetPatternAction();
                    if (Enemy->CombatPatterns && Enemy->CombatPatterns->RifleFire)
                        S->RifleFireLength=Enemy->CombatPatterns->RifleFire->GetPlayLength();
                    if (Action==EOBEnemyPatternAction::Reload && S->LastActions[Index]==EOBEnemyPatternAction::Fire)
                    {
                        S->OK &= S->MaximumAnimationTimes[Index]>=S->RifleFireLength-.06f;
                        ++S->AnimationCompletions;++S->CompletedAnimations[Index];S->MaximumAnimationTimes[Index]=0.f;
                    }
                    if (auto* Anim=Cast<UOBEnemyAnimInstance>(Enemy->GetMesh()->GetAnimInstance()))
                    {
                        const auto Pose=Anim->GetPatternPose();
                        if (Pose.Action && Enemy->CombatPatterns && Pose.Action==Enemy->CombatPatterns->RifleFire)
                            S->MaximumAnimationTimes[Index]=FMath::Max(S->MaximumAnimationTimes[Index],Pose.ActionTime);
                    }
                    S->LastActions[Index]=Action;
                    AllRifleCyclesComplete &= S->CompletedAnimations[Index]>=5;
                }
                if (S->Ejected) AllAdvanced &= Enemy && S->ShotsAtEject.IsValidIndex(Index) && Enemy->GetPatternShotsFired()>S->ShotsAtEject[Index];
            }
            if (!S->RequestedEject && Now-S->Started>2.f)
            {
                FScreenshotRequest::RequestScreenshot(Output/TEXT("Preview-Play.png"),true,false);
                for (const auto& Entry : Preview->Entries) S->ShotsAtEject.Add(Entry.Enemy->GetPatternShotsFired());
                GEditor->RequestToggleBetweenPIEandSIE();S->RequestedEject=true;return;
            }
            if (S->RequestedEject && !S->Ejected && GEditor->bIsSimulatingInEditor)
            {
                auto Viewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
                auto& View=Viewport->GetAssetViewportClient();const FVector Before=View.GetViewLocation();
                const FVector Position=Preview->GetActorLocation()+FVector(950,-900,450);
                View.SetViewLocation(Position);View.SetViewRotation((Preview->GetActorLocation()+FVector(0,0,70)-Position).Rotation());View.Invalidate();
                S->CameraMoved=!View.GetViewLocation().Equals(Before,100.f);S->Ejected=true;S->EjectedAt=Now;return;
            }
            if (S->Ejected && !S->Captured && Now-S->EjectedAt>.5f)
            {
                auto Window=FSlateApplication::Get().GetActiveTopLevelWindow();
                if (!Window)
                    for (const auto& Candidate : FSlateApplication::Get().GetTopLevelWindows())
                        if (Candidate->GetClientSizeInScreen().X>500) { Window=Candidate;break; }
                TArray<FColor> Pixels;FIntVector Size;
                if (Window && FSlateApplication::Get().TakeScreenshot(Window.ToSharedRef(),Pixels,Size))
                {
                    TArray<uint8> Png;FImageUtils::CompressImageArray(Size.X,Size.Y,Pixels,Png);
                    S->Captured=FFileHelper::SaveArrayToFile(Png,*(Output/TEXT("Preview-F8-FreeCamera.png")));
                }
            }
            if (Now-S->Started<25.f && !(S->Ejected && AllAdvanced && AllRifleCyclesComplete && S->Captured)) return;
            S->OK &= S->Ejected && S->CameraMoved && S->Captured && AllAdvanced;
            S->OK &= AllRifleCyclesComplete && S->RifleIntervals>=16 && S->RifleFireLength>0.f
                && S->MinimumRifleGap>=S->RifleFireLength-.04f && S->MaximumRifleGap<=S->RifleFireLength+S->MaximumFrameDelta+.025f;
            FString Report=FString::Printf(TEXT("saved_preview_entries=%d\nf8_ejected=%d\nfree_camera_moved=%d\nall_five_trees_advanced_after_f8=%d\nfree_camera_screenshot=%d\n"),
                Preview->Entries.Num(),S->Ejected,S->CameraMoved,AllAdvanced,S->Captured);
            Report+=FString::Printf(TEXT("rifle_intervals_observed=%d\nminimum_rifle_shot_gap_seconds=%.4f\n"),S->RifleIntervals,S->MinimumRifleGap);
            Report+=FString::Printf(TEXT("maximum_five_round_burst_span_seconds=%.4f\n"),S->MaximumRifleBurstSpan);
            Report+=FString::Printf(TEXT("fire_animation_length_seconds=%.4f\nmaximum_rifle_shot_gap_seconds=%.4f\nrifle_animation_starts=%d\ncompleted_rifle_animation_playbacks=%d\n"),S->RifleFireLength,S->MaximumRifleGap,S->AnimationStarts,S->AnimationCompletions);
            Report+=FString::Printf(TEXT("maximum_world_frame_delta_seconds=%.4f\n"),S->MaximumFrameDelta);
            for (const auto& Entry : Preview->Entries)
                Report+=FString::Printf(TEXT("pattern=%s shots=%d\n"),*UEnum::GetValueAsString(Entry.Pattern),Entry.Enemy->GetPatternShotsFired());
            Report+=S->OK ? TEXT("outcome=PASS\n") : TEXT("outcome=FAIL\n");
            FFileHelper::SaveStringToFile(Report,*(Output/TEXT("sandbox-preview-pie.txt")));UE_LOG(LogTemp,Display,TEXT("SANDBOX_PREVIEW_PIE\n%s"),*Report);
            W->GetTimerManager().ClearTimer(S->Timer);GEditor->RequestEndPlayMap();
            const bool OK=S->OK;FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([OK](float) {
                if (GEditor && GEditor->PlayWorld) return true;FPlatformMisc::RequestExitWithStatus(false,OK ? 0 : 1);return false;
            }),.5f);
        }),1.f/120.f,true);
    }
};
static FAutoConsoleCommand GOBSandboxEnemyPreviewPIECommand(TEXT("OB.AI.SandboxPreviewPIE"),TEXT("Load the saved five-enemy SandboxMap preview and verify F8 eject/free camera without stopping the StateTrees."),FConsoleCommandDelegate::CreateStatic(&FOBSandboxEnemyPatternPreviewSmoke::Start));

// Exercise the evaluated firing pose, not just the posture enum or the pre-shot aim frame.
// Uses the saved preview actors in a disposable PIE world and never saves the map.
class FOBProneFirePreviewSmoke
{
public:
    static void Start(const TArray<FString>& Args)
    {
        const FString Folder=FPaths::ProjectDir()/TEXT("Build/Testing/Verification/ProneFire")
            /(Args.Num() ? FPaths::GetCleanFilename(Args[0]) : TEXT("After"));
        IFileManager::Get().MakeDirectory(*Folder,true);
        FRequestPlaySessionParams Params;Params.GlobalMapOverride=TEXT("/Game/Maps/SandboxMap");Params.bAllowOnlineSubsystem=false;
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        GEditor->RequestPlaySession(Params);
        const double Started=FPlatformTime::Seconds();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Started,Folder](float) {
            auto* W=GEditor ? GEditor->PlayWorld.Get() : nullptr;
            if (W && W->GetTimeSeconds()>.6f)
                for (TActorIterator<AOBEnemyPatternPreview> It(W);It;++It)
                    if (It->HasStartedPreview())
                        for (const auto& Entry : It->Entries)
                            if (Entry.Pattern==EOBEnemyCombatPattern::Prone && Entry.Enemy)
                            { Run(W,Entry.Enemy,Folder);return false; }
            if (FPlatformTime::Seconds()-Started>60.) { FPlatformMisc::RequestExitWithStatus(false,1);return false; }
            return true;
        }),.05f);
    }
private:
    static void Run(UWorld* W,AOBEnemyCharacter* Enemy,const FString& Folder)
    {
        struct FState
        {
            int32 LastShots=0,FireSamples=0,Capture=0;
            float Started=0,ShotAt=0,MinimumArmZ=MAX_flt,MaximumGripError=0,MaximumPelvisDrift=0;
            FVector Pelvis=FVector::ZeroVector;
            bool HasAim=false;
            FString CSV=TEXT("seconds,action,shots,head_z,elbow_l_z,elbow_r_z,hand_l_z,hand_r_z,grip_error_cm,pelvis_world_z,pelvis_mesh_z\n");
            FTimerHandle Timer;
        };
        auto S=MakeShared<FState>();S->Started=W->GetTimeSeconds();
        auto* Camera=W->SpawnActor<ACameraActor>();
        const FVector Center=Enemy->GetActorLocation()+FVector(15,0,-45);
        Camera->SetActorLocation(Center+FVector(140,-270,65));
        Camera->SetActorRotation((Center-Camera->GetActorLocation()).Rotation());
        Camera->GetCameraComponent()->SetFieldOfView(50.f);Camera->GetCameraComponent()->bConstrainAspectRatio=false;
        W->GetFirstPlayerController()->SetViewTarget(Camera);
        TWeakObjectPtr<UWorld> World=W;TWeakObjectPtr<AOBEnemyCharacter> Weak=Enemy;
        W->GetTimerManager().SetTimer(S->Timer,FTimerDelegate::CreateLambda([S,World,Weak,Folder]() {
            auto* W=World.Get();auto* Enemy=Weak.Get();if (!W || !Enemy) return;
            auto* Body=Enemy->GetMesh();auto* Grip=Enemy->GetWeaponGripComponent();auto* Weapon=Enemy->GetWeaponMesh();
            const float Now=W->GetTimeSeconds();const auto Action=Enemy->GetPatternAction();const int32 Shots=Enemy->GetPatternShotsFired();
            const FVector Left=Body->GetSocketLocation(TEXT("hand_l")),Right=Body->GetSocketLocation(TEXT("hand_r"));
            const float ElbowL=Body->GetSocketLocation(TEXT("lowerarm_l")).Z,ElbowR=Body->GetSocketLocation(TEXT("lowerarm_r")).Z;
            const float GripError=FVector::Distance(Left,(Grip->LeftHandInWeapon*Weapon->GetComponentTransform()).GetLocation());
            if (Action==EOBEnemyPatternAction::Aim && !S->HasAim && Now-S->Started>.75f)
            {
                S->HasAim=true;S->Pelvis=Body->GetSocketTransform(TEXT("pelvis"),RTS_Component).GetLocation();
                FScreenshotRequest::RequestScreenshot(Folder/TEXT("Prone-Aim.png"),false,false);
            }
            if (Shots>S->LastShots) { S->ShotAt=Now;S->LastShots=Shots; }
            if (Action==EOBEnemyPatternAction::Fire && S->HasAim && Now-S->ShotAt>.025f)
            {
                if (!S->FireSamples) S->Pelvis=Body->GetSocketTransform(TEXT("pelvis"),RTS_Component).GetLocation();
                ++S->FireSamples;S->MinimumArmZ=FMath::Min(S->MinimumArmZ,FMath::Min(FMath::Min(Left.Z,Right.Z),FMath::Min(ElbowL,ElbowR)));
                S->MaximumGripError=FMath::Max(S->MaximumGripError,GripError);
                S->MaximumPelvisDrift=FMath::Max(S->MaximumPelvisDrift,float(FVector::Distance(S->Pelvis,Body->GetSocketTransform(TEXT("pelvis"),RTS_Component).GetLocation())));
                const float Times[]={.05f,.15f,.30f,.48f};
                if (Shots>=1 && Shots<=5 && S->Capture<UE_ARRAY_COUNT(Times) && Now-S->ShotAt>=Times[S->Capture])
                {
                    FScreenshotRequest::RequestScreenshot(Folder/FString::Printf(TEXT("Prone-Fire-%02d.png"),S->Capture),false,false);++S->Capture;
                }
            }
            S->CSV+=FString::Printf(TEXT("%.4f,%s,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n"),Now-S->Started,
                *UEnum::GetValueAsString(Action),Shots,Body->GetSocketLocation(TEXT("head")).Z,ElbowL,ElbowR,Left.Z,Right.Z,GripError,
                Body->GetSocketLocation(TEXT("pelvis")).Z,Body->GetSocketTransform(TEXT("pelvis"),RTS_Component).GetLocation().Z);
            if (Now-S->Started<25.f && !(Shots>=10 && Action==EOBEnemyPatternAction::Reload)) return;
            const bool OK=S->HasAim && S->FireSamples>=30 && S->Capture==4 && Shots>=10
                && S->MinimumArmZ>=5.9f && S->MaximumGripError<1.f && S->MaximumPelvisDrift<.1f;
            const FString Report=FString::Printf(TEXT("shots=%d\nfire_pose_samples=%d\nminimum_arm_bone_height_cm=%.4f\nmaximum_support_hand_error_cm=%.4f\nmaximum_pelvis_drift_cm=%.4f\nfire_screenshots=%d\noutcome=%s\n"),
                Shots,S->FireSamples,S->MinimumArmZ,S->MaximumGripError,S->MaximumPelvisDrift,S->Capture,OK ? TEXT("PASS") : TEXT("FAIL"));
            FFileHelper::SaveStringToFile(S->CSV,*(Folder/TEXT("prone-fire-samples.csv")));
            FFileHelper::SaveStringToFile(Report,*(Folder/TEXT("prone-fire-pie.txt")));UE_LOG(LogTemp,Display,TEXT("PRONE_FIRE_PIE\n%s"),*Report);
            W->GetTimerManager().ClearTimer(S->Timer);GEditor->RequestEndPlayMap();
            FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([OK](float) {
                if (GEditor && GEditor->PlayWorld) return true;FPlatformMisc::RequestExitWithStatus(false,OK ? 0 : 1);return false;
            }),.5f);
        }),1.f/60.f,true);
    }
};
static FAutoConsoleCommand ProneFireCommand(TEXT("OB.AI.ProneFirePIE"),TEXT("Measure and capture prone arms throughout two real StateTree firing bursts in the saved SandboxMap. Optional output subfolder."),FConsoleCommandWithArgsDelegate::CreateStatic(&FOBProneFirePreviewSmoke::Start));
#endif

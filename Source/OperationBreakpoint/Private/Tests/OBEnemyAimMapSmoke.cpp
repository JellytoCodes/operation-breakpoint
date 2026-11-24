#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Editor.h"
#include "LevelEditor.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "Animation/OBEnemyAnimInstance.h"
#include "AI/OBAIController.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Character/OBEnemyCharacter.h"
#include "Character/OBHostageCharacter.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Character/OBActionComponent.h"
#include "Character/OBWeaponGripComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/OBPlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MediaPipe/OBMediaPipeInputComponent.h"
#include "Stage/OBStageFlowComponent.h"
#include "Widget/OBHUD.h"
#include "Widget/OBPlayerOverlayWidget.h"

// Disposable actual-game animation evaluation. It never saves the PIE world or an asset package.
class FOBEnemyAimMapSmoke
{
    struct FPoseSample
    {
        float BarrelError=180.f,LeftPositionError=0,LeftRotationError=0;
        float RightPositionError=0,RightRotationError=0;
        FVector Head=FVector::ZeroVector,Body=FVector::ZeroVector;
        bool bBoxesFollowBones=false,bRightAttached=false;
    };
    static AOBEnemyCharacter* SpawnStagedEnemy(UWorld* W,const TCHAR* Path,const FVector& Location)
    {
        auto* Class=LoadClass<AOBEnemyCharacter>(nullptr,Path);if (!Class) return nullptr;
        FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Enemy=W->SpawnActor<AOBEnemyCharacter>(Class,Location,FRotator::ZeroRotator,Params);if (!Enemy) return nullptr;
        Enemy->GetCharacterMovement()->DisableMovement();
        Enemy->EncounterBehavior.EntryMode=EOBEnemyEntryMode::WaitAtSpawn;
        Enemy->EncounterBehavior.bFacePlayer=false;Enemy->EncounterBehavior.bStartStateTreeAfterReaction=false;
        if (!Enemy->GetController()) Enemy->SpawnDefaultController();
        auto* AI=Cast<AOBAIController>(Enemy->GetController());
        if (!AI || !AI->PrepareEncounterEntry(Enemy->GetActorTransform(),Enemy->GetActorTransform(),0.f)) { Enemy->Destroy();return nullptr; }
        AI->StopStateTree();AI->SetActorTickEnabled(false);
        Enemy->SetCombatState(EOBEnemyCombatState::PreparingAttack);
        TInlineComponentArray<USkeletalMeshComponent*> Meshes;Enemy->GetComponents(Meshes);
        for (auto* Mesh : Meshes)
        {
            Mesh->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
            Mesh->bEnableUpdateRateOptimizations=false;Mesh->SetForcedLOD(1);
        }
        return Enemy;
    }
    static bool Measure(AOBEnemyCharacter* Enemy,AOBPlayerCharacter* Player,FPoseSample& Out)
    {
        auto* Body=Enemy->GetMesh();auto* Weapon=Enemy->GetWeaponMesh();auto* Grip=Enemy->GetWeaponGripComponent();
        if (!Body || !Weapon || !Grip) return false;
        FVector Muzzle,Barrel;
        if (Enemy->IsRPGTrooper())
        {
            if (!Enemy->RPGLauncherMesh || !Enemy->RPGLauncherMesh->GetStaticMesh()) return false;
            const FTransform Launcher=Enemy->RPGLauncherMesh->GetComponentTransform();
            Muzzle=Launcher.TransformPosition(Enemy->RPGMuzzleOffset);Barrel=Launcher.TransformVectorNoScale(FVector::UpVector).GetSafeNormal();
        }
        else
        {
            if (!Weapon->DoesSocketExist(TEXT("Muzzle")) || !Weapon->DoesSocketExist(Enemy->GetMuzzleSocketName())) return false;
            Muzzle=Weapon->GetSocketLocation(Enemy->GetMuzzleSocketName());
            Barrel=Weapon->GetSocketTransform(TEXT("Muzzle"),RTS_World).GetRotation().GetForwardVector();
        }
        const FVector Desired=(Enemy->GetDirectAimTarget(*Player)-Muzzle).GetSafeNormal();
        Out.BarrelError=FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Barrel,Desired),-1.f,1.f)));
        const FTransform LeftTarget=Grip->LeftHandInWeapon*Weapon->GetComponentTransform();
        const FTransform Left=Body->GetSocketTransform(TEXT("hand_l"),RTS_World);
        Out.LeftPositionError=FVector::Distance(Left.GetLocation(),LeftTarget.GetLocation());
        Out.LeftRotationError=Left.GetRotation().AngularDistance(LeftTarget.GetRotation());
        const FTransform RightTarget=Grip->WeaponInRightHand*Body->GetSocketTransform(TEXT("hand_r"),RTS_World);
        Out.RightPositionError=FVector::Distance(RightTarget.GetLocation(),Weapon->GetComponentLocation());
        Out.RightRotationError=RightTarget.GetRotation().AngularDistance(Weapon->GetComponentQuat());
        Out.bRightAttached=Weapon->GetAttachParent()==Body && Weapon->GetAttachSocketName()==TEXT("hand_r");
        int32 BoxCount=0;bool bMatches=true;TInlineComponentArray<UBoxComponent*> Boxes;Enemy->GetComponents(Boxes);
        for (auto* Box : Boxes)
        {
            if (Box->GetFName()!=TEXT("HeadHitBox") && Box->GetFName()!=TEXT("BodyHitBox")) continue;
            ++BoxCount;const FTransform Expected=Box->GetRelativeTransform()*Body->GetSocketTransform(Box->GetAttachSocketName(),RTS_World);
            bMatches &= Box->GetAttachParent()==Body && Box->GetComponentLocation().Equals(Expected.GetLocation(),.1f)
                && Box->GetComponentQuat().AngularDistance(Expected.GetRotation())<.002f;
            if (Box->GetFName()==TEXT("HeadHitBox")) Out.Head=Box->GetComponentLocation();else Out.Body=Box->GetComponentLocation();
        }
        Out.bBoxesFollowBones=BoxCount==2 && bMatches;
        return FMath::IsFinite(Out.BarrelError);
    }
public:
    static void Start()
    {
        FRequestPlaySessionParams Params;Params.GlobalMapOverride=TEXT("/Game/Maps/Stage_01");Params.bAllowOnlineSubsystem=false;
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        GEditor->RequestPlaySession(Params);const double Began=FPlatformTime::Seconds();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Began](float) {
            auto* W=GEditor ? GEditor->PlayWorld.Get() : nullptr;auto* PC=W ? Cast<AOBPlayerController>(W->GetFirstPlayerController()) : nullptr;
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
            int32 Step=0,Pose=0;double Began=0,Mark=0;bool OK=true,bRPG=false;
            FVector Base=FVector(100000,100000,5000);float StandingHalfHeight=0,StandingTargetZ=0,StandingPitch=0;
            FPoseSample Baseline;TWeakObjectPtr<AOBEnemyCharacter> Enemy;
            TArray<TWeakObjectPtr<AActor>> Actors;TWeakObjectPtr<ACameraActor> Camera;
            FString Log,CSV=TEXT("role,case,baseline_error_deg,aim_error_deg,pitch,yaw,alpha,left_error_cm,left_error_rad,right_error_cm,right_error_rad,head_delta_cm,body_delta_cm,target_error_cm\n");
            FTimerHandle Timer;
        };
        auto S=MakeShared<FState>();S->Began=S->Mark=W->GetTimeSeconds();S->StandingHalfHeight=P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        const FString Folder=FPaths::ProjectDir()/TEXT("Build/Testing/Verification/EnemyAimMM");IFileManager::Get().MakeDirectory(*Folder,true);
        auto* F=P->GetStageFlowComponent();F->ClearPresentation();F->RestoreOpeningMovement();F->Phase=EOBStagePhase::Combat;F->CurrentZone=1;F->RemainingSeconds=120.f;
        P->GetPlayerSplineComponent()->SetStageMovementBlocked(true);P->GetCharacterMovement()->DisableMovement();P->SetCanBeDamaged(false);
        P->GetActionComponent()->ResetEncounterForQA();P->GetActionComponent()->SetExternalActionState(true);
        if (auto* Pose=P->FindComponentByClass<UOBMediaPipeInputComponent>()) Pose->SetPoseInputEnabled(false);
        auto* PC=Cast<AOBPlayerController>(P->GetController());
        if (auto* HUD=PC ? PC->GetOBHUD() : nullptr; HUD && HUD->GetPlayerOverlayWidget()) HUD->GetPlayerOverlayWidget()->SetVisibility(ESlateVisibility::Collapsed);
        TWeakObjectPtr<UWorld> Weak=W;
        W->GetTimerManager().SetTimer(S->Timer,FTimerDelegate::CreateLambda([S,Weak,Folder]() {
            auto* World=Weak.Get();if (!World) return;auto* Controller=Cast<AOBPlayerController>(World->GetFirstPlayerController());
            auto* Player=Controller ? Cast<AOBPlayerCharacter>(Controller->GetPawn()) : nullptr;if (!Player) return;
            auto* Flow=Player->GetStageFlowComponent();auto* Enemy=S->Enemy.Get();
            auto* Anim=Enemy ? Cast<UOBEnemyAnimInstance>(Enemy->GetMesh()->GetAnimInstance()) : nullptr;
            const auto KeepAlive=S;
            auto Check=[&](bool Good,const TCHAR* Label) { S->OK &= Good;S->Log+=FString::Printf(TEXT("%s %s\n"),Good ? TEXT("PASS") : TEXT("FAIL"),Label); };
            auto Finish=[&]() {
                Controller->SetViewTarget(Player);
                for (auto Actor : S->Actors) if (Actor.IsValid())
                { if (auto* Pawn=Cast<APawn>(Actor.Get()); Pawn && Pawn->GetController()) Pawn->GetController()->Destroy();Actor->Destroy(); }
                S->Log+=FString::Printf(TEXT("outcome=%s\n"),S->OK ? TEXT("PASS") : TEXT("FAIL"));
                FFileHelper::SaveStringToFile(S->Log,*(Folder/TEXT("aim-pie.txt")));FFileHelper::SaveStringToFile(S->CSV,*(Folder/TEXT("aim-samples.csv")));
                UE_LOG(LogTemp,Display,TEXT("ENEMY_AIM_PIE\n%s"),*S->Log);
                const bool OK=S->OK;World->GetTimerManager().ClearTimer(S->Timer);GEditor->RequestEndPlayMap();
                FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([OK](float) {
                    if (GEditor && GEditor->PlayWorld) return true;FPlatformMisc::RequestExitWithStatus(false,OK ? 0 : 1);return false;
                }),.5f);
            };
            auto ConfigurePose=[&]() {
                const FVector Offsets[]={FVector(800,0,0),FVector(800,-400,0),FVector(800,400,0),FVector(800,0,400),FVector(800,0,-300),FVector(800,0,0),FVector(450,-350,260)};
                const FVector RPGOffsets[]={FVector(800,350,0),FVector(800,-300,350)};
                const FVector Offset=S->bRPG ? RPGOffsets[S->Pose] : Offsets[S->Pose];
                auto* Movement=Player->GetCharacterMovement();Movement->SetMovementMode(MOVE_Walking);Movement->UnCrouch(false);
                Player->SetActorLocation(S->Base+Offset,false,nullptr,ETeleportType::TeleportPhysics);
                if (!S->bRPG && S->Pose==5) Movement->Crouch(false);
                Movement->StopMovementImmediately();Movement->DisableMovement();
                Anim->bEnableUpperBodyAim=false;S->Mark=World->GetTimeSeconds();S->Step=1;
            };
            if (World->GetTimeSeconds()-S->Began>80) { Check(false,TEXT("Aim pose scenario deadline"));Finish();return; }
            if (S->Step==0)
            {
                auto* E=SpawnStagedEnemy(World,TEXT("/Game/Blueprints/Enemy/BP_EnemyCharacter.BP_EnemyCharacter_C"),S->Base);S->Enemy=E;
                Check(E!=nullptr,TEXT("Saved rifle enemy Blueprint spawns with a completed staging entry"));
                if (!E) { Finish();return; }S->Actors.Add(E);Enemy=E;Anim=Cast<UOBEnemyAnimInstance>(E->GetMesh()->GetAnimInstance());
                Check(Anim!=nullptr,TEXT("Saved rifle AnimBP uses the native aim context and pose proxy"));if (!Anim) { Finish();return; }
                auto* Camera=World->SpawnActor<ACameraActor>(S->Base+FVector(350,-400,100),FRotator::ZeroRotator);
                if (Camera) { Camera->SetActorRotation((S->Base+FVector(0,0,20)-Camera->GetActorLocation()).Rotation());Camera->GetCameraComponent()->SetFieldOfView(60.f);Controller->SetViewTarget(Camera);S->Actors.Add(Camera);S->Camera=Camera; }
                ConfigurePose();return;
            }
            if (!Enemy || !Anim) { Check(false,TEXT("Fixture enemy and native animation remain available"));Finish();return; }
            if (S->Step==1)
            {
                if (World->GetTimeSeconds()-S->Mark<.45) return;
                Check(Measure(Enemy,Player,S->Baseline),TEXT("Baseline measures the real barrel axis and weapon pose"));
                Check(Anim->AimAlpha==0.f && !Anim->GetEnemyAimContext().bActive,TEXT("Aim OFF supplies an uncorrected baseline"));
                Anim->bEnableUpperBodyAim=true;S->Mark=World->GetTimeSeconds();S->Step=2;return;
            }
            if (S->Step==2)
            {
                if (World->GetTimeSeconds()-S->Mark<1.25) return;
                const TCHAR* Names[]={TEXT("Standing"),TEXT("Left"),TEXT("Right"),TEXT("Above"),TEXT("Below"),TEXT("Crouch"),TEXT("Teleport")};
                const FString Name=S->bRPG ? (S->Pose==0 ? TEXT("RPG_Right") : TEXT("RPG_AboveLeft")) : Names[S->Pose];
                FPoseSample Sample;Check(Measure(Enemy,Player,Sample),TEXT("Aim ON measures actual evaluated muzzle and collision boxes"));
                const auto& Context=Anim->GetEnemyAimContext();const float TargetError=FVector::Distance(Context.TargetWorld,Context.RawTargetWorld);
                const float HeadDelta=FVector::Distance(Sample.Head,S->Baseline.Head),BodyDelta=FVector::Distance(Sample.Body,S->Baseline.Body);
                S->CSV+=FString::Printf(TEXT("%s,%s,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f\n"),S->bRPG ? TEXT("RPG") : TEXT("Rifle"),*Name,S->Baseline.BarrelError,Sample.BarrelError,Anim->AimPitch,Anim->AimYaw,Anim->AimAlpha,Sample.LeftPositionError,Sample.LeftRotationError,Sample.RightPositionError,Sample.RightRotationError,HeadDelta,BodyDelta,TargetError);
                S->Log+=FString::Printf(TEXT("INFO Case=%s Before=%.3fdeg After=%.3fdeg Pitch=%.3f Yaw=%.3f Alpha=%.3f Grip=%.4fcm/%.4frad HeadDelta=%.3fcm BodyDelta=%.3fcm\n"),*Name,S->Baseline.BarrelError,Sample.BarrelError,Anim->AimPitch,Anim->AimYaw,Anim->AimAlpha,Sample.LeftPositionError,Sample.LeftRotationError,HeadDelta,BodyDelta);
                Check(Context.bActive && Anim->AimAlpha>.99f && Context.RawTargetWorld.Equals(Enemy->GetDirectAimTarget(*Player),.1f) && TargetError<1.f,TEXT("Settled native context follows the actual current player target"));
                Check(Sample.BarrelError<5.f && (S->Baseline.BarrelError>5.f ? Sample.BarrelError<S->Baseline.BarrelError-2.f : Sample.BarrelError<=S->Baseline.BarrelError+1.f),TEXT("Evaluated physical barrel converges toward the player beyond the uncorrected baseline"));
                Check(Sample.bRightAttached && Sample.RightPositionError<.05f && Sample.RightRotationError<.002f,TEXT("Right hand weapon attachment survives upper-body aiming"));
                Check(Sample.bBoxesFollowBones,TEXT("Head and body hit boxes follow their evaluated attachment bones"));
                Check(FMath::Abs(Enemy->GetActorRotation().Yaw)<.1f && Enemy->GetActorLocation().Equals(S->Base,.1f),TEXT("Upper-body aiming works without rotating or moving the enemy actor"));
                const bool bCapturePose=!S->bRPG && (S->Pose==1 || S->Pose==3 || S->Pose==5);
                if (!S->bRPG)
                {
                    Check(Enemy->GetWeaponGripComponent()->ShouldApplyGrip() && Sample.LeftPositionError<.5f && Sample.LeftRotationError<.02f,TEXT("Support wrist and palm retain the existing rifle grip"));
                    if (S->Pose==0) { S->StandingTargetZ=Context.RawTargetWorld.Z;S->StandingPitch=Anim->AimPitch; }
                    if (S->Pose==5) Check(Player->bIsCrouched && Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()<S->StandingHalfHeight
                        && Context.RawTargetWorld.Z<S->StandingTargetZ-10.f && Anim->AimPitch<S->StandingPitch-1.f,TEXT("A real crouched capsule lowers the actual target and visual aim pitch"));
                    if (S->Baseline.BarrelError>8.f) Check(HeadDelta>.1f && BodyDelta>.1f,TEXT("The aimed pose changes actual upper-body and head hit-box positions"));
                    if (bCapturePose) FScreenshotRequest::RequestScreenshot(Folder/(Name+TEXT(".png")),false,false);
                }
                else Check(Enemy->RPGLauncherMesh->IsVisible() && !Enemy->RPGLauncherMesh->bHiddenInGame,TEXT("RPG measurement uses the visible launcher rather than the hidden rifle"));
                ++S->Pose;
                if (S->Pose<(S->bRPG ? 2 : 7))
                {
                    // Keep the aimed pose intact until the requested frame has rendered.
                    if (bCapturePose) { S->Mark=World->GetTimeSeconds();S->Step=9;return; }
                    ConfigurePose();return;
                }
                if (S->bRPG) { Finish();return; }
                Enemy->GetCharacterMovement()->SetMovementMode(MOVE_Flying);Enemy->GetCharacterMovement()->Velocity=FVector(120,0,0);
                S->Mark=World->GetTimeSeconds();S->Step=3;return;
            }
            if (S->Step==9)
            {
                if (World->GetTimeSeconds()-S->Mark<.2) return;
                ConfigurePose();return;
            }
            if (S->Step==3)
            {
                if (World->GetTimeSeconds()-S->Mark<.35) return;
                Check(Enemy->GetVelocity().SizeSquared2D()>9.f && Anim->AimAlpha==0.f && !Anim->GetEnemyAimContext().bActive,TEXT("Actual locomotion disables and clears the aim correction"));
                Enemy->GetCharacterMovement()->StopMovementImmediately();Enemy->GetCharacterMovement()->DisableMovement();Enemy->SetActorLocation(S->Base);
                Enemy->GetWeaponMesh()->SetHiddenInGame(true);S->Mark=World->GetTimeSeconds();S->Step=4;return;
            }
            if (S->Step==4)
            {
                if (World->GetTimeSeconds()-S->Mark<.3) return;
                Check(Anim->AimAlpha==0.f && !Anim->GetEnemyAimContext().bActive,TEXT("Hiding the rifle removes its stale upper-body aim correction"));
                Enemy->GetWeaponMesh()->SetHiddenInGame(false);Flow->SuspendForQA();S->Mark=World->GetTimeSeconds();S->Step=5;return;
            }
            if (S->Step==5)
            {
                if (World->GetTimeSeconds()-S->Mark<.3) return;
                Check(Anim->AimAlpha==0.f && !Anim->GetEnemyAimContext().bActive,TEXT("QA suspension clears native aim state"));
                Flow->Phase=EOBStagePhase::Combat;Flow->RemainingSeconds=120.f;Player->GetPlayerSplineComponent()->SetStageMovementBlocked(true);
                S->Mark=World->GetTimeSeconds();S->Step=6;return;
            }
            if (S->Step==6)
            {
                if (World->GetTimeSeconds()-S->Mark<1.25) return;
                Check(Anim->AimAlpha>.99f && Anim->GetEnemyAimContext().bActive,TEXT("Combat resumption rebuilds aim from the current target"));
                Enemy->SetCombatState(EOBEnemyCombatState::Idle);S->Mark=World->GetTimeSeconds();S->Step=7;return;
            }
            if (S->Step==7)
            {
                if (World->GetTimeSeconds()-S->Mark<.3) return;
                Check(Anim->AimAlpha==0.f && !Anim->GetEnemyAimContext().bActive,TEXT("Idle state cannot retain the last attack aim pose"));
                FDamageEvent Damage;Enemy->TakeDamage(Enemy->GetMaxHealth()+1.f,Damage,Controller,Player);
                S->Mark=World->GetTimeSeconds();S->Step=8;return;
            }
            if (S->Step==8)
            {
                if (World->GetTimeSeconds()-S->Mark<1.4) return;
                Check(Enemy->GetCombatState()==EOBEnemyCombatState::Dead && Anim->AimAlpha==0.f && !Anim->GetEnemyAimContext().bActive
                    && !Enemy->GetWeaponGripComponent()->ShouldApplyGrip(),TEXT("Normal death releases both aim correction and the weapon grip"));
                Check(Enemy->GetWeaponMesh()->GetAttachParent()!=Enemy->GetMesh() && Enemy->GetWeaponMesh()->IsSimulatingPhysics(),
                    TEXT("The MM death montage still fires the authored weapon-drop notify"));
                Check(Enemy->GetMesh()->IsAnySimulatingPhysics(),TEXT("The MM death montage still fires the authored ragdoll notify"));
                auto* HostageClass=LoadClass<AOBHostageCharacter>(nullptr,TEXT("/Game/Blueprints/Hostage/BP_HostageCharacter.BP_HostageCharacter_C"));
                auto* Hostage=HostageClass ? World->SpawnActor<AOBHostageCharacter>(HostageClass,S->Base+FVector(0,1200,0),FRotator::ZeroRotator) : nullptr;
                Check(Hostage && Hostage->GetWeaponMesh()==nullptr && !Hostage->IsEncounterCombatant()
                    && !Cast<UOBEnemyAnimInstance>(Hostage->GetMesh()->GetAnimInstance()),TEXT("Dedicated hostage pose remains outside enemy aiming and grip evaluation"));
                if (Hostage) S->Actors.Add(Hostage);
                S->Base+=FVector(0,3000,0);
                auto* RPG=SpawnStagedEnemy(World,TEXT("/Game/Blueprints/Enemy/BP_RPGTrooper.BP_RPGTrooper_C"),S->Base);
                Check(RPG && RPG->IsRPGTrooper(),TEXT("Saved RPG Blueprint uses the same evaluated aim path"));if (!RPG) { Finish();return; }
                S->Actors.Add(RPG);S->Enemy=RPG;Enemy=RPG;Anim=Cast<UOBEnemyAnimInstance>(RPG->GetMesh()->GetAnimInstance());
                if (!Anim) { Check(false,TEXT("RPG has a native enemy animation instance"));Finish();return; }
                S->bRPG=true;S->Pose=0;ConfigurePose();
            }
        }),.05f,true);
    }
};
static FAutoConsoleCommand GOBEnemyAimPIE(TEXT("OB.Enemy.AimPIE"),
    TEXT("Disposable Stage_01 physical barrel tracking, grip, hit-box and aim-gate PIE verification. Exits this editor."),
    FConsoleCommandDelegate::CreateStatic(&FOBEnemyAimMapSmoke::Start));
#endif

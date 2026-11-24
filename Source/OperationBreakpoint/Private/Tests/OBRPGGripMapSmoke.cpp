#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Editor.h"
#include "LevelEditor.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Character/OBEnemyCharacter.h"
#include "Character/OBWeaponGripComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "World/OBEnemyPatternPreview.h"

// Actual saved RPG Blueprint and StateTree, including release/regrip across two reloads.
class FOBRPGGripMapSmoke
{
public:
    static void Start(const TArray<FString>& Args)
    {
        const bool Before=Args.Num() && Args[0]==TEXT("Before");
        FRequestPlaySessionParams Params;Params.GlobalMapOverride=TEXT("/Game/Maps/SandboxMap");Params.bAllowOnlineSubsystem=false;
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        GEditor->RequestPlaySession(Params);const double Started=FPlatformTime::Seconds();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Started,Before](float) {
            auto* W=GEditor ? GEditor->PlayWorld.Get() : nullptr;
            if (W && W->GetTimeSeconds()>.6f)
                for (TActorIterator<AOBEnemyPatternPreview> It(W);It;++It)
                    if (It->HasStartedPreview())
                        for (const auto& Entry : It->Entries)
                            if (Entry.Pattern==EOBEnemyCombatPattern::RPG && Entry.Enemy)
                            { Run(W,Entry.Enemy,Before);return false; }
            if (FPlatformTime::Seconds()-Started>60.) { FPlatformMisc::RequestExitWithStatus(false,1);return false; }
            return true;
        }),.1f);
    }
private:
    // FScreenshotRequest can be consumed by an editor viewport before the PIE viewport.
    // Capture the actual PIE actors directly so editor selections and free cameras cannot substitute a frame.
    static bool Capture(USceneCaptureComponent2D* View,const FString& File)
    {
        View->CaptureScene();FlushRenderingCommands();
        TArray<FColor> Pixels;
        if (!View->TextureTarget->GameThread_GetRenderTargetResource()->ReadPixels(Pixels)) return false;
        TArray<uint8> PNG;FImageUtils::CompressImageArray(View->TextureTarget->SizeX,View->TextureTarget->SizeY,Pixels,PNG);
        return FFileHelper::SaveArrayToFile(PNG,*File);
    }

    static void Run(UWorld* W,AOBEnemyCharacter* Enemy,bool Before)
    {
        const FString Folder=FPaths::ProjectDir()/TEXT("Build/Testing/Verification/RPGGrip")/(Before ? TEXT("Before") : TEXT("After"));
        IFileManager::Get().MakeDirectory(*Folder,true);
        auto* Grip=Enemy->GetWeaponGripComponent();
        if (Before)
        {
            Grip->bRPGEnabled=false;
            // Exact attachment from the pre-fix BP_RPGTrooper snapshot (inspection.json).
            Enemy->RPGWeaponMesh->SetRelativeTransform(FTransform(FQuat(-.080046717,-.080046803,.702561392,.702561402),FVector(-10.3705845,4.7112694,-1.0355588)));
        }
        struct FState
        {
            float Started=0,ActionAt=0,MaxLeftError=0,MaxRightError=0,MaxRotationError=0,MaxBarrelError=0;
            int32 Samples=0,LastShots=0;
            bool CameraPositioned=false,AimCapture=false,FireCapture=false,ReloadCapture=false,ReloadReleased=false;
            EOBEnemyPatternAction LastAction=EOBEnemyPatternAction::Posture;
            FString CSV=TEXT("seconds,action,shots,grip_active,left_error_cm,right_error_cm,left_error_rad,barrel_error_deg\n");
            FTimerHandle Timer;TWeakObjectPtr<ACameraActor> Camera;TWeakObjectPtr<USceneCaptureComponent2D> View;
        };
        auto S=MakeShared<FState>();S->Started=S->ActionAt=W->GetTimeSeconds();
        S->Camera=W->SpawnActor<ACameraActor>();S->Camera->GetCameraComponent()->SetFieldOfView(42.f);
        S->Camera->GetCameraComponent()->bConstrainAspectRatio=false;
        auto* View=NewObject<USceneCaptureComponent2D>(S->Camera.Get());S->View=View;
        View->SetupAttachment(S->Camera->GetRootComponent());View->FOVAngle=42.f;
        View->bCaptureEveryFrame=false;View->bCaptureOnMovement=false;
        View->CaptureSource=ESceneCaptureSource::SCS_BaseColor; // Inspect actual contact without lighting/shadow obscuring the fingers.
        View->TextureTarget=NewObject<UTextureRenderTarget2D>(View);View->TextureTarget->InitCustomFormat(1280,720,PF_B8G8R8A8,false);
        View->RegisterComponent();
        W->GetFirstPlayerController()->SetViewTarget(S->Camera.Get());
        TWeakObjectPtr<UWorld> World=W;TWeakObjectPtr<AOBEnemyCharacter> Weak=Enemy;
        W->GetTimerManager().SetTimer(S->Timer,FTimerDelegate::CreateLambda([S,World,Weak,Folder,Before]() {
            auto* W=World.Get();auto* Enemy=Weak.Get();if (!W || !Enemy) return;
            auto* Grip=Enemy->GetWeaponGripComponent();auto* Weapon=Enemy->RPGWeaponMesh.Get();auto* Body=Enemy->GetMesh();
            const float Now=W->GetTimeSeconds();const auto Action=Enemy->GetPatternAction();const int32 Shots=Enemy->GetPatternShotsFired();
            if (Action!=S->LastAction) { S->LastAction=Action;S->ActionAt=Now; }
            const FTransform Left=Body->GetSocketTransform(TEXT("hand_l")),Right=Body->GetSocketTransform(TEXT("hand_r"));
            const FTransform LeftTarget=Grip->RPGLeftHandInWeapon*Weapon->GetComponentTransform();
            const FTransform RightTarget=Grip->RPGWeaponInRightHand*Right;
            const float LeftError=FVector::Distance(Left.GetLocation(),LeftTarget.GetLocation());
            const float RightError=FVector::Distance(RightTarget.GetLocation(),Weapon->GetComponentLocation());
            const float RotationError=Left.GetRotation().AngularDistance(LeftTarget.GetRotation());
            const FVector Muzzle=Weapon->GetComponentTransform().TransformPosition(Enemy->RPGMuzzleOffset);
            const FVector Barrel=Weapon->GetComponentTransform().TransformVectorNoScale(Enemy->RPGBarrelAxis).GetSafeNormal();
            auto* Player=Cast<ACharacter>(W->GetFirstPlayerController()->GetPawn());
            const FVector Desired=Player ? (Enemy->GetDirectAimTarget(*Player)-Muzzle).GetSafeNormal() : Barrel;
            const float BarrelError=FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Barrel,Desired),-1.f,1.f)));
            if ((Action==EOBEnemyPatternAction::Aim || Action==EOBEnemyPatternAction::Fire) && Now-S->ActionAt>.2f)
            {
                ++S->Samples;S->MaxLeftError=FMath::Max(S->MaxLeftError,LeftError);S->MaxRightError=FMath::Max(S->MaxRightError,RightError);
                S->MaxRotationError=FMath::Max(S->MaxRotationError,RotationError);
                // The existing AimPitch/AimYaw interpolation intentionally takes time after reload.
                if (Action==EOBEnemyPatternAction::Fire || Now-S->ActionAt>.7f) S->MaxBarrelError=FMath::Max(S->MaxBarrelError,BarrelError);
            }
            if (Action==EOBEnemyPatternAction::Aim && !S->CameraPositioned && Now-S->ActionAt>.2f)
            {
                const FTransform Launcher=Weapon->GetComponentTransform();
                const FVector Center=Launcher.TransformPosition(FVector(0,-15,0));
                S->Camera->SetActorLocation(Launcher.TransformPosition(FVector(65,-15,-5)));
                S->Camera->SetActorRotation((Center-S->Camera->GetActorLocation()).Rotation());S->CameraPositioned=true;
            }
            if (Action==EOBEnemyPatternAction::Aim && !S->AimCapture && Now-S->ActionAt>.7f)
            {
                S->AimCapture=Capture(S->View.Get(),Folder/TEXT("RPG-Aim-Grip.png"));
                const FVector Location=S->Camera->GetActorLocation();const FRotator Rotation=S->Camera->GetActorRotation();
                const FTransform Launcher=Weapon->GetComponentTransform();const FVector Center=Launcher.TransformPosition(FVector(0,-15,0));
                S->Camera->SetActorLocation(Launcher.TransformPosition(FVector(-65,-15,-5)));S->Camera->SetActorRotation((Center-S->Camera->GetActorLocation()).Rotation());
                Capture(S->View.Get(),Folder/TEXT("RPG-Aim-Grip-OtherSide.png"));
                S->Camera->SetActorLocation(Location);S->Camera->SetActorRotation(Rotation);
            }
            if (S->AimCapture && Action==EOBEnemyPatternAction::Fire && !S->FireCapture && Now-S->ActionAt>.1f)
            { S->FireCapture=Capture(S->View.Get(),Folder/TEXT("RPG-Fire-Grip.png")); }
            if (Shots>0 && Action==EOBEnemyPatternAction::Reload && Now-S->ActionAt>.6f)
            {
                S->ReloadReleased |= !Grip->ShouldApplyGrip() && LeftError>1.f;
                if (!S->ReloadCapture)
                {
                    const FVector Center=Enemy->GetActorLocation()+FVector(0,0,-10);
                    S->Camera->SetActorLocation(Center+FVector(220,-300,120));S->Camera->SetActorRotation((Center-S->Camera->GetActorLocation()).Rotation());
                    S->ReloadCapture=Capture(S->View.Get(),Folder/TEXT("RPG-Reload-Released.png"));
                }
            }
            S->CSV+=FString::Printf(TEXT("%.4f,%s,%d,%d,%.4f,%.4f,%.5f,%.4f\n"),Now-S->Started,*UEnum::GetValueAsString(Action),Shots,Grip->ShouldApplyGrip(),LeftError,RightError,RotationError,BarrelError);
            if (Now-S->Started<30.f && !(Shots>=2 && Action==EOBEnemyPatternAction::Reload && Now-S->ActionAt>.8f)) return;
            const bool OK=S->Samples>30 && S->MaxLeftError<.5f && S->MaxRightError<.1f && S->MaxRotationError<.02f && S->MaxBarrelError<1.f
                && S->AimCapture && S->FireCapture && S->ReloadCapture && S->ReloadReleased;
            const FString Report=FString::Printf(TEXT("recreated_before_fix=%d\nrockets=%d\nheld_pose_samples=%d\nmaximum_left_wrist_error_cm=%.4f\nmaximum_right_attachment_error_cm=%.4f\nmaximum_left_wrist_rotation_error_rad=%.5f\nmaximum_barrel_error_deg=%.4f\nreload_releases_support_hand=%d\naim_fire_reload_captured=%d\noutcome=%s\n"),
                Before,Shots,S->Samples,S->MaxLeftError,S->MaxRightError,S->MaxRotationError,S->MaxBarrelError,S->ReloadReleased,S->AimCapture && S->FireCapture && S->ReloadCapture,OK ? TEXT("PASS") : TEXT("FAIL"));
            FFileHelper::SaveStringToFile(S->CSV,*(Folder/TEXT("rpg-grip-samples.csv")));FFileHelper::SaveStringToFile(Report,*(Folder/TEXT("rpg-grip-pie.txt")));
            UE_LOG(LogTemp,Display,TEXT("RPG_GRIP_PIE\n%s"),*Report);W->GetTimerManager().ClearTimer(S->Timer);GEditor->RequestEndPlayMap();
            FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([OK,Before](float) {
                if (GEditor && GEditor->PlayWorld) return true;FPlatformMisc::RequestExitWithStatus(false,OK || Before ? 0 : 1);return false;
            }),.5f);
        }),1.f/30.f,true);
    }
};
static FAutoConsoleCommand GOBRPGGripPIECommand(TEXT("OB.AI.RPGGripPIE"),TEXT("Capture RPG hand contact during actual SandboxMap aim, fire and reload. Optional Before recreates the previous attachment without grip IK."),FConsoleCommandWithArgsDelegate::CreateStatic(&FOBRPGGripMapSmoke::Start));
#endif

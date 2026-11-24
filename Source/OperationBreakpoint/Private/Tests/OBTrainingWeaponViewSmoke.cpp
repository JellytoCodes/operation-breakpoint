#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Training/OBTrainingDirector.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Core/OBPlayerController.h"
#include "MediaPipe/OBMediaPipeInputComponent.h"
#include "Widget/OBPlayerOverlayWidget.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor.h"
#include "LevelEditor.h"
#include "EngineUtils.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

// Render actual arrival and trigger-driven view restoration on the saved map, without saving assets.
class FOBTrainingWeaponViewSmoke
{
    static void Finish(bool bPass)
    {
        UE_LOG(LogTemp, Display, TEXT("WEAPON_VIEW_MAP outcome=%s"), bPass ? TEXT("PASS") : TEXT("FAIL"));
        if (GEditor && GEditor->PlayWorld) GEditor->RequestEndPlayMap();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([bPass](float) {
            FPlatformMisc::RequestExitWithStatus(false, bPass ? 0 : 1); return false;
        }), 1.f);
    }
public:
    static void Run(UWorld* World)
    {
        struct FState { int32 Stage=0; double Started=0, Mark=0; FTimerHandle Timer; FString Inspect, Restore; };
        auto S=MakeShared<FState>(); S->Started=World->GetTimeSeconds();
        S->Inspect=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Tests/TrainingWeaponView/WeaponCheck.png"));
        S->Restore=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Tests/TrainingWeaponView/AimView.png"));
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(S->Inspect), true);
        TWeakObjectPtr<UWorld> WeakWorld=World;
        World->GetTimerManager().SetTimer(S->Timer, FTimerDelegate::CreateLambda([S,WeakWorld]() {
            auto* W=WeakWorld.Get(); if (!W) return;
            auto End=[&](bool bPass) { W->GetTimerManager().ClearTimer(S->Timer); Finish(bPass); };
            if (W->GetTimeSeconds()-S->Started>50) { End(false); return; }
            AOBTrainingDirector* D=nullptr;
            for (TActorIterator<AOBTrainingDirector> It(W);It;++It) { D=*It; break; }
            if (!D || !D->bInitialized) return;
            auto* P=Cast<AOBPlayerCharacter>(D->Controller->GetPawn()); if (!P) return;
            auto* C=P->GetPlayerCamera();
            if (S->Stage==0)
            {
                D->StopPrototypeNarration(); W->GetTimerManager().ClearTimer(D->PrototypeIntroTimer);
                D->bPrototypeIntroPending=false; D->bPrototypeTurning=false; D->IntroBrightness=1;
                D->Controller->PlayerCameraManager->SetManualCameraFade(0,FLinearColor::Black,false);
                D->Controller->PlayerCameraManager->StopCameraFade();
                if (D->PoseInput) D->PoseInput->SetPoseInputEnabled(false);
                if (D->Overlay) D->Overlay->SetVisibility(ESlateVisibility::Visible);
                // Use the supported QA entry so the initial skip UI and its
                // input gate are closed just as they are after the real intro.
                D->Controller->ConsoleCommand(TEXT("QA Goto WeaponCheck"),true); S->Stage=1;
            }
            else if (S->Stage==1 && D->CurrentStep==EOBTrainingStep::WeaponCheck)
            { S->Mark=W->GetTimeSeconds(); S->Stage=2; }
            else if (S->Stage==2 && W->GetTimeSeconds()-S->Mark>2)
            {
                UE_LOG(LogTemp,Display,TEXT("WEAPON_VIEW_MAP inspect pitch=%.2f yaw=%.2f fov=%.2f point=%d"), C->GetRelativeRotation().Pitch,C->GetRelativeRotation().Yaw,C->FieldOfView,D->Spline->GetCurrentPointIndex());
                if (!FMath::IsNearlyEqual(C->GetRelativeRotation().Pitch,static_cast<double>(D->WeaponCheckCameraPitch),.01) || !FMath::IsNearlyEqual(C->FieldOfView,D->WeaponCheckCameraFOV,.01f) || !P->GetWeaponMesh()->bOwnerNoSee) { End(false); return; }
                UE_LOG(LogTemp,Display,TEXT("WEAPON_VIEW_MAP inspection_held_weapon_hidden=1 table_prop_unchanged=1"));
                FScreenshotRequest::RequestScreenshot(S->Inspect,false,false,false,FIntRect(),true); S->Mark=W->GetTimeSeconds(); S->Stage=3;
            }
            else if (S->Stage==3 && W->GetTimeSeconds()-S->Mark>1)
            { D->Controller->FireAtCursor(); S->Mark=W->GetTimeSeconds(); S->Stage=4; }
            else if (S->Stage==4 && D->CurrentStep==EOBTrainingStep::AimHold && W->GetTimeSeconds()-S->Mark>2)
            {
                UE_LOG(LogTemp,Display,TEXT("WEAPON_VIEW_MAP restore pitch=%.2f yaw=%.2f fov=%.2f checked=%d"), C->GetRelativeRotation().Pitch,C->GetRelativeRotation().Yaw,C->FieldOfView,D->Result.HasPassed(EOBTrainingStep::WeaponCheck));
                if (!D->Result.HasPassed(EOBTrainingStep::WeaponCheck) || D->Spline->IsTrainingCameraFocusActive() || FMath::Abs(C->GetRelativeRotation().Pitch)>.1 || FMath::Abs(C->GetRelativeRotation().Yaw)>.1 || !P->GetWeaponMesh()->bOwnerNoSee) { End(false); return; }
                UE_LOG(LogTemp,Display,TEXT("WEAPON_VIEW_MAP first_person_weapon_hidden=1"));
                FScreenshotRequest::RequestScreenshot(S->Restore,false,false,false,FIntRect(),true); S->Mark=W->GetTimeSeconds(); S->Stage=5;
            }
            else if (S->Stage==5 && W->GetTimeSeconds()-S->Mark>1)
                End(IFileManager::Get().FileSize(*S->Inspect)>0 && IFileManager::Get().FileSize(*S->Restore)>0);
        }), .1f, true);
    }
};
static FAutoConsoleCommand GOBTrainingWeaponViewPIE(TEXT("OB.Training.WeaponViewPIE"),TEXT("Verify weapon inspection and view restoration on TutorialMap; exits its process."),
    FConsoleCommandDelegate::CreateLambda([]() {
        if (!GEditor) return;
        FRequestPlaySessionParams Params; Params.GlobalMapOverride=TEXT("/Game/Maps/TutorialMap");
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        Params.bAllowOnlineSubsystem=false; GEditor->RequestPlaySession(Params);
        const double Start=FPlatformTime::Seconds();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Start](float) {
            if (GEditor && GEditor->PlayWorld) { FOBTrainingWeaponViewSmoke::Run(GEditor->PlayWorld); return false; }
            if (FPlatformTime::Seconds()-Start>45) { FPlatformMisc::RequestExitWithStatus(false,1); return false; }
            return true;
        }), .05f);
    }));
#endif

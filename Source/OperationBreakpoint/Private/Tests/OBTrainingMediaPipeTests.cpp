#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Training/OBTrainingDirector.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBActionComponent.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Core/OBPlayerController.h"
#include "MediaPipe/OBMediaPipeInputComponent.h"
#include "MediaPipe/OBMediaPipeOSCSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "OSCManager.h"
#include "OSCAddress.h"
#include "OSCMessage.h"
#include "OSCClient.h"
#include "TimerManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBTrainingMediaPipePoseTest,"OperationBreakpoint.Training.MediaPipePose",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOBTrainingMediaPipePoseTest::RunTest(const FString&)
{
    TGuardValue<bool> ScriptGuard(GAllowActorScriptExecutionInEditor,true);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Instance=NewObject<UGameInstance>();
    World->SetGameInstance(Instance);
    auto* Motion=NewObject<UOBMediaPipeOSCSubsystem>(Instance);
    auto* Player=World->SpawnActor<AOBPlayerCharacter>();
    auto* PC=World->SpawnActor<AOBPlayerController>(); PC->Possess(Player);
    auto* D=World->SpawnActor<AOBTrainingDirector>();
    auto* Pose=Player->FindComponentByClass<UOBMediaPipeInputComponent>();
    auto* Movement=Player->GetCharacterMovement();
    Pose->OwnerCharacter=Player; Pose->MediaPipeSubsystem=Motion;
    Motion->OnCrouchStateChanged.AddUObject(Pose,&UOBMediaPipeInputComponent::HandleCrouchStateChanged);
    D->Controller=PC; D->Action=Player->GetActionComponent(); D->Spline=Player->GetPlayerSplineComponent();
    D->Motion=Motion; D->PoseInput=Pose; D->bUseSplinePrototype=true;
    D->bEnableKeyboardTestInputs=true; D->bSavedPoseInputEnabled=true; D->PrototypeRouteIndex=1;
    ON_SCOPE_EXIT
    {
        D->EndPlay(EEndPlayReason::Destroyed); Motion->OnCrouchStateChanged.RemoveAll(Pose);
        World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    };
    auto Receive=[&](int32 Value) {
        FOSCMessage Message; UOSCManager::SetOSCMessageAddress(Message,FOSCAddress(TEXT("/motion/crouch")));
        UOSCManager::AddInt32(Message,Value); Motion->HandleOSCMessage(Message,TEXT("127.0.0.1"),8000);
    };
    const double StandingCameraZ=Player->GetPlayerCamera()->GetRelativeLocation().Z;
    D->Action->SetExternalActionState(false); Receive(1);
    TestTrue(TEXT("OSC crouch value is decoded"),Motion->IsCrouching());
    TestFalse(TEXT("Prepare still blocks posture changes"),Movement->bWantsToCrouch);
    D->BeginPrototypeExercise(EOBTrainingStep::Cover);
    TestTrue(TEXT("Held OSC crouch is reapplied on Cover entry with keyboard enabled"),Movement->bWantsToCrouch);
    D->Tick(0);
    TestEqual(TEXT("A movement request alone cannot pass Cover"),D->CurrentStep,EOBTrainingStep::Cover);
    Movement->Crouch(false); D->Tick(0);
    TestTrue(TEXT("MediaPipe applies physical character crouch"),Player->bIsCrouched);
    TestTrue(TEXT("MediaPipe lowers the Unreal camera"),Player->GetPlayerCamera()->GetRelativeLocation().Z<StandingCameraZ);
    TestEqual(TEXT("Actual crouch advances to Reload"),D->CurrentStep,EOBTrainingStep::Reload);
    Receive(0); Movement->UnCrouch(false);
    TestFalse(TEXT("Stand packet raises the character"),Player->bIsCrouched);
    TestEqual(TEXT("Stand restores camera height"),Player->GetPlayerCamera()->GetRelativeLocation().Z,StandingCameraZ);
    D->BeginPrototypeExercise(EOBTrainingStep::Cover); D->Action->SetExternalActionState(false); D->bPrototypePaused=true; D->bStepActive=false;
    Receive(1); TestFalse(TEXT("Paused Prepare blocks new crouch request"),Movement->bWantsToCrouch);
    D->ResumePrototype();
    TestTrue(TEXT("Resume reapplies crouch already received while paused"),Movement->bWantsToCrouch);
    Movement->Crouch(false); D->Tick(0);
    TestEqual(TEXT("Resume can complete Cover through real pose"),D->CurrentStep,EOBTrainingStep::Reload);
    Receive(0); Movement->UnCrouch(false); Pose->SetPoseInputEnabled(false); Receive(1);
    TestFalse(TEXT("Explicit component disable is still respected"),Movement->bWantsToCrouch);
    Pose->SetPoseInputEnabled(true); Motion->LastMessageReceivedTime=FPlatformTime::Seconds()-3;
    Pose->RefreshPoseState();
    TestFalse(TEXT("Stale cached posture is not reapplied"),Movement->bWantsToCrouch);
    D->bEnableKeyboardTestInputs=false; Receive(0); D->BeginPrototypeExercise(EOBTrainingStep::Cover); Receive(1);
    Movement->Crouch(false); D->Tick(0);
    TestEqual(TEXT("Hardware-only Cover also advances"),D->CurrentStep,EOBTrainingStep::Reload);
    // Advance the actual subsystem timer: unchanged refreshes extend its lease,
    // while a heartbeat alone must never keep an old crouch held.
    {
        TGuardValue<uint64> FrameGuard(GFrameCounter,GFrameCounter+1);
        auto& Timers=Instance->GetTimerManager();
        Timers.Tick(0.f);
        Receive(1);
        ++GFrameCounter; Timers.Tick(.5f);
        Receive(1);
        ++GFrameCounter; Timers.Tick(.5f);
        TestTrue(TEXT("Periodic identical crouch packets renew the timeout"),Motion->IsCrouching());
        Receive(2);
        TestTrue(TEXT("Malformed posture values do not change a held crouch"),Motion->IsCrouching());
        FOSCMessage Heartbeat;
        UOSCManager::SetOSCMessageAddress(Heartbeat,FOSCAddress(TEXT("/system/mediapipe/heartbeat")));
        UOSCManager::AddInt32(Heartbeat,1);
        Motion->HandleOSCMessage(Heartbeat,TEXT("127.0.0.1"),8000);
        D->Action->SetExternalActionState(true);
        ++GFrameCounter; Timers.Tick(.3f);
        TestFalse(TEXT("Posture timeout releases crouch even while heartbeat continues"),Motion->IsCrouching());
        TestFalse(TEXT("Timeout releases the character's held crouch during Prepare"),Movement->bWantsToCrouch);
        Movement->UnCrouch(false);
        TestFalse(TEXT("Timed-out character can physically stand up"),Player->bIsCrouched);
    }
    return !HasAnyErrors();
}

#if WITH_EDITOR
#include "Editor.h"
#include "LevelEditor.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Containers/Ticker.h"
#include "UObject/StrongObjectPtr.h"

// Verify the saved map's actual keyboard setting, OSC socket, camera and Cover progression.
class FOBTrainingMediaPipePoseSmoke
{
public:
    static void Finish(bool bSuccess)
    {
        UE_LOG(LogTemp,Display,TEXT("POSE_MAP outcome=%s"),bSuccess ? TEXT("PASS") : TEXT("FAIL"));
        if (GEditor && GEditor->PlayWorld) GEditor->RequestEndPlayMap();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([bSuccess](float) {
            FPlatformMisc::RequestExitWithStatus(false,bSuccess ? 0 : 1); return false;
        }),1.f);
    }
    struct FState
    {
        int32 Stage=0;
        double Start=0;
        double CameraZ=0;
        TStrongObjectPtr<UOSCClient> Client;
        FTimerHandle Timer;
    };
    static void Run(UWorld* World)
    {
        auto State=MakeShared<FState>(); State->Start=World->GetTimeSeconds();
        TWeakObjectPtr<UWorld> WeakWorld=World;
        World->GetTimerManager().SetTimer(State->Timer,FTimerDelegate::CreateLambda([State,WeakWorld]() {
            auto* W=WeakWorld.Get(); if (!W) return;
            auto End=[&](bool bSuccess) { W->GetTimerManager().ClearTimer(State->Timer); State->Client.Reset(); Finish(bSuccess); };
            if (W->GetTimeSeconds()-State->Start>15) { End(false); return; }
            AOBTrainingDirector* D=nullptr;
            for (TActorIterator<AOBTrainingDirector> It(W);It;++It) { D=*It; break; }
            if (!D || !D->bInitialized || !D->Motion || !D->PoseInput) return;
            auto* P=Cast<AOBPlayerCharacter>(D->Controller->GetPawn());
            auto Send=[&](int32 Value) {
                FOSCMessage Message; UOSCManager::SetOSCMessageAddress(Message,FOSCAddress(TEXT("/motion/crouch")));
                UOSCManager::AddInt32(Message,Value); State->Client->SendOSCMessage(Message);
            };
            switch (State->Stage)
            {
            case 0:
                UE_LOG(LogTemp,Display,TEXT("POSE_MAP keyboard=%d prototype=%d pose_enabled=%d"),D->bEnableKeyboardTestInputs,D->bUseSplinePrototype,D->PoseInput->IsPoseInputEnabled());
                if (!D->PoseInput->IsPoseInputEnabled() || !D->bUseSplinePrototype) { End(false); return; }
                D->StopPrototypeNarration(); W->GetTimerManager().ClearTimer(D->PrototypeIntroTimer); D->bPrototypeIntroPending=false;
                D->Controller->PlayerCameraManager->SetManualCameraFade(0,FLinearColor::Black,false);
                D->Controller->PlayerCameraManager->StopCameraFade();
                D->PrototypeRouteIndex=1; D->Action->SetExternalActionState(false);
                State->CameraZ=P->GetPlayerCamera()->GetComponentLocation().Z;
                State->Client.Reset(UOSCManager::CreateOSCClient(TEXT("127.0.0.1"),8000,TEXT("TrainingPoseLoopback"),W));
                Send(0); State->Stage=1; break;
            case 1:
                if (D->Motion->IsCrouching()) return;
                Send(1); State->Stage=2; break;
            case 2:
                if (!D->Motion->IsCrouching()) return;
                if (P->GetCharacterMovement()->bWantsToCrouch) { End(false); return; }
                D->BeginPrototypeExercise(EOBTrainingStep::Cover); State->Stage=3; break;
            case 3:
                if (D->CurrentStep!=EOBTrainingStep::Reload) return;
                UE_LOG(LogTemp,Display,TEXT("POSE_MAP cover_pass crouched=%d camera_world_z=%.2f standing_world_z=%.2f"),P->bIsCrouched,P->GetPlayerCamera()->GetComponentLocation().Z,State->CameraZ);
                // Walking lowers the capsule center; relative camera Z compensates for that shift.
                if (!P->bIsCrouched || P->GetPlayerCamera()->GetComponentLocation().Z>=State->CameraZ) { End(false); return; }
                Send(0); State->Stage=4; break;
            case 4:
                if (P->bIsCrouched || D->Motion->IsCrouching()) return;
                UE_LOG(LogTemp,Display,TEXT("POSE_MAP stand_restored camera_world_z=%.2f"),P->GetPlayerCamera()->GetComponentLocation().Z);
                if (!FMath::IsNearlyEqual(P->GetPlayerCamera()->GetComponentLocation().Z,State->CameraZ,1.0)) { End(false); return; }
                D->BeginPrototypeExercise(EOBTrainingStep::Cover); Send(1); State->Stage=5; break;
            case 5:
                if (D->CurrentStep==EOBTrainingStep::Reload && P->bIsCrouched) End(true);
                break;
            }
        }),.05f,true);
    }
};
static FAutoConsoleCommand GOBTrainingPosePIE(TEXT("OB.Training.MediaPipePIE"),TEXT("Verify saved TutorialMap OSC posture reception, camera and Cover progression; exits its editor process."),
    FConsoleCommandDelegate::CreateLambda([]() {
        if (!GEditor) return;
        FRequestPlaySessionParams Params; Params.GlobalMapOverride=TEXT("/Game/Maps/TutorialMap");
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        Params.bAllowOnlineSubsystem=false; GEditor->RequestPlaySession(Params);
        const double Start=FPlatformTime::Seconds();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Start](float) {
            if (GEditor && GEditor->PlayWorld) { FOBTrainingMediaPipePoseSmoke::Run(GEditor->PlayWorld); return false; }
            if (FPlatformTime::Seconds()-Start>30) { FPlatformMisc::RequestExitWithStatus(false,1); return false; }
            return true;
        }),.05f);
    }));
#endif
#endif

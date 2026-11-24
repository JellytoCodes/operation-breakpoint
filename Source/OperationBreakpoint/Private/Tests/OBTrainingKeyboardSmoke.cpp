#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Training/OBTrainingDirector.h"
#include "Training/OBTrainingTarget.h"
#include "Training/OBTrainingDoor.h"
#include "Core/OBPlayerController.h"
#include "Character/OBPlayerCharacter.h"
#include "MediaPipe/OBMediaPipeInputComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "LevelEditor.h"
#include "EngineUtils.h"
#include "InputKeyEventArgs.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Containers/Ticker.h"
#include "Modules/ModuleManager.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

// Dedicated unattended PIE process: keyboard bindings + real movement, traces, timers, door and result UI.
// Aim-hold coordinates and firing rays are supplied directly because an offscreen window cannot own OS focus.
class FOBTrainingKeyboardSmoke
{
    struct FState
    {
        FTimerHandle Timer;
        EOBTrainingStep LastStep = EOBTrainingStep::Idle;
        bool bKeySent = false, bReloadSent = false, bReloadVerified = false, bResultCaptured = false;
        int32 RecordedShots = 0;
        TWeakObjectPtr<AOBTrainingTarget> LastFiredTarget;
        bool bFoldedCaptured = false;
        double StartedAt = 0;
    };

    static void Capture(const TCHAR *Name)
    {
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Tests/TrainingKeyboardBC") / Name,
                                             true, false);
    }
    static void Finish(bool bSuccess)
    {
        GEditor->RequestEndPlayMap();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([bSuccess](float) {
            FPlatformMisc::RequestExitWithStatus(false, bSuccess ? 0 : 1);
            return false;
        }), 1.f);
    }
    static void Tap(AOBPlayerController *PC, FKey Key)
    {
        const FInputDeviceId Device = FInputDeviceId::CreateFromInternalId(0);
        PC->InputKey(FInputKeyEventArgs(nullptr, Device, Key, IE_Pressed, FPlatformTime::Cycles64()));
        TWeakObjectPtr<AOBPlayerController> WeakPC(PC);
        PC->GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([WeakPC, Key, Device]() {
            if (auto *PlayerController = WeakPC.Get())
                PlayerController->InputKey(FInputKeyEventArgs(nullptr, Device, Key, IE_Released, FPlatformTime::Cycles64()));
        }));
    }
    static void FireTarget(AOBTrainingDirector *Director, AOBTrainingTarget *Target)
    {
        auto *PC = Director->Controller.Get();
        const FVector Start = PC->PlayerCameraManager->GetCameraLocation();
        const FVector Aim = Target->BoardMesh->Bounds.Origin;
        const FVector Direction = (Aim - Start).GetSafeNormal();
        FHitResult Hit;
        PC->GetWorld()->LineTraceSingleByChannel(Hit, Start, Aim + Direction * 100.f,
            PC->ShotTraceChannel, FCollisionQueryParams(SCENE_QUERY_STAT(OBTrainingSmokeTrace), PC->bTraceComplex, PC->GetPawn()));
        UE_LOG(LogTemp, Display, TEXT("TRAINING_SHOT requested=%s hit=%s component=%s scoring=%d start=%s aim=%s pose=%.2f"),
            *Target->GetName(), *GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()),
            Target->IsScoringHit(Hit), *Start.ToString(), *Aim.ToString(), Target->GetPoseProgress());
        PC->FireAlongRay(Start, Aim + Direction * 100.f);
    }

  public:
    static void Run(UWorld *World)
    {
        auto State = MakeShared<FState>();
        State->StartedAt = World->GetTimeSeconds();
        TWeakObjectPtr<UWorld> WeakWorld(World);
        World->GetTimerManager().SetTimer(State->Timer, FTimerDelegate::CreateLambda([State, WeakWorld]() {
            auto *World = WeakWorld.Get();
            if (!World) return;
            AOBTrainingDirector *Director = nullptr;
            for (TActorIterator<AOBTrainingDirector> It(World); It; ++It) { Director = *It; break; }
            if (!Director || !Director->Controller) return;
            auto *PC = Director->Controller.Get();
            if (!Director->bEnableKeyboardTestInputs || World->GetTimeSeconds() - State->StartedAt > 150)
            {
                World->GetTimerManager().ClearTimer(State->Timer);
                Finish(false);
                return;
            }
            if (Director->Result.bFinished)
            {
                if (!State->bResultCaptured)
                {
                    Capture(TEXT("Result.png"));
                    State->bResultCaptured = true;
                    return;
                }
                const auto &Data = Director->Result;
                const bool bAllPassed = Data.Steps.Num() == 9 && !Data.Steps.ContainsByPredicate([](const auto &Step) {
                    return Step.Outcome != EOBTrainingOutcome::Passed;
                });
                const bool bSuccess = !Data.bAborted && Data.bUsedTestInputs && !Data.bPassed && bAllPassed &&
                    State->bReloadVerified && Director->BreachDoor->IsBreached() &&
                    Data.UniqueTargetsHit >= 6 && Data.RecordedFireSeconds >= 29.95f;
                UE_LOG(LogTemp, Display, TEXT("TRAINING_KEYBOARD_BC outcome=%s targets=%d shots=%d time=%.2f reload=%d door=%d debug=%d"),
                    bSuccess ? TEXT("PASS") : TEXT("FAIL"), Data.UniqueTargetsHit, Data.ShotsFired,
                    Data.RecordedFireSeconds, State->bReloadVerified, Director->BreachDoor->IsBreached(), Data.bUsedTestInputs);
                World->GetTimerManager().ClearTimer(State->Timer);
                Finish(bSuccess);
                return;
            }
            if (Director->CurrentStep != State->LastStep)
            {
                State->LastStep = Director->CurrentStep;
                State->bKeySent = false;
                Director->SetActorTickEnabled(Director->CurrentStep != EOBTrainingStep::AimHold);
                UE_LOG(LogTemp, Display, TEXT("TRAINING_KEYBOARD_BC stage=%s"),
                    *StaticEnum<EOBTrainingStep>()->GetNameStringByValue(static_cast<int64>(Director->CurrentStep)));
            }
            if (!Director->AcceptsInput(Director->CurrentStep)) return;
            switch (Director->CurrentStep)
            {
            case EOBTrainingStep::WeaponCheck:
            case EOBTrainingStep::Reload:
            case EOBTrainingStep::Calibration:
            case EOBTrainingStep::Cover:
            case EOBTrainingStep::Breach:
                if (!State->bKeySent)
                {
                    const FKey Key = Director->CurrentStep == EOBTrainingStep::WeaponCheck ? EKeys::SpaceBar :
                        Director->CurrentStep == EOBTrainingStep::Reload ? EKeys::RightMouseButton :
                        Director->CurrentStep == EOBTrainingStep::Calibration ? EKeys::C :
                        Director->CurrentStep == EOBTrainingStep::Cover ? EKeys::V : EKeys::B;
                    if (Director->CurrentStep == EOBTrainingStep::Breach) Capture(TEXT("Breach.png"));
                    Tap(PC, Key);
                    State->bKeySent = true;
                }
                break;
            case EOBTrainingStep::AimHold:
                Director->UpdateAimHold(Director->AimHoldSeconds,
                    Director->GetAimTargetPosition() * FVector2D(1280, 720), FVector2D(1280, 720));
                break;
            case EOBTrainingStep::Accuracy:
                if (!State->bKeySent)
                {
                    Capture(TEXT("AccuracyTarget.png"));
                    State->bKeySent = true;
                }
                FireTarget(Director, Director->AccuracyTarget);
                break;
            case EOBTrainingStep::TimedTargets:
                if (!State->bFoldedCaptured && State->LastFiredTarget.IsValid() &&
                    State->LastFiredTarget->GetPoseProgress() <= 0 && !Director->GetActiveTimedTarget())
                {
                    Capture(TEXT("BFolded.png"));
                    State->bFoldedCaptured = true;
                }
                if (State->RecordedShots < 7)
                {
                    if (State->RecordedShots == 0)
                    {
                        const auto *Player = Cast<AOBPlayerCharacter>(PC->GetPawn());
                        if (!Player || !Player->bIsCrouched || !Director->PoseInput || Director->PoseInput->IsPoseInputEnabled())
                        {
                            World->GetTimerManager().ClearTimer(State->Timer);
                            Finish(false);
                            return;
                        }

                    }
                    auto* Target = Director->bUsePopUpTargets ? Director->GetActiveTimedTarget() :
                        Director->TimedTargets[FMath::Min(State->RecordedShots, 5)].Get();
                    if (!Target || (Director->bUsePopUpTargets && Target == State->LastFiredTarget)) break;
                    if (State->RecordedShots == 0) Capture(TEXT("BTargets.png"));
                    FireTarget(Director, Target);
                    ++State->RecordedShots;
                    if (State->RecordedShots == 1)
                    {
                        FireTarget(Director, Target);
                        ++State->RecordedShots;
                    }
                    State->LastFiredTarget = Target;
                }
                else if (!State->bReloadSent)
                {
                    Tap(PC, EKeys::RightMouseButton);
                    State->bReloadSent = true;
                }
                else if (PC->GetCurrentAmmo() == 20)
                    State->bReloadVerified = true;
                break;
            default:
                break;
            }
        }), 0.15f, true);
    }
};

static FAutoConsoleCommand GOBTrainingKeyboardPIE(
    TEXT("OB.Training.KeyboardPIE"), TEXT("Unattended full keyboard training-course smoke test; exits its editor process."),
    FConsoleCommandDelegate::CreateLambda([]() {
        if (!GEditor) return;
        FRequestPlaySessionParams Params;
        Params.GlobalMapOverride = TEXT("/Game/Maps/TrainingTestMap");
        Params.DestinationSlateViewport = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        Params.bAllowOnlineSubsystem = false;
        GEditor->RequestPlaySession(Params);
        const double Start = FPlatformTime::Seconds();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Start](float) {
            if (GEditor && GEditor->PlayWorld)
            {
                FOBTrainingKeyboardSmoke::Run(GEditor->PlayWorld);
                return false;
            }
            if (FPlatformTime::Seconds() - Start > 30)
            {
                FPlatformMisc::RequestExitWithStatus(false, 1);
                return false;
            }
            return true;
        }), 0.1f);
    }));
#endif

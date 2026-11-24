#if WITH_DEV_AUTOMATION_TESTS
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Training/OBTrainingDirector.h"
#include "TimerManager.h"

// Development-only startup proof against the saved map and real local player/HUD.
// Run UnrealEditor-Cmd <project> /Game/Maps/TrainingTestMap -game -nullrhi -ExecCmds="OB.Training.Smoke".
static FAutoConsoleCommandWithWorld GOBTrainingSmoke(
    TEXT("OB.Training.Smoke"), TEXT("Validate training map startup after nine seconds, then exit."),
    FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld *World) {
        if (!World || !World->IsGameWorld())
        {
            FPlatformMisc::RequestExitWithStatus(false, 1);
            return;
        }
        FTimerHandle Handle;
        TWeakObjectPtr<UWorld> WeakWorld(World);
        World->GetTimerManager().SetTimer(
            Handle, FTimerDelegate::CreateLambda([WeakWorld]() {
                UWorld *TestWorld = WeakWorld.Get();
                bool bSuccess = false;
                if (TestWorld)
                {
                    for (TActorIterator<AOBTrainingDirector> It(TestWorld); It; ++It)
                    {
                        const auto Data = It->GetTrainingResult();
                        bSuccess = It->CurrentStep == EOBTrainingStep::WeaponCheck && It->GetRemainingSeconds() > 0 &&
                                   Data.SessionId.IsValid() && !Data.bFinished && It->TimedTargets.Num() == 10;
                        UE_LOG(
                            LogTemp, Display,
                            TEXT("TRAINING_RUNTIME_SMOKE step=%s remaining=%.2f targets=%d session=%s outcome=%s"),
                            *StaticEnum<EOBTrainingStep>()->GetNameStringByValue(static_cast<int64>(It->CurrentStep)),
                            It->GetRemainingSeconds(), It->TimedTargets.Num(), *Data.SessionId.ToString(),
                            bSuccess ? TEXT("PASS") : TEXT("FAIL"));
                        break;
                    }
                }
                FPlatformMisc::RequestExitWithStatus(false, bSuccess ? 0 : 1);
            }),
            9.f, false);
    }));
#endif

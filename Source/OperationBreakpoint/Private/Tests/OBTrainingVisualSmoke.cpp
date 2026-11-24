#if WITH_DEV_AUTOMATION_TESTS
#include "Training/OBTrainingDirector.h"
#include "Training/OBTrainingStatusWidget.h"
#include "Widget/OBPlayerOverlayWidget.h"
#include "Core/OBPlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#if WITH_EDITOR
#include "Editor.h"
#include "LevelEditor.h"
#include "Containers/Ticker.h"
#include "Modules/ModuleManager.h"
#endif

// Exercise the saved map with a rendering viewport, including the actual mounted UMG widget.
class FOBTrainingVisualSmoke
{
  public:
    static void Finish(bool bSuccess)
    {
#if WITH_EDITOR
        if (GEditor && GEditor->PlayWorld)
        {
            GEditor->RequestEndPlayMap();
            FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([bSuccess](float) {
                FPlatformMisc::RequestExitWithStatus(false, bSuccess ? 0 : 1);
                return false;
            }), 1.f);
            return;
        }
#endif
        FPlatformMisc::RequestExitWithStatus(false, bSuccess ? 0 : 1);
    }
    static void Run(UWorld *World)
    {
        if (!World || !World->IsGameWorld())
        {
            Finish(false);
            return;
        }
        const double Start = World->GetTimeSeconds();
        auto Handle = MakeShared<FTimerHandle>();
        auto Captures = MakeShared<int32>(0);
        auto bReloadSeen = MakeShared<bool>(false);
        TWeakObjectPtr<UWorld> WeakWorld(World);
        World->GetTimerManager().SetTimer(
            *Handle, FTimerDelegate::CreateLambda([WeakWorld, Handle, Captures, bReloadSeen, Start]() {
                UWorld *TestWorld = WeakWorld.Get();
                if (!TestWorld)
                    return;
                if (*Captures == 4)
                {
                    TestWorld->GetTimerManager().ClearTimer(*Handle);
                    bool bSuccess = false;
                    for (TActorIterator<AOBTrainingDirector> It(TestWorld); It; ++It)
                    {
                        It->UpdateAimHold(It->AimHoldSeconds, It->GetAimTargetPosition() * FVector2D(1280, 720), FVector2D(1280, 720));
                        bSuccess = *bReloadSeen && It->AimIndex == 4 && It->PendingStep == EOBTrainingStep::Calibration;
                        UE_LOG(LogTemp, Display, TEXT("TRAINING_VISUAL four_holds=%d reload_before_aim=%d outcome=%s"),
                               It->AimIndex, *bReloadSeen, bSuccess ? TEXT("PASS") : TEXT("FAIL"));
                        break;
                    }
                    Finish(bSuccess);
                    return;
                }
                for (TActorIterator<AOBTrainingDirector> It(TestWorld); It; ++It)
                {
                    auto *Director = *It;
                    if (Director->AcceptsInput(EOBTrainingStep::WeaponCheck))
                        Director->Controller->OnTriggerPressed.Broadcast();
                    else if (Director->AcceptsInput(EOBTrainingStep::Reload))
                    {
                        *bReloadSeen = true;
                        UE_LOG(LogTemp, Display, TEXT("TRAINING_VISUAL reload_instruction=%s"), *Director->GetStatusText().ToString());
                        Director->Controller->CompleteReload();
                    }
                    else if (Director->AcceptsInput(EOBTrainingStep::AimHold))
                    {
                        auto *Widget = Director->StatusWidget.Get();
                        const FVector2D Size = Widget ? Widget->GetCachedGeometry().GetLocalSize() : FVector2D::ZeroVector;
                        UE_LOG(LogTemp, Display, TEXT("TRAINING_VISUAL mounted=%d root=%s size=%s target=%s ammo=%d"),
                               Director->Overlay && Director->Overlay->IsInViewport(),
                               Widget && Widget->WidgetTree ? *GetNameSafe(Widget->WidgetTree->RootWidget) : TEXT("NONE"),
                               *Size.ToString(), *Director->GetAimTargetPosition().ToString(), Director->Controller->GetCurrentAmmo());
                        if (!Widget || !Director->Overlay || !Director->Overlay->IsInViewport() || Size.X <= 0 || Size.Y <= 0)
                        {
                            Finish(false);
                            return;
                        }
                        // Offscreen windows cannot own OS focus. Exercise the same hold evaluator explicitly;
                        // the flow test separately checks continuous-hold resets and boundary conditions.
                        Director->SetActorTickEnabled(false);
                        if (*Captures > 0)
                            Director->UpdateAimHold(Director->AimHoldSeconds,
                                Director->GetAimTargetPosition() * FVector2D(1280, 720), FVector2D(1280, 720));
                        Director->UpdateAimHold(Director->AimHoldSeconds * 0.5f,
                            Director->GetAimTargetPosition() * FVector2D(1280, 720), FVector2D(1280, 720));
                        FScreenshotRequest::RequestScreenshot(
                            FPaths::ProjectSavedDir() / FString::Printf(TEXT("Tests/TrainingVisual/Aim%d.png"), *Captures + 1), true, false);
                        ++*Captures;
                    }
                    break;
                }
                if (TestWorld->GetTimeSeconds() - Start > 40)
                    Finish(false);
            }), 1.f, true);
    }
};
static FAutoConsoleCommandWithWorld GOBTrainingVisualSmoke(
    TEXT("OB.Training.VisualSmoke"), TEXT("Capture the aim circle in a rendering training-map session, then exit."),
    FConsoleCommandWithWorldDelegate::CreateStatic(&FOBTrainingVisualSmoke::Run));
#if WITH_EDITOR
static FAutoConsoleCommand GOBTrainingVisualPIE(
    TEXT("OB.Training.VisualPIE"), TEXT("Start an in-viewport PIE session and capture the training aim circle."),
    FConsoleCommandDelegate::CreateLambda([]() {
        if (!GEditor)
            return;
        FRequestPlaySessionParams Params;
        Params.GlobalMapOverride = TEXT("/Game/Maps/TrainingTestMap");
        Params.DestinationSlateViewport = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        Params.bAllowOnlineSubsystem = false;
        GEditor->RequestPlaySession(Params);
        const double Start = FPlatformTime::Seconds();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Start](float) {
            if (GEditor && GEditor->PlayWorld)
            {
                FOBTrainingVisualSmoke::Run(GEditor->PlayWorld);
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
#endif

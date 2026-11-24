#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING && !UE_BUILD_TEST
#include "Core/OBQACheatManager.h"
#include "Core/OBPlayerController.h"
#include "Training/OBTrainingDirector.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "MediaPipe/OBMediaPipeInputComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#if WITH_EDITOR
#include "Editor.h"
#include "LevelEditor.h"
#endif

// Exercise the actual console exec chain and map reload in PIE, Standalone or a Development package.
static void RunQASmoke(UWorld* InitialWorld)
{
        struct FState { double Started=FPlatformTime::Seconds(); bool bReset=false,bPass=true; TWeakObjectPtr<UWorld> Original; };
        auto S=MakeShared<FState>(); S->Original=InitialWorld;
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float) {
            auto End=[](bool bPass) {
                UE_LOG(LogTemp,Display,TEXT("QA_PACKAGE_SMOKE outcome=%s"),bPass ? TEXT("PASS") : TEXT("FAIL"));
#if WITH_EDITOR
                if (GEditor && GEditor->PlayWorld)
                {
                    GEditor->RequestEndPlayMap();
                    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([bPass](float) {
                        FPlatformMisc::RequestExitWithStatus(false,bPass ? 0 : 1); return false;
                    }),1.f);
                    return false;
                }
#endif
                FPlatformMisc::RequestExitWithStatus(false,bPass ? 0 : 1); return false;
            };
            if (FPlatformTime::Seconds()-S->Started>60) return End(false);
            UWorld* World=nullptr;
            for (const auto& Context : GEngine->GetWorldContexts())
                if ((Context.WorldType==EWorldType::Game || Context.WorldType==EWorldType::PIE) && Context.World())
                { World=Context.World(); break; }
            if (!World || !World->HasBegunPlay()) return true;
            auto* PC=Cast<AOBPlayerController>(World->GetFirstPlayerController());
            auto* Player=PC ? Cast<AOBPlayerCharacter>(PC->GetPawn()) : nullptr;
            if (!Player || !PC->CheatManager) return true;
            if (!PC->CheatManager->IsA<UOBQACheatManager>()) return End(false);
            AOBTrainingDirector* Director=nullptr;
            for (TActorIterator<AOBTrainingDirector> It(World); It; ++It) { Director=*It; break; }
            if (UOBQACheatManager::IsTrainingMap(World) && (!Director || !Director->IsReadyForQA())) return true;
            if (S->bReset)
            {
                if (World==S->Original.Get()) return true;
                // Keep the reloaded TutorialMap in a QA session as well, so exiting cannot persist a normal training record.
                if (Director) PC->ConsoleCommand(TEXT("QA Goto Accuracy"),true);
                return End(S->bPass);
            }
            if (auto* Pose=Player->FindComponentByClass<UOBMediaPipeInputComponent>()) Pose->SetPoseInputEnabled(false);
            PC->ConsoleCommand(TEXT("QA Help"),true);
            PC->ConsoleCommand(TEXT("QA List"),true);
            PC->ConsoleCommand(TEXT("QA Status"),true);
            if (UOBQACheatManager::IsTrainingMap(World))
            {
                PC->ConsoleCommand(TEXT("QA Goto Accuracy"),true);
                S->bPass &= Director->CurrentStep==EOBTrainingStep::Accuracy && PC->GetCurrentAmmo()==3 && Player->GetPlayerSplineComponent()->GetCurrentPointIndex()==2;
                PC->SetTrainingAmmo(1,3); PC->ConsoleCommand(TEXT("QA Restart"),true);
                S->bPass &= PC->GetCurrentAmmo()==3;
                PC->ConsoleCommand(TEXT("QA Next"),true);
                S->bPass &= Director->CurrentStep==EOBTrainingStep::Countdown;
                PC->ConsoleCommand(TEXT("QA Goto Shooting2"),true);
                S->bPass &= Director->CurrentStep==EOBTrainingStep::TimedTargets && Player->GetPlayerSplineComponent()->GetCurrentPointIndex()==12;
                PC->ConsoleCommand(TEXT("QA Goto Breach"),true);
                S->bPass &= Director->CurrentStep==EOBTrainingStep::Breach;
                PC->ConsoleCommand(TEXT("QA Goto Result"),true);
                S->bPass &= Director->GetTrainingResult().bFinished && !Director->GetTrainingResult().bPassed;
                PC->ConsoleCommand(TEXT("QA Goto WeaponCheck"),true);
                S->bPass &= Director->CurrentStep==EOBTrainingStep::WeaponCheck && !Director->GetTrainingResult().bFinished;
                const auto Session=Director->GetTrainingResult().SessionId;
                PC->ConsoleCommand(TEXT("QA Goto BadName"),true);
                S->bPass &= Director->GetTrainingResult().SessionId==Session;
            }
            else
            {
                const FVector Before=Player->GetActorLocation();
                const int32 Ammo=PC->GetCurrentAmmo();
                for (const auto* Command : {TEXT("QA Goto Accuracy"),TEXT("QA Restart"),TEXT("QA Next")})
                    PC->ConsoleCommand(Command,true);
                S->bPass &= Player->GetActorLocation().Equals(Before) && PC->GetCurrentAmmo()==Ammo;
            }
            UE_LOG(LogTemp,Display,TEXT("QA_PACKAGE_SMOKE commands map=%s world=%s pass=%d"),
                *World->GetMapName(),World->WorldType==EWorldType::PIE ? TEXT("PIE") : TEXT("Game"),S->bPass);
            S->bReset=true;
            PC->ConsoleCommand(TEXT("QA Reset"),true);
            return true;
        }),.1f);
}

static FAutoConsoleCommandWithWorld GOBQAPackageSmoke(TEXT("OB.QA.Smoke"),
    TEXT("Verify QA console commands, map guard and reset in a game world; exits the process."),
    FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World) { RunQASmoke(World); }));

#if WITH_EDITOR
static FAutoConsoleCommand GOBQAPIESmoke(TEXT("OB.QA.PIESmoke"),
    TEXT("Start TutorialMap PIE and verify QA console commands and reset; exits the process."),
    FConsoleCommandDelegate::CreateLambda([]() {
        if (!GEditor) return;
        FRequestPlaySessionParams Params;
        Params.GlobalMapOverride=TEXT("/Game/Maps/TutorialMap");
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        Params.bAllowOnlineSubsystem=false;
        GEditor->RequestPlaySession(Params);
        const double Started=FPlatformTime::Seconds();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Started](float) {
            if (GEditor && GEditor->PlayWorld) { RunQASmoke(GEditor->PlayWorld); return false; }
            if (FPlatformTime::Seconds()-Started>45)
            { FPlatformMisc::RequestExitWithStatus(false,1); return false; }
            return true;
        }),.05f);
    }));
#endif
#endif

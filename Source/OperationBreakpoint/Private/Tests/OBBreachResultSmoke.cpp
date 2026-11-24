#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING && !UE_BUILD_TEST
#include "Training/OBTrainingDirector.h"
#include "Core/OBPlayerController.h"
#include "Widget/OBHUD.h"
#include "Widget/OBPlayerOverlayWidget.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UnrealClient.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

// Opt-in actual-map check: inject only the recognized breach gesture, then let
// the door animation, result UI and camera fade complete through normal gameplay.
static FAutoConsoleCommand GBreachResultSmoke(TEXT("OB.Training.BreachResultSmoke"),
    TEXT("Verify completed breach, visible world at 0.3 fade and result panel; capture and exit."),
    FConsoleCommandDelegate::CreateLambda([] {
        struct FState { double Started=FPlatformTime::Seconds(),FinishedAt=0; int32 Phase=0; };
        auto S=MakeShared<FState>();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float) {
            auto End=[](bool OK) {
                UE_LOG(LogTemp,Display,TEXT("BREACH_RESULT_SMOKE outcome=%s"),OK?TEXT("PASS"):TEXT("FAIL"));
                FPlatformMisc::RequestExitWithStatus(false,OK?0:1); return false;
            };
            const double Now=FPlatformTime::Seconds();
            if (Now-S->Started>60) return End(false);
            UWorld* World=nullptr;
            for (const auto& Context:GEngine->GetWorldContexts())
                if (Context.WorldType==EWorldType::Game && Context.World()) { World=Context.World(); break; }
            if (!World || !World->HasBegunPlay()) return true;
            auto* PC=Cast<AOBPlayerController>(World->GetFirstPlayerController());
            if (!PC || !PC->GetOBHUD()) return true;
            AOBTrainingDirector* Director=nullptr;
            for (TActorIterator<AOBTrainingDirector> It(World);It;++It) { Director=*It; break; }
            if (!Director || !Director->IsReadyForQA()) return true;
            if (!S->Phase)
            {
                PC->ConsoleCommand(TEXT("QA Goto Breach"),true);
                Director->NotifyBreachState(false); Director->NotifyBreachState(true);
                if (!Director->HasAcceptedBreach() || Director->GetTrainingResult().bFinished) return End(false);
                S->Phase=1; return true;
            }
            if (!Director->GetTrainingResult().bFinished) return true;
            if (!S->FinishedAt) S->FinishedAt=Now;
            if (Now-S->FinishedAt<1) return true;
            auto* Overlay=PC->GetOBHUD()->GetPlayerOverlayWidget();
            if (!Director->GetTrainingResult().IsScenarioPassed() || !Overlay || !Overlay->IsTrainingResultVisible()
                || !PC->PlayerCameraManager || !FMath::IsNearlyEqual(PC->PlayerCameraManager->FadeAmount,.3f,.001f)) return End(false);
            const FString Folder=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Tests/BreachResultPackage"));
            const FString Shot=Folder/TEXT("Completed.png");
            if (S->Phase==1)
            {
                IFileManager::Get().MakeDirectory(*Folder,true);
                FScreenshotRequest::RequestScreenshot(Shot,true,false); S->Phase=2; return true;
            }
            if (IFileManager::Get().FileSize(*Shot)<4096) return true;
            UE_LOG(LogTemp,Display,TEXT("BREACH_RESULT_SMOKE world_fade=%.3f result_visible=1 screenshot=%s"),PC->PlayerCameraManager->FadeAmount,*Shot);
            return End(true);
        }),.05f);
    }));
#endif

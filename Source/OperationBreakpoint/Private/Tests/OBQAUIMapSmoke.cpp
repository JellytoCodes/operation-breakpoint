#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Core/OBQACheatManager.h"
#include "Core/OBPlayerController.h"
#include "Training/OBTrainingDirector.h"
#include "Training/OBTrainingResultWidget.h"
#include "Widget/OBHUD.h"
#include "Widget/OBPlayerOverlayWidget.h"
#include "Widget/OBHudPanelWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/AudioComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Editor.h"
#include "LevelEditor.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"

class FOBQAUIMapSmoke
{
public:
    static bool Check(AOBPlayerController* PC,AOBTrainingDirector* Director)
    {
        auto* Cheats=Cast<UOBQACheatManager>(PC->CheatManager);
        auto* HUD=PC->GetOBHUD(); auto* Original=HUD ? HUD->GetPlayerOverlayWidget() : nullptr;
        if (!Cheats || !Original) return false;
        const auto Visibility=Original->GetVisibility();
        const FString Status=Director->GetQAStatus();
        const int32 Ammo=PC->GetCurrentAmmo();
        const FVector Location=PC->GetPawn()->GetActorLocation();
        bool bOK=true; FString Reply;
        TArray<TWeakObjectPtr<UAudioComponent>> Audio;
        for (int32 Id=3001;Id<=3010;++Id)
        {
            // Exercise the real Exec path, including the console's FString argument parsing.
            PC->ConsoleCommand(FString::Printf(TEXT("QA UI %d"),Id),true);
            auto* Preview=Cheats->UIPreview.Get();
            bOK &= Preview && Preview->IsInViewport() && UGameplayStatics::IsGamePaused(PC)
                && Original->GetVisibility()==ESlateVisibility::Hidden && Director->GetQAStatus()==Status;
            if (!Preview) break;
            if (Id==3001) Audio=Cheats->PausedAudio;
            if (Id<=3007)
            {
                auto* Guide=Cast<UOBGuideWidget>(Preview->WidgetTree->FindWidget(TEXT("Guide")));
                auto* Trigger=Guide ? Cast<UImage>(Guide->WidgetTree->FindWidget(TEXT("TriggerImage"))) : nullptr;
                bOK &= Preview->VisibleElements==1 && Guide && Guide->CurrentUIId==Id && Trigger && Trigger->IsVisible();
            }
            else bOK &= Preview->VisibleElements==128 && Preview->GetTrainingResultWidget()->CurrentUIId==Id
                && !Preview->GetTrainingResultWidget()->bAutomaticNavigation && !Preview->IsTrainingResultVisible();
            UE_LOG(LogTemp,Display,TEXT("QA_UI_MAP page=%d visible=%d paused=%d"),Id,Preview->VisibleElements,UGameplayStatics::IsGamePaused(PC));
        }
        auto* Last=Cheats->UIPreview.Get();
        bOK &= !Cheats->ExecuteCommand(TEXT("UI 3011"),Reply) && Cheats->UIPreview==Last && UGameplayStatics::IsGamePaused(PC);
        PC->ConsoleCommand(TEXT("QA UI Close"),true);
        bOK &= !Cheats->IsUIPreviewActive() && !UGameplayStatics::IsGamePaused(PC) && Original->GetVisibility()==Visibility;
        for (auto Sound : Audio) bOK &= !Sound.IsValid() || !Sound->bIsPaused;
        bOK &= Director->GetQAStatus()==Status && PC->GetCurrentAmmo()==Ammo && PC->GetPawn()->GetActorLocation().Equals(Location);
        // Closing never removes an engine pause that existed before this preview.
        bOK &= PC->SetPause(true);
        bOK &= Cheats->ExecuteCommand(TEXT("UI 3008"),Reply);
        bOK &= Cheats->ExecuteCommand(TEXT("UI Close"),Reply) && UGameplayStatics::IsGamePaused(PC);
        PC->SetPause(false);
        bOK &= Cheats->ExecuteCommand(TEXT("UI Close"),Reply);
        return bOK;
    }
    static void Finish(bool bOK)
    {
        UE_LOG(LogTemp,Display,TEXT("QA_UI_MAP outcome=%s"),bOK ? TEXT("PASS") : TEXT("FAIL"));
        if (GEditor && GEditor->PlayWorld) GEditor->RequestEndPlayMap();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([bOK](float) {
            FPlatformMisc::RequestExitWithStatus(false,bOK ? 0 : 1); return false;
        }),1.f);
    }
    static void Start()
    {
        if (!GEditor) return;
        FRequestPlaySessionParams Params; Params.GlobalMapOverride=TEXT("/Game/Maps/TutorialMap"); Params.bAllowOnlineSubsystem=false;
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        GEditor->RequestPlaySession(Params);
        const double Started=FPlatformTime::Seconds();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Started](float) {
            auto* World=GEditor ? GEditor->PlayWorld.Get() : nullptr;
            if (World)
                for (TActorIterator<AOBTrainingDirector> It(World);It;++It)
                    if (It->IsReadyForQA())
                        if (auto* PC=Cast<AOBPlayerController>(World->GetFirstPlayerController()); PC && PC->GetOBHUD())
                        { Finish(Check(PC,*It)); return false; }
            if (FPlatformTime::Seconds()-Started>60) { Finish(false); return false; }
            return true;
        }),.1f);
    }
};
static FAutoConsoleCommand GQAUIMapSmoke(TEXT("OB.QA.UIMapSmoke"),TEXT("Verify all QA UI pages and state restoration in TutorialMap PIE, then exit."),
    FConsoleCommandDelegate::CreateStatic(&FOBQAUIMapSmoke::Start));
#endif

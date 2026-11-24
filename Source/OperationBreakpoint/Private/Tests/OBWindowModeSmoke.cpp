#if WITH_DEV_AUTOMATION_TESTS
#include "Containers/Ticker.h"
#include "Core/OBGameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Kismet/GameplayStatics.h"
#include "UnrealClient.h"
#include "Widgets/SWindow.h"

// Opt-in standalone check. PIE intentionally does not apply the game's startup display policy.
class FOBWindowModeSmoke
{
public:
    static void Start()
    {
        struct FState { double Started=FPlatformTime::Seconds();int32 Step=0;TWeakObjectPtr<UWorld> InitialWorld;FString Report; };
        auto S=MakeShared<FState>();FString Out=FPaths::ProjectSavedDir()/TEXT("Tests/WindowMode");
        FParse::Value(FCommandLine::Get(),TEXT("OBWindowOutput="),Out);
        Out=FPaths::ConvertRelativePathToFull(Out);IFileManager::Get().MakeDirectory(*Out,true);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S,Out](float) {
            auto Finish=[&](bool OK,const FString& Why) {
                S->Report+=FString::Printf(TEXT("outcome=%s reason=%s\n"),OK ? TEXT("PASS") : TEXT("FAIL"),*Why);
                FFileHelper::SaveStringToFile(S->Report,*(Out/TEXT("verification.txt")));
                UE_LOG(LogTemp,Display,TEXT("WINDOW_MODE_SMOKE\n%s"),*S->Report);
                FPlatformMisc::RequestExitWithStatus(false,OK ? 0 : 1);return false;
            };
            if (GIsEditor) return Finish(false,TEXT("Run in standalone -game rather than PIE"));
            if (FPlatformTime::Seconds()-S->Started>90) return Finish(false,TEXT("Standalone window deadline"));
            UWorld* W=nullptr;
            for (const auto& C:GEngine->GetWorldContexts()) if (C.WorldType==EWorldType::Game && C.World()) { W=C.World();break; }
            if (!W || !W->HasBegunPlay() || W->GetTimeSeconds()<.5f || !W->GetGameInstance<UOBGameInstance>()) return true;
            if (S->Step && W==S->InitialWorld.Get()) return true;
            auto* View=W->GetGameViewport();auto Window=View ? View->GetWindow() : nullptr;
            auto* Settings=GEngine->GetGameUserSettings();
            if (!Window || !Settings || !View->Viewport) return true;
            const FIntPoint Size=View->Viewport->GetSizeXY(),Resolution=Settings->GetScreenResolution();
            if (Size.X<=0 || Size.Y<=0) return true;
            S->Report+=FString::Printf(TEXT("map=%s settings_mode=%d window_mode=%d viewport=%dx%d selected_resolution=%dx%d quality=%d\n"),
                *UGameplayStatics::GetCurrentLevelName(W,true),int32(Settings->GetFullscreenMode()),int32(Window->GetWindowMode()),Size.X,Size.Y,Resolution.X,Resolution.Y,Settings->GetOverallScalabilityLevel());
            if (Settings->GetFullscreenMode()!=EWindowMode::WindowedFullscreen || Window->GetWindowMode()!=EWindowMode::WindowedFullscreen)
                return Finish(false,TEXT("Native settings and actual Slate window must both be borderless (1)"));
            if (Size!=Resolution) return Finish(false,TEXT("Viewport must match selected monitor resolution"));
            if (Settings->GetOverallScalabilityLevel()!=1) return Finish(false,TEXT("Existing Medium preset changed"));
            if (!S->Step)
            {
                S->InitialWorld=W;S->Step=1;
                UGameplayStatics::OpenLevel(W,TEXT("/Game/Maps/TutorialMap"));return true;
            }
            return Finish(true,TEXT("Borderless native window and monitor-sized viewport persist from TitleMap to TutorialMap"));
        }),.1f);
    }
};
static FAutoConsoleCommand GOBWindowModeSmoke(TEXT("OB.UI.WindowModeSmoke"),TEXT("Check actual standalone borderless mode, viewport size and map travel, then exit."),FConsoleCommandDelegate::CreateStatic(&FOBWindowModeSmoke::Start));
#endif

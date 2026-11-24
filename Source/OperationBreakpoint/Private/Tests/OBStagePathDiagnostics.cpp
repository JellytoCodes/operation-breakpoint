#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Editor.h"
#include "LevelEditor.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Components/CapsuleComponent.h"
#include "Character/OBEnemyCharacter.h"
#include "World/OBEncounterSpawner.h"

static void DiagnoseStagePaths(UWorld* World)
{
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    auto* EnemyClass=LoadClass<AOBEnemyCharacter>(nullptr,TEXT("/Game/Blueprints/Enemy/BP_EnemyCharacter.BP_EnemyCharacter_C"));
    FString Report=TEXT("actor,default_start,default_goal,local_start,local_goal,default_path,local_path,default_goal_z,local_goal_z,local_goal_offset_cm\n");
    int32 Checked=0,Complete=0;
    if (Nav && EnemyClass)
    {
        const float HalfHeight=EnemyClass->GetDefaultObject<AOBEnemyCharacter>()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        for (TActorIterator<AOBEncounterSpawner> It(World); It; ++It)
        {
            auto* S=*It;
            if (!S->bMoveToStagingPoint || !S->GetFolderPath().ToString().EndsWith(TEXT("/Move"))) continue;
            FNavLocation A,B,WideA,WideB;
            const FVector Start=S->GetActorLocation()-FVector(0,0,HalfHeight),Goal=S->GetStagingTransform().GetLocation();
            const bool bA=Nav->ProjectPointToNavigation(Start,A,FVector(50,50,250));
            const bool bB=Nav->ProjectPointToNavigation(Goal,B,FVector(50,50,250));
            const bool bWA=Nav->ProjectPointToNavigation(Start,WideA,FVector(100,100,120));
            const bool bWB=Nav->ProjectPointToNavigation(Goal,WideB,FVector(100,100,120));
            auto* Path=bWA && bWB ? UNavigationSystemV1::FindPathToLocationSynchronously(World,WideA.Location,WideB.Location) : nullptr;
            const bool bPath=Path && Path->IsValid() && !Path->IsPartial();
            auto* DefaultPath=bA && bB ? UNavigationSystemV1::FindPathToLocationSynchronously(World,A.Location,B.Location) : nullptr;
            const bool bDefaultPath=DefaultPath && DefaultPath->IsValid() && !DefaultPath->IsPartial();
            Report+=FString::Printf(TEXT("%s,%d,%d,%d,%d,%d,%d,%.2f,%.2f,%.2f\n"),*S->GetActorLabel(),bA,bB,bWA,bWB,bDefaultPath,bPath,
                bB ? B.Location.Z : -9999,bWB ? WideB.Location.Z : -9999,bWB ? FVector::Distance(Goal,WideB.Location) : -1);
            ++Checked; if (bPath) ++Complete;
        }
    }
    FFileHelper::SaveStringToFile(Report,*(FPaths::ProjectDir()/TEXT("Build/Testing/Verification/StageEncounters/path-diagnostics.csv")));
    UE_LOG(LogTemp,Display,TEXT("STAGE_PATH_DIAGNOSTICS checked=%d complete=%d"),Checked,Complete);
    GEditor->RequestEndPlayMap();
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float) {
        FPlatformMisc::RequestExitWithStatus(false,0); return false;
    }),1.f);
}
static FAutoConsoleCommand GOBStagePathDiagnostics(TEXT("OB.Stage.PathDiagnosticsPIE"),
    TEXT("Read authored mover navigation in disposable PIE, export CSV and exit."),
    FConsoleCommandDelegate::CreateLambda([]() {
        if (!GEditor) return;
        FRequestPlaySessionParams Params; Params.GlobalMapOverride=TEXT("/Game/Maps/Stage_01");
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        Params.bAllowOnlineSubsystem=false; GEditor->RequestPlaySession(Params);
        const double Started=FPlatformTime::Seconds();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Started](float) {
            if (GEditor && GEditor->PlayWorld && GEditor->PlayWorld->GetTimeSeconds()>1)
            { DiagnoseStagePaths(GEditor->PlayWorld); return false; }
            if (FPlatformTime::Seconds()-Started>60) { FPlatformMisc::RequestExitWithStatus(false,1); return false; }
            return true;
        }),.05f);
    }));
#endif

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Editor.h"
#include "LevelEditor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "Character/OBAllyCharacter.h"
#include "Character/OBAllySplineComponent.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "World/OBAllyRoute.h"
#include "GameFramework/PlayerController.h"

namespace
{
void RunAllyRedeploySmoke(UWorld* World)
{
    struct FState
    {
        double Started=0, ShotAt=-1;
        TArray<TWeakObjectPtr<AOBAllyCharacter>> Allies;
        bool bMoved=false;
        FTimerHandle Timer;
    };
    auto S=MakeShared<FState>(); S->Started=World->GetTimeSeconds();
    const FString Shot=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Tests/AllyRedeploy/Regroup.png"));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Shot),true);
    IFileManager::Get().Delete(*Shot);
    for (TActorIterator<AOBAllyCharacter> It(World); It; ++It) S->Allies.Add(*It);
    TWeakObjectPtr<UWorld> WeakWorld=World;
    World->GetTimerManager().SetTimer(S->Timer,FTimerDelegate::CreateLambda([S,WeakWorld,Shot]() {
        auto* W=WeakWorld.Get(); if (!W) return;
        auto Finish=[&](bool bPass,const TCHAR* Reason) {
            W->GetTimerManager().ClearTimer(S->Timer);
            UE_LOG(LogTemp,Display,TEXT("ALLY_REDEPLOY_PIE outcome=%s reason=%s"),bPass ? TEXT("PASS") : TEXT("FAIL"),Reason);
            GEditor->RequestEndPlayMap();
            FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([bPass](float) {
                FPlatformMisc::RequestExitWithStatus(false,bPass ? 0 : 1); return false;
            }),1.f);
        };
        if (W->GetTimeSeconds()-S->Started>30) { Finish(false,TEXT("regroup timeout")); return; }
        int32 Count=0; for (TActorIterator<AOBAllyCharacter> It(W); It; ++It) ++Count;
        if (Count!=2 || S->Allies.Num()!=2) { Finish(false,TEXT("squad was duplicated or destroyed")); return; }
        bool bBothRegrouped=true,bBothMoving=true,bBothFinished=true;
        for (auto Ref : S->Allies)
        {
            auto* Ally=Ref.Get(); if (!Ally) { Finish(false,TEXT("original ally destroyed")); return; }
            auto* C=Ally->GetRouteComponent();
            const bool bRegroup=C->GetCurrentRoute() && C->GetCurrentRoute()->ActorHasTag(TEXT("OB.AllyRedeploy.Test"));
            bBothRegrouped &= bRegroup;
            bBothMoving &= bRegroup && C->GetDistanceAlongRoute()>100 && Ally->GetVelocity().Size()>1;
            bBothFinished &= bRegroup && C->HasCompletedRoute();
            if (bRegroup && C->IsBlocked()) { Finish(false,TEXT("regroup route intersects an obstacle")); return; }
        }
        if (bBothRegrouped && bBothMoving && !S->bMoved)
        {
            S->bMoved=true; S->ShotAt=W->GetTimeSeconds();
            FScreenshotRequest::RequestScreenshot(Shot,false,false,false,FIntRect(),true);
        }
        if (bBothFinished && S->bMoved && W->GetTimeSeconds()-S->ShotAt>1.0)
            Finish(IFileManager::Get().FileSize(*Shot)>0,TEXT("same two allies teleported at point 5, moved and completed new routes"));
    }),.05f,true);
}
FAutoConsoleCommand GOBAllyRedeployPIE(TEXT("OB.Ally.RedeployPIE"),
    TEXT("Verify the saved Stage_01 point-5 squad regroup in disposable PIE; exits this editor process."),
    FConsoleCommandDelegate::CreateLambda([]() {
        if (!GEditor) return;
        FRequestPlaySessionParams Params; Params.GlobalMapOverride=TEXT("/Game/Maps/Stage_01");
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        Params.bAllowOnlineSubsystem=false; GEditor->RequestPlaySession(Params);
        const double Started=FPlatformTime::Seconds();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Started](float) {
            if (GEditor && GEditor->PlayWorld) { RunAllyRedeploySmoke(GEditor->PlayWorld); return false; }
            if (FPlatformTime::Seconds()-Started>60) { FPlatformMisc::RequestExitWithStatus(false,1); return false; }
            return true;
        }),.05f);
    }));
}
#endif

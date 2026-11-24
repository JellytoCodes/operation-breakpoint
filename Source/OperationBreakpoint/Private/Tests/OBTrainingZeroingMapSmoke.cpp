#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Training/OBTrainingDirector.h"
#include "Training/OBTrainingTarget.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBActionComponent.h"
#include "Core/OBPlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "LevelEditor.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Containers/Ticker.h"
#include "Modules/ModuleManager.h"
#include "TimerManager.h"

// Uses the saved TutorialMap, its real player camera and controller's ray/stance/ammo gates.
// Only aim-hold coordinates are injected; no actors or assets are moved or saved.
class FOBTrainingZeroingMapSmoke
{
    struct FState { FTimerHandle Timer; double StartedAt=0; bool bCrouchedPassed=false, bStandingRequested=false; };
    static void Finish(bool bSuccess)
    {
        GEditor->RequestEndPlayMap();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([bSuccess](float) {
            FPlatformMisc::RequestExitWithStatus(false,bSuccess ? 0 : 1); return false;
        }),1.f);
    }
    static void Fire(AOBTrainingDirector* Director)
    {
        auto* PC=Director->Controller.Get();
        auto* Target=Director->AccuracyTarget.Get();
        auto* Player=Cast<AOBPlayerCharacter>(PC->GetPawn());
        const FVector Start=PC->PlayerCameraManager->GetCameraLocation();
        const FVector Aim=Target->BoardMesh->Bounds.Origin;
        const FVector End=Aim+(Aim-Start).GetSafeNormal()*100.f;
        FHitResult Hit;
        PC->GetWorld()->LineTraceSingleByChannel(Hit,Start,End,PC->ShotTraceChannel,
            FCollisionQueryParams(SCENE_QUERY_STAT(OBZeroingMapTrace),PC->bTraceComplex,PC->GetPawn()));
        const int32 Ammo=PC->GetCurrentAmmo(), Shots=Director->Shots;
        const bool bScoring=Target->IsScoringHit(Hit), bRaised=Target->IsTargetRaised();
        PC->FireAlongRay(Start,End);
        UE_LOG(LogTemp,Display,TEXT("ZEROING_MAP_RAY actor=%s component=%s scoring=%d raised=%d crouched=%d mode=%d actionAllowed=%d canShoot=%d ammo=%d->%d shots=%d->%d start=%s aim=%s"),
            *GetNameSafe(Hit.GetActor()),*GetNameSafe(Hit.GetComponent()),bScoring,bRaised,
            Player->bIsCrouched,PC->bIsCrouchMode,PC->IsActionInputAllowed(),PC->bTrainingCanShoot,
            Ammo,PC->GetCurrentAmmo(),Shots,Director->Shots,*Start.ToString(),*Aim.ToString());
    }
public:
    static void Run(UWorld* World)
    {
        auto State=MakeShared<FState>(); State->StartedAt=World->GetTimeSeconds();
        TWeakObjectPtr<UWorld> WeakWorld(World);
        World->GetTimerManager().SetTimer(State->Timer,FTimerDelegate::CreateLambda([State,WeakWorld]() {
            auto* World=WeakWorld.Get(); if (!World) return;
            AOBTrainingDirector* Director=nullptr;
            for (TActorIterator<AOBTrainingDirector> It(World);It;++It) { Director=*It; break; }
            if (!Director || !Director->Controller) return;
            auto* PC=Director->Controller.Get(); auto* Player=Cast<AOBPlayerCharacter>(PC->GetPawn());
            if (!Player || Director->Result.bFinished || World->GetTimeSeconds()-State->StartedAt>45)
            { World->GetTimerManager().ClearTimer(State->Timer); Finish(false); return; }
            Director->PrototypeNarrationSeconds=0.05f;
            switch (Director->CurrentStep)
            {
            case EOBTrainingStep::Welcome:
                if (Director->bGuideAdvanceReady) Director->HandleTriggerPressed();
                break;
            case EOBTrainingStep::WeaponCheck: PC->FireAtCursor(); break;
            case EOBTrainingStep::AimHold:
                Director->UpdateAimHold(Director->AimHoldSeconds+0.01f,Director->GetAimTargetPosition()*FVector2D(1280,720),FVector2D(1280,720));
                break;
            case EOBTrainingStep::Cover: Player->Crouch(); break;
            case EOBTrainingStep::Reload: PC->CompleteReload(); break;
            case EOBTrainingStep::Accuracy:
                if (!State->bCrouchedPassed)
                {
                    for (int32 I=0;I<3 && Director->CurrentStep==EOBTrainingStep::Accuracy;++I) Fire(Director);
                    State->bCrouchedPassed=Director->CurrentDialogueId==14;
                    UE_LOG(LogTemp,Display,TEXT("ZEROING_MAP crouched_pass=%d"),State->bCrouchedPassed);
                    if (!State->bCrouchedPassed) { World->GetTimerManager().ClearTimer(State->Timer); Finish(false); return; }
                    Director->BeginPrototypeExercise(EOBTrainingStep::Accuracy);
                    Player->UnCrouch(); State->bStandingRequested=true;
                    return;
                }
                if (State->bStandingRequested && !Player->bIsCrouched)
                {
                    for (int32 I=0;I<3 && Director->CurrentStep==EOBTrainingStep::Accuracy;++I) Fire(Director);
                    const bool bPassed=Director->CurrentDialogueId==14 && Director->Hits==3;
                    UE_LOG(LogTemp,Display,TEXT("ZEROING_MAP standing_pass=%d"),bPassed);
                    World->GetTimerManager().ClearTimer(State->Timer); Finish(bPassed);
                }
                break;
            default: break;
            }
        }),0.15f,true);
    }
};
static FAutoConsoleCommand GOBZeroingMapPIE(TEXT("OB.Training.ZeroingPIE"),TEXT("Reproduce camera-to-board zeroing in TutorialMap in an unattended PIE process; exits that process."),
    FConsoleCommandDelegate::CreateLambda([]() {
        if (!GEditor) return;
        FRequestPlaySessionParams Params; Params.GlobalMapOverride=TEXT("/Game/Maps/TutorialMap");
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        Params.bAllowOnlineSubsystem=false; GEditor->RequestPlaySession(Params);
        const double Start=FPlatformTime::Seconds();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Start](float) {
            if (GEditor && GEditor->PlayWorld) { FOBTrainingZeroingMapSmoke::Run(GEditor->PlayWorld); return false; }
            if (FPlatformTime::Seconds()-Start>30) { FPlatformMisc::RequestExitWithStatus(false,1); return false; }
            return true;
        }),0.1f);
    }));
#endif

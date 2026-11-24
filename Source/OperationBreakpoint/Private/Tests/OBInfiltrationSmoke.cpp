#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "AI/OBAIController.h"
#include "Character/OBAllyCharacter.h"
#include "Character/OBAllySplineComponent.h"
#include "Character/OBModularSoldierComponent.h"
#include "Character/OBEnemyCharacter.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Character/OBWeaponGripComponent.h"
#include "Animation/OBAllyAnimInstance.h"
#include "World/OBAllyRoute.h"
#include "World/OBRailPath.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SplineComponent.h"
#include "Components/StateTreeAIComponent.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "LevelEditor.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

// Starts a disposable PIE session on the saved map. Does not save world changes and exits its process.
static void RunInfiltrationSmoke(UWorld* World)
{
    struct FState
    {
        double Started=0, Mark=0; int32 Stage=0;
        bool bPass=true, bAllyMoving=false, bBranchSeen=false, bRunnerMoving=false, bCoverMoving=false, bCrouched=false;
        FVector NavA,NavB,GuardStart; float StandingHead=0;
        FTimerHandle Timer;
        TArray<TWeakObjectPtr<AOBAllyCharacter>> Allies;
        TWeakObjectPtr<AOBEnemyCharacter> Enemy;
    };
    auto S=MakeShared<FState>(); S->Started=World->GetTimeSeconds();
    const FString Folder=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Tests/Infiltration"));
    IFileManager::Get().MakeDirectory(*Folder,true);
    TWeakObjectPtr<UWorld> WeakWorld=World;
    World->GetTimerManager().SetTimer(S->Timer,FTimerDelegate::CreateLambda([S,WeakWorld,Folder]() {
        auto* W=WeakWorld.Get(); if (!W) return;
        auto End=[&](bool bPass,const TCHAR* Reason) {
            W->GetTimerManager().ClearTimer(S->Timer);
            UE_LOG(LogTemp,Display,TEXT("INFILTRATION_PIE outcome=%s reason=%s ally_moving=%d branch=%d runner_moving=%d cover_moving=%d crouched=%d"),
                bPass ? TEXT("PASS") : TEXT("FAIL"),Reason,S->bAllyMoving,S->bBranchSeen,S->bRunnerMoving,S->bCoverMoving,S->bCrouched);
            GEditor->RequestEndPlayMap();
            FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([bPass](float) { FPlatformMisc::RequestExitWithStatus(false,bPass ? 0 : 1); return false; }),1.f);
        };
        const double Now=W->GetTimeSeconds();
        if (Now-S->Started>40) { End(false,TEXT("timeout")); return; }
        auto* PC=W->GetFirstPlayerController(); auto* Player=PC ? Cast<AOBPlayerCharacter>(PC->GetPawn()) : nullptr;
        if (!Player) return;
        if (S->Allies.IsEmpty())
        {
            for (TActorIterator<AOBAllyCharacter> It(W); It; ++It) S->Allies.Add(*It);
            if (S->Allies.Num()!=2) { End(false,TEXT("saved squad count")); return; }
        }
        bool bSquadFinished=true;
        for (auto& Ref : S->Allies)
        {
            auto* Ally=Ref.Get(); if (!Ally) { End(false,TEXT("ally destroyed")); return; }
            auto* Route=Ally->GetRouteComponent();
            S->bAllyMoving |= Ally->GetVelocity().Size()>3.f;
            auto* Anim=Cast<UOBAllyAnimInstance>(Ally->GetMesh()->GetAnimInstance());
            auto* Appearance=Ally->GetSoldierAppearanceComponent();
            S->bPass &= Anim && Appearance->GetVisualParts().Num()==Appearance->OutfitMeshes.Num()
                && Appearance->OutfitMeshes.ContainsByPredicate([](const auto& Mesh) { return Mesh && Mesh->GetName()==TEXT("SKM_Binoculars2"); });
            if (Anim && Ally->GetVelocity().Size()>3.f && Now-S->Started>.2f) S->bPass &= Anim->bIsMoving;
            if (auto* Path=Route->GetCurrentRoute()) S->bBranchSeen |= Path->ActorHasTag(TEXT("OB.Infiltration.Branch"));
            bSquadFinished &= Route->HasCompletedRoute();
        }
        if (S->Stage==0 && Now-S->Started>.8f)
        {
            FScreenshotRequest::RequestScreenshot(Folder/TEXT("StageSquadApproach.png"),false,false,false,FIntRect(),true);
            S->Stage=1;
        }
        if (S->Stage==1 && Player->GetPlayerSplineComponent()->HasFinishedRoutePreview() && bSquadFinished)
        {
            S->bPass &= S->bAllyMoving && S->bBranchSeen;
            FScreenshotRequest::RequestScreenshot(Folder/TEXT("StageSquadSplit.png"),false,false,false,FIntRect(),true);
            auto* Rail=Player->GetPlayerSplineComponent()->GetRailPath();
            auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(W); FNavLocation A,B;
            if (!Rail || !Nav || !Nav->ProjectPointToNavigation(Rail->GetSpline()->GetLocationAtDistanceAlongSpline(200,ESplineCoordinateSpace::World),A,FVector(100,100,90))
                || !Nav->ProjectPointToNavigation(Rail->GetSpline()->GetLocationAtDistanceAlongSpline(450,ESplineCoordinateSpace::World),B,FVector(100,100,90)))
            { End(false,TEXT("saved navigation unavailable")); return; }
            auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(W,A.Location,B.Location);
            if (!Path || !Path->IsValid() || Path->IsPartial() || Path->GetPathLength()<100) { End(false,TEXT("navigation path")); return; }
            S->NavA=A.Location; S->NavB=B.Location;
            auto* Class=LoadClass<AOBEnemyCharacter>(nullptr,TEXT("/Game/Blueprints/Enemy/BP_EnemyCharacter.BP_EnemyCharacter_C"));
            if (!Class) { End(false,TEXT("enemy blueprint")); return; }
            FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            FVector Position=S->NavA+FVector(0,0,Class->GetDefaultObject<AOBEnemyCharacter>()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3);
            auto* Enemy=W->SpawnActor<AOBEnemyCharacter>(Class,Position,FRotator::ZeroRotator,Params);
            auto* AI=Enemy ? Cast<AOBAIController>(Enemy->GetController()) : nullptr;
            if (!AI) { End(false,TEXT("runner controller")); return; }
            Enemy->EncounterBehavior.EntryMode=EOBEnemyEntryMode::RunToStaging;
            Enemy->EncounterBehavior.RunSpeed=300; Enemy->EncounterBehavior.bStartStateTreeAfterReaction=false;
            if (!AI->PrepareEncounterEntry(FTransform(S->NavB),FTransform(S->NavB))) { End(false,TEXT("runner move request")); return; }
            S->Enemy=Enemy; S->Mark=Now; S->Stage=2;
            UE_LOG(LogTemp,Display,TEXT("INFILTRATION_PIE squad completed; nav path %.1f cm"),Path->GetPathLength());
        }
        else if (S->Stage==2)
        {
            auto* Enemy=S->Enemy.Get(); auto* AI=Enemy ? Cast<AOBAIController>(Enemy->GetController()) : nullptr;
            if (!AI) { End(false,TEXT("runner lost")); return; }
            S->bRunnerMoving |= Enemy->GetVelocity().Size2D()>10.f;
            if (Now-S->Mark>10) { End(false,TEXT("runner arrival")); return; }
            if (AI->HasReachedStagingPoint())
            {
                S->bPass &= S->bRunnerMoving && FVector::Dist2D(Enemy->GetActorLocation(),S->NavB)<65;
                // Reuse this live pawn as a waiting guard, then request a distinct cover move.
                Enemy->EncounterBehavior.EntryMode=EOBEnemyEntryMode::WaitAtSpawn;
                Enemy->EncounterBehavior.CoverMode=EOBEnemyCoverMode::MoveToCover;
                Enemy->EncounterBehavior.bFacePlayer=true; Enemy->EncounterBehavior.ReactionDelay=.4f;
                Enemy->EncounterBehavior.CoverHoldSeconds=1.f; Enemy->EncounterBehavior.bStartStateTreeAfterReaction=true;
                AI->PrepareEncounterEntry(FTransform(S->NavA),FTransform(S->NavA));
                S->GuardStart=Enemy->GetActorLocation(); S->Mark=Now; S->Stage=3;
            }
        }
        else if (S->Stage==3 && Now-S->Mark>.6f)
        {
            auto* Enemy=S->Enemy.Get();
            S->bPass &= Enemy && Enemy->GetActorLocation().Equals(S->GuardStart,.2f) && Enemy->GetCombatState()==EOBEnemyCombatState::Idle;
            if (!Enemy) { End(false,TEXT("guard lost")); return; }
            S->StandingHead=Enemy->GetMesh()->GetSocketLocation(TEXT("head")).Z;
            auto* Camera=W->SpawnActor<ACameraActor>();
            const FVector Mid=(S->NavA+S->NavB)*.5f+FVector(0,0,80);
            const FVector Side=FVector::CrossProduct((S->NavB-S->NavA).GetSafeNormal2D(),FVector::UpVector);
            const FVector Position=Mid+Side*300.f+FVector(0,0,30);
            Camera->SetActorLocationAndRotation(Position,(Mid-Position).Rotation());
            Camera->GetCameraComponent()->SetFieldOfView(65.f); PC->SetViewTarget(Camera);
            Enemy->StartEncounterBehavior();
            S->bPass &= Enemy->GetCombatState()==EOBEnemyCombatState::Idle;
            S->Mark=Now; S->Stage=4;
        }
        else if (S->Stage==4)
        {
            auto* Enemy=S->Enemy.Get(); if (!Enemy) { End(false,TEXT("cover guard lost")); return; }
            S->bCoverMoving |= Enemy->GetCombatState()==EOBEnemyCombatState::MovingToCover && Enemy->GetVelocity().Size2D()>10;
            if (Enemy->GetCombatState()==EOBEnemyCombatState::InCover && Enemy->bIsCrouched)
            {
                S->bCrouched=true;
                S->bPass &= FVector::Dist2D(Enemy->GetActorLocation(),S->NavA)<65;
                FScreenshotRequest::RequestScreenshot(Folder/TEXT("EnemyCover.png"),false,false,false,FIntRect(),true);
            }
            auto* AI=Cast<AOBAIController>(Enemy->GetController());
            auto* Tree=AI ? AI->FindComponentByClass<UStateTreeAIComponent>() : nullptr;
            if (Tree && Tree->IsRunning())
            {
                S->bPass &= S->bCoverMoving && S->bCrouched;
                UE_LOG(LogTemp,Display,TEXT("INFILTRATION_PIE cover arrived and crouched; combat StateTree running"));
                End(S->bPass,TEXT("squad routes / runner / waiting guard / cover / combat"));
            }
            else if (Now-S->Mark>15) End(false,TEXT("cover or StateTree timeout"));
        }
    }),.05f,true);
}
static FAutoConsoleCommand GOBInfiltrationSmoke(TEXT("OB.Infiltration.IntegrationPIE"),
    TEXT("Verify saved Stage_01 squad branching, nav entry and cover-to-combat in disposable PIE; exits its process."),
    FConsoleCommandDelegate::CreateLambda([]() {
        if (!GEditor) return;
        FRequestPlaySessionParams Params; Params.GlobalMapOverride=TEXT("/Game/Maps/Stage_01");
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        Params.bAllowOnlineSubsystem=false; GEditor->RequestPlaySession(Params);
        const double Started=FPlatformTime::Seconds();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Started](float) {
            if (GEditor && GEditor->PlayWorld) { RunInfiltrationSmoke(GEditor->PlayWorld); return false; }
            if (FPlatformTime::Seconds()-Started>90) { FPlatformMisc::RequestExitWithStatus(false,1); return false; }
            return true;
        }),.05f);
    }));
#endif

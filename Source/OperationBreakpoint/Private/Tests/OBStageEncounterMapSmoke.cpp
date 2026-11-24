#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Editor.h"
#include "LevelEditor.h"
#include "Containers/Ticker.h"
#include "EngineUtils.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "Navigation/PathFollowingComponent.h"
#include "Components/StateTreeAIComponent.h"
#include "Components/SplineComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "World/OBRailPath.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Character/OBActionComponent.h"
#include "Character/OBEnemyCharacter.h"
#include "AI/OBAIController.h"
#include "World/OBEncounterTrigger.h"
#include "World/OBEncounterSpawner.h"
#include "Core/OBPlayerController.h"
#include "Widget/OBHUD.h"
#include "GameFramework/PlayerController.h"
#include "ContentStreaming.h"
#include "Camera/CameraComponent.h"
#include "UnrealClient.h"

// Disposable PIE only: exercise real overlap triggers, then clear each wave through the damage path.
class FOBStageEncounterMapSmoke
{
public:
    static void Run(UWorld* World,bool bZone6Only=false,int32 OnlyPoint=INDEX_NONE)
    {
        struct FEnemySample { FVector Spawn; double Seen=0; bool bMoved=false; };
        struct FState
        {
            double Started=0, WaveStarted=-1;
            int32 Wave=0, VerifiedRunners=0, SpawnCount=0;
            int32 VerifiedFacingStops=0, VerifiedDepartures=0, ReturnTurnSamples=0;
            int32 FirstPersonTravelSamples=0;
            bool bWasReturning=false;
            FVector ReturnLocation=FVector::ZeroVector;
            int64 PeakTextureOverBudget=0;
            int32 PeakLightingUnbuilt=0,PeakTextureUnbuilt=0;
            bool bHostageTargetsVerified=false;
            TArray<TWeakObjectPtr<AOBEnemyCharacter>> Hostages;
            TArray<TWeakObjectPtr<AOBEncounterTrigger>> Triggers;
            TMap<TWeakObjectPtr<AOBEnemyCharacter>,FEnemySample> Samples;
            TArray<FString> Failures;
            FTimerHandle Timer;
        };
        auto S=MakeShared<FState>(); S->Started=World->GetTimeSeconds();
        const int32 SelectedPoint=bZone6Only ? 85 : OnlyPoint;
        for (TActorIterator<AOBEncounterTrigger> It(World); It; ++It)
            if (!It->Spawners.IsEmpty() && (SelectedPoint==INDEX_NONE || It->PointIndex==SelectedPoint)) S->Triggers.Add(*It);
        S->Triggers.Sort([](const auto& A,const auto& B) { return A->PointIndex<B->PointIndex; });
        if (SelectedPoint!=INDEX_NONE)
        {
            auto* PC=World->GetFirstPlayerController();
            auto* Player=PC ? Cast<AOBPlayerCharacter>(PC->GetPawn()) : nullptr;
            auto* C=Player ? Player->GetPlayerSplineComponent() : nullptr;
            if (C && C->GetRailPath())
            {
                C->CurrentPointIndex=SelectedPoint-1; C->NextSplineCuePoint=SelectedPoint; C->TargetPointIndex=SelectedPoint;
                C->TargetDistance=C->GetRailPath()->GetSpline()->GetDistanceAlongSplineAtSplinePoint(SelectedPoint);
                C->CurrentDistance=FMath::Max(0.f,C->TargetDistance-200.f);
                C->RailState=UOBPlayerSplineComponent::EOBRailState::Moving;
                C->ApplySplineTransform(C->CurrentDistance);
            }
        }
        TWeakObjectPtr<UWorld> WeakWorld=World;
        if (auto* PC=World->GetFirstPlayerController())
            if (auto* Player=Cast<AOBPlayerCharacter>(PC->GetPawn()))
            {
                auto* C=Player->GetPlayerSplineComponent();
                C->OnTravelStarted.AddWeakLambda(C,[S,C,WeakWorld]() {
                    auto* W=WeakWorld.Get();
                    if (!W || C->CurrentPointIndex<=0) return;
                    const float Yaw=C->RailPath->GetSpline()->GetDirectionAtDistanceAlongSpline(
                        C->CurrentDistance,ESplineCoordinateSpace::World).Rotation().Yaw;
                    if (FMath::Abs(FMath::FindDeltaAngleDegrees(C->PlayerCharacter->GetActorRotation().Yaw,Yaw))>1.f)
                        S->Failures.Add(TEXT("Player departed before facing the outgoing spline"));
                    else ++S->VerifiedDepartures;
                });
            }
        World->GetTimerManager().SetTimer(S->Timer,FTimerDelegate::CreateLambda([S,WeakWorld,bZone6Only,SelectedPoint]() {
            auto* W=WeakWorld.Get(); if (!W) return;
            auto Finish=[&](const TCHAR* Reason) {
                if (bZone6Only && (S->Hostages.Num()!=2 || !S->bHostageTargetsVerified)) S->Failures.Add(TEXT("Expected two hostages and four combat targets"));
                for (auto Ref : S->Hostages)
                    if (!Ref.IsValid() || Ref->GetCurrentHealth()<=0) S->Failures.Add(TEXT("Hostage was lost before combat completion"));
                if (S->PeakTextureOverBudget>0 || S->PeakLightingUnbuilt>0 || S->PeakTextureUnbuilt>0)
                    S->Failures.Add(TEXT("Lighting/texture streaming warnings remain during the route"));
                if (SelectedPoint==INDEX_NONE && (S->VerifiedFacingStops!=6 || S->VerifiedDepartures!=5 || S->ReturnTurnSamples==0))
                    S->Failures.Add(TEXT("Expected six combat-facing stops and five spline-facing departures with a return turn"));
                if (SelectedPoint==INDEX_NONE && S->FirstPersonTravelSamples==0)
                    S->Failures.Add(TEXT("No first-person Stage travel samples were observed"));
                const bool bPass=S->Failures.IsEmpty() && S->Wave==S->Triggers.Num();
                FString Report=FString::Printf(TEXT("outcome=%s reason=%s waves=%d spawns=%d runners_verified=%d hostages_alive=%d captor_targets_verified=%d\n"),
                    bPass ? TEXT("PASS") : TEXT("FAIL"),Reason,S->Wave,S->SpawnCount,S->VerifiedRunners,S->Hostages.Num(),S->bHostageTargetsVerified);
                Report+=FString::Printf(TEXT("texture_over_budget_peak_bytes=%lld lighting_unbuilt_peak=%d texture_unbuilt_peak=%d\n"),
                    S->PeakTextureOverBudget,S->PeakLightingUnbuilt,S->PeakTextureUnbuilt);
                Report+=FString::Printf(TEXT("combat_facing_verified=%d departure_facing_verified=%d return_turn_samples=%d\n"),
                    S->VerifiedFacingStops,S->VerifiedDepartures,S->ReturnTurnSamples);
                Report+=FString::Printf(TEXT("first_person_travel_samples=%d\n"),S->FirstPersonTravelSamples);
                for (const auto& Error : S->Failures) Report+=Error+TEXT("\n");
                const FString File=bZone6Only ? TEXT("Build/Testing/Verification/StageEncounters/hostage-runtime.txt")
                    : SelectedPoint!=INDEX_NONE ? FString::Printf(TEXT("Build/Testing/Verification/StageRepair/zone%d-runtime.txt"),SelectedPoint)
                    : TEXT("Build/Testing/Verification/StageEncounters/runtime.txt");
                FFileHelper::SaveStringToFile(Report,*(FPaths::ProjectDir()/File));
                UE_LOG(LogTemp,Display,TEXT("STAGE_ENCOUNTERS_PIE %s"),*Report);
                W->GetTimerManager().ClearTimer(S->Timer);
                GEditor->RequestEndPlayMap();
                FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([bPass](float) {
                    FPlatformMisc::RequestExitWithStatus(false,bPass ? 0 : 1); return false;
                }),1.f);
            };
            const double Now=W->GetTimeSeconds();
            if (Now-S->Started>10)
            {
                S->PeakTextureOverBudget=FMath::Max<int64>(S->PeakTextureOverBudget,IStreamingManager::Get().GetTextureStreamingManager().GetMemoryOverBudget());
                S->PeakLightingUnbuilt=FMath::Max(S->PeakLightingUnbuilt,int32(W->NumLightingUnbuiltObjects));
                S->PeakTextureUnbuilt=FMath::Max(S->PeakTextureUnbuilt,int32(W->NumTextureStreamingUnbuiltComponents+W->NumTextureStreamingDirtyResources));
            }
            if (Now-S->Started>210) { S->Failures.Add(TEXT("Whole route timed out")); Finish(TEXT("timeout")); return; }
            auto* PC=Cast<AOBPlayerController>(W->GetFirstPlayerController());
            auto* Player=PC ? Cast<AOBPlayerCharacter>(PC->GetPawn()) : nullptr;
            if (!Player) return;
            auto* RailMovement=Player->GetPlayerSplineComponent();
            const bool bReturning=RailMovement->RailState==UOBPlayerSplineComponent::EOBRailState::AligningForTravel;
            if (RailMovement->IsMoving() || bReturning)
            {
                ++S->FirstPersonTravelSamples;
                if (!RailMovement->UsesFirstPersonTravelCamera()
                    || !Player->GetPlayerCamera()->GetRelativeLocation().Equals(RailMovement->FirstPersonCameraLocation,.1)
                    || !Player->GetPlayerCamera()->GetRelativeRotation().Equals(RailMovement->FirstPersonCameraRotation,.1))
                    S->Failures.AddUnique(TEXT("Stage travel switched out of its first-person camera"));
            }
            if (bReturning)
            {
                ++S->ReturnTurnSamples;
                if (S->bWasReturning && !Player->GetActorLocation().Equals(S->ReturnLocation,.1f))
                    S->Failures.AddUnique(TEXT("Player moved while returning to the spline heading"));
                S->ReturnLocation=Player->GetActorLocation();
            }
            S->bWasReturning=bReturning;
            Player->SetCanBeDamaged(false);
            if (S->Triggers.Num()!=(SelectedPoint!=INDEX_NONE ? 1 : 6)) { S->Failures.Add(TEXT("Expected zone trigger bindings are missing")); Finish(TEXT("configuration")); return; }
            if (S->Wave>=S->Triggers.Num())
            {
                if (bZone6Only && (Player->GetActionComponent()->HasEncounterEnemies() || !PC->GetOBHUD() || !PC->GetOBHUD()->IsMenuVisible()))
                    S->Failures.Add(TEXT("Final result did not open after the four captors died"));
                Finish(TEXT("all configured waves cleared")); return;
            }
            auto* Trigger=S->Triggers[S->Wave].Get();
            if (!Trigger) { S->Failures.Add(TEXT("Trigger destroyed")); Finish(TEXT("configuration")); return; }
            bool bAllSpawned=true,bAllReady=true;
            for (AOBEncounterSpawner* Spawner : Trigger->Spawners)
            {
                auto* Enemy=Spawner ? Spawner->GetSpawnedEnemy() : nullptr;
                if (!Enemy) { bAllSpawned=false; bAllReady=false; continue; }
                auto* AI=Cast<AOBAIController>(Enemy->GetController());
                auto* Sample=S->Samples.Find(Enemy);
                if (!Sample)
                {
                    FEnemySample New; New.Spawn=Enemy->GetActorLocation(); New.Seen=Now;
                    Sample=&S->Samples.Add(Enemy,New); ++S->SpawnCount;
                    if (S->WaveStarted<0) S->WaveStarted=Now;
                }
                if (!Spawner->bParticipatesInCombat)
                {
                    S->Hostages.AddUnique(Enemy);
                    if (Enemy->IsEncounterCombatant() || Enemy->GetCombatState()!=EOBEnemyCombatState::Idle
                        || Enemy->OnKilled.IsBoundToObject(Player->GetActionComponent())
                        || (AI && (AI->GetMoveStatus()!=EPathFollowingStatus::Idle || AI->FindComponentByClass<UStateTreeAIComponent>()->IsRunning())))
                        S->Failures.AddUnique(TEXT("Hostage was registered for combat or activated AI"));
                    continue;
                }
                if (!AI) { S->Failures.AddUnique(TEXT("Spawned pawn lacks AI controller")); bAllReady=false; continue; }
                const float Moved=FVector::Dist2D(Sample->Spawn,Enemy->GetActorLocation());
                if (Spawner->bMoveToStagingPoint)
                {
                    if (Now-Sample->Seen<2.65 && Moved>4.f)
                        S->Failures.AddUnique(FString::Printf(TEXT("Runner moved before its 3-second delay: %s"),*Spawner->GetActorLabel()));
                    if (Moved>20.f) Sample->bMoved=true;
                }
                else if (AI->GetMoveStatus()!=EPathFollowingStatus::Idle)
                    S->Failures.AddUnique(FString::Printf(TEXT("Stationary spawn requested AI movement: %s"),*Spawner->GetActorLabel()));
                bAllReady &= AI->HasReachedStagingPoint();
            }
            if (!bAllSpawned) return;
            auto* Spline=Player->GetPlayerSplineComponent();
            if (!Spline->IsAtActionPoint() || Spline->GetCurrentPointIndex()!=Trigger->PointIndex) return;
            if (Trigger->PointIndex==85)
            {
                S->bHostageTargetsVerified=Player->GetActionComponent()->TargetKillCount==4;
                if (!S->bHostageTargetsVerified) S->Failures.AddUnique(TEXT("Zone6 combat count includes hostages"));
            }
            if (auto* HUD=PC->GetOBHUD(); HUD && HUD->IsMenuVisible())
                S->Failures.AddUnique(TEXT("Result menu appeared before encounter was cleared"));
            const bool bTimedOut=Now-S->WaveStarted>16;
            if ((!bAllReady && !bTimedOut) || Now-S->WaveStarted<3.6) return;
            FVector Focus;
            const auto* Stop=Spline->GetRailPath()->FindStopSettings(Trigger->PointIndex);
            const bool bManual=Stop && Stop->bOverrideFacingYaw;
            if (bManual || Trigger->GetCombatFacingLocation(Focus))
            {
                // Entry direction is fixed at the authored stop, not recomputed after floor/penetration correction.
                const FVector StopLocation=Spline->GetRailPath()->GetSpline()->GetLocationAtSplinePoint(Trigger->PointIndex,ESplineCoordinateSpace::World);
                const float ExpectedYaw=bManual ? Stop->FacingYaw : (Focus-StopLocation).Rotation().Yaw;
                UE_LOG(LogTemp,Display,TEXT("COMBAT_FACING point=%d expected=%.2f actor=%.2f control=%.2f target=%.2f location=%s stop=%s"),
                    Trigger->PointIndex,ExpectedYaw,Player->GetActorRotation().Yaw,PC->GetControlRotation().Yaw,
                    Spline->TargetStopRotation.Yaw,*Player->GetActorLocation().ToCompactString(),*StopLocation.ToCompactString());
                if (FMath::Abs(FMath::FindDeltaAngleDegrees(Player->GetActorRotation().Yaw,ExpectedYaw))>1.f
                    || FMath::Abs(FMath::FindDeltaAngleDegrees(PC->GetControlRotation().Yaw,ExpectedYaw))>1.f)
                    S->Failures.Add(FString::Printf(TEXT("Combat view missed its zone at point %d: expected=%.2f actor=%.2f control=%.2f target=%.2f"),
                        Trigger->PointIndex,ExpectedYaw,Player->GetActorRotation().Yaw,PC->GetControlRotation().Yaw,Spline->TargetStopRotation.Yaw));
                else ++S->VerifiedFacingStops;
            }
            if (Trigger->PointIndex==64)
                for (TActorIterator<AActor> It(W);It;++It)
                    if (It->GetActorLabel()==TEXT("BP_Simple_Door11"))
                    {
                        TInlineComponentArray<UStaticMeshComponent*> Meshes(*It);
                        for (auto* Mesh : Meshes) UE_LOG(LogTemp,Display,TEXT("ZONE4_DOOR component=%s rotation=%s"),*Mesh->GetName(),*Mesh->GetComponentRotation().ToCompactString());
                    }
            if (!bZone6Only) FScreenshotRequest::RequestScreenshot(FPaths::ProjectDir()/TEXT("Build/Testing/Verification/StageRepair")/
                FString::Printf(TEXT("Stage_Point_%02d.png"),Trigger->PointIndex),true,false);
            for (AOBEncounterSpawner* Spawner : Trigger->Spawners)
            {
                if (!Spawner->bParticipatesInCombat) continue;
                auto* Enemy=Spawner->GetSpawnedEnemy();
                auto* AI=Cast<AOBAIController>(Enemy->GetController());
                const auto& Sample=S->Samples.FindChecked(Enemy);
                if (Spawner->bMoveToStagingPoint)
                {
                    if (Sample.bMoved && AI->HasReachedStagingPoint()) ++S->VerifiedRunners;
                    else S->Failures.Add(FString::Printf(TEXT("Runner failed to reach authored StagingPoint: %s | spawn=%s target=%s actual=%s"),
                        *Spawner->GetActorLabel(),*Sample.Spawn.ToCompactString(),*Spawner->GetStagingTransform().GetLocation().ToCompactString(),*Enemy->GetActorLocation().ToCompactString()));
                    if (!AI->HasReachedStagingPoint())
                    {
                        FCollisionQueryParams Q(SCENE_QUERY_STAT(OBRunnerBlocked),false,Enemy);
                        for (int32 Direction=0;Direction<8;++Direction)
                        {
                            const float Angle=Direction*PI/4.f;
                            FHitResult Hit;
                            W->SweepSingleByChannel(Hit,Enemy->GetActorLocation(),Enemy->GetActorLocation()+FVector(FMath::Cos(Angle)*90,FMath::Sin(Angle)*90,0),
                                FQuat::Identity,Enemy->GetCapsuleComponent()->GetCollisionObjectType(),
                                FCollisionShape::MakeCapsule(Enemy->GetCapsuleComponent()->GetScaledCapsuleRadius(),Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()),Q,
                                FCollisionResponseParams(Enemy->GetCapsuleComponent()->GetCollisionResponseToChannels()));
                            if (Hit.bBlockingHit) UE_LOG(LogTemp,Display,TEXT("RUNNER_BLOCKER spawner=%s direction=%d actor=%s component=%s point=%s normal=%s penetrating=%d"),
                                *Spawner->GetActorLabel(),Direction,Hit.GetActor() ? *Hit.GetActor()->GetActorLabel() : TEXT("None"),
                                *GetNameSafe(Hit.GetComponent()),*Hit.ImpactPoint.ToCompactString(),*Hit.ImpactNormal.ToCompactString(),Hit.bStartPenetrating);
                        }
                    }
                }
                Enemy->TakeDamage(100000.f,FDamageEvent(),PC,Player);
            }
            UE_LOG(LogTemp,Display,TEXT("STAGE_ENCOUNTER_WAVE point=%d spawned=%d ready=%d runners_verified=%d failures=%d"),
                Trigger->PointIndex,Trigger->Spawners.Num(),bAllReady,S->VerifiedRunners,S->Failures.Num());
            ++S->Wave; S->WaveStarted=-1;
        }),.05f,true);
    }
};
static FAutoConsoleCommand GOBStageEncountersPIE(TEXT("OB.Stage.EncountersPIE"),
    TEXT("Test saved Stage_01 zone overlaps and delayed movers, auto-clear combat and exit this disposable editor process."),
    FConsoleCommandDelegate::CreateLambda([]() {
        if (!GEditor) return;
        FRequestPlaySessionParams Params; Params.GlobalMapOverride=TEXT("/Game/Maps/Stage_01");
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        Params.bAllowOnlineSubsystem=false; GEditor->RequestPlaySession(Params);
        const double Started=FPlatformTime::Seconds();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Started](float) {
            if (GEditor && GEditor->PlayWorld) { FOBStageEncounterMapSmoke::Run(GEditor->PlayWorld); return false; }
            if (FPlatformTime::Seconds()-Started>60) { FPlatformMisc::RequestExitWithStatus(false,1); return false; }
            return true;
        }),.05f);
    }));
static FAutoConsoleCommand GOBStageZonePIE(TEXT("OB.Stage.ZonePIE"),
    TEXT("Test a single saved encounter through its physical box, then exit this disposable editor. Argument: rail point index."),
    FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args) {
        if (!GEditor) return;
        const int32 Point=Args.IsEmpty() ? 64 : FCString::Atoi(*Args[0]);
        FRequestPlaySessionParams Params; Params.GlobalMapOverride=TEXT("/Game/Maps/Stage_01");
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        Params.bAllowOnlineSubsystem=false;GEditor->RequestPlaySession(Params);
        const double Started=FPlatformTime::Seconds();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Started,Point](float) {
            if (GEditor && GEditor->PlayWorld && GEditor->PlayWorld->GetTimeSeconds()>.1f)
            { FOBStageEncounterMapSmoke::Run(GEditor->PlayWorld,false,Point); return false; }
            if (FPlatformTime::Seconds()-Started>60) { FPlatformMisc::RequestExitWithStatus(false,1); return false; }
            return true;
        }),.05f);
    }));
static FAutoConsoleCommand GOBStageHostagesPIE(TEXT("OB.Stage.HostagesPIE"),
    TEXT("Start near the saved Zone6 trigger, verify two noncombatants and four captor kills, then exit this disposable editor process."),
    FConsoleCommandDelegate::CreateLambda([]() {
        if (!GEditor) return;
        FRequestPlaySessionParams Params; Params.GlobalMapOverride=TEXT("/Game/Maps/Stage_01");
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        Params.bAllowOnlineSubsystem=false; GEditor->RequestPlaySession(Params);
        const double Started=FPlatformTime::Seconds();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Started](float) {
            if (GEditor && GEditor->PlayWorld && GEditor->PlayWorld->GetTimeSeconds()>.1f)
            { FOBStageEncounterMapSmoke::Run(GEditor->PlayWorld,true); return false; }
            if (FPlatformTime::Seconds()-Started>60) { FPlatformMisc::RequestExitWithStatus(false,1); return false; }
            return true;
        }),.05f);
    }));
#endif

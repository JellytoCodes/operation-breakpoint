#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "AI/OBAIController.h"
#include "AI/OBEnemyCombatPattern.h"
#include "Character/OBAllyCharacter.h"
#include "Character/OBAllySplineComponent.h"
#include "Character/OBEnemyCharacter.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBActionComponent.h"
#include "Character/OBPlayerSplineComponent.h"
#include "World/OBAllyRoute.h"
#include "World/OBAllyCueTrigger.h"
#include "World/OBRailPath.h"
#include "World/OBEncounterSpawner.h"
#include "Components/SplineComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "TimerManager.h"

namespace
{
    struct FTestWorld
    {
        UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
        FTestWorld() { GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World); }
        ~FTestWorld() { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
    };
    AOBAllyRoute* MakeRoute(UWorld* W,std::initializer_list<FVector> Points)
    {
        auto* Route=W->SpawnActor<AOBAllyRoute>(); auto* Spline=Route->GetSpline();
        Spline->ClearSplinePoints(false);
        for (const auto& P : Points) Spline->AddSplinePoint(P,ESplineCoordinateSpace::World,false);
        for (int32 I=0; I<Spline->GetNumberOfSplinePoints(); ++I) Spline->SetSplinePointType(I,ESplinePointType::Linear,false);
        Spline->UpdateSpline(); return Route;
    }
    void AdvanceTimers(UWorld* W,float Seconds)
    {
        for (float Elapsed=0; Elapsed<Seconds; Elapsed+=.05f) { ++GFrameCounter; W->GetTimerManager().Tick(.05f); }
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBAllyRouteTest,"OperationBreakpoint.Infiltration.AllyRouteXYZAndCues",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOBAllyRouteTest::RunTest(const FString&)
{
    FTestWorld Fixture; auto* W=Fixture.World;
    auto* Ally=W->SpawnActor<AOBAllyCharacter>(); auto* Follower=Ally->GetRouteComponent();
    Follower->bSweepMovement=false; Follower->bPauseDuringPlayerAction=false;
    auto* Route=MakeRoute(W,{FVector::ZeroVector,FVector(300,0,300),FVector(600,0,100)});
    for (int32 FPS : {30,120})
    {
        TestTrue(TEXT("Route starts"),Follower->StartRoute(Route));
        for (int32 I=0; I<FPS; ++I) Follower->TickComponent(1.f/FPS,LEVELTICK_All,&Follower->PrimaryComponentTick);
        TestTrue(TEXT("Speed is stable across 30/120 FPS"),FMath::IsNearlyEqual(Follower->GetDistanceAlongRoute(),300.f,.1f));
        FVector Feet=Ally->GetActorLocation()-FVector(0,0,Ally->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+Follower->GroundClearance);
        TestTrue(TEXT("Spline controls XYZ including steep ascent"),Feet.Equals(Route->GetSpline()->GetLocationAtDistanceAlongSpline(300,ESplineCoordinateSpace::World),.1f));
        Follower->PauseRoute(); const FVector Stopped=Ally->GetActorLocation();
        Follower->TickComponent(1.f,LEVELTICK_All,&Follower->PrimaryComponentTick);
        TestTrue(TEXT("Explicit pause holds position and velocity"),Ally->GetActorLocation().Equals(Stopped,.01f) && Ally->GetVelocity().IsNearlyZero());
        Follower->ResumeRoute();
        for (int32 I=0; I<FPS*10 && !Follower->HasCompletedRoute(); ++I) Follower->TickComponent(1.f/FPS,LEVELTICK_All,&Follower->PrimaryComponentTick);
        TestTrue(TEXT("Route completes once"),Follower->HasCompletedRoute());
        TestTrue(TEXT("Body remains upright on slopes"),FMath::IsNearlyZero(Ally->GetActorRotation().Pitch) && FMath::IsNearlyZero(Ally->GetActorRotation().Roll));
    }
    auto* Approach=MakeRoute(W,{FVector::ZeroVector,FVector(100,0,0),FVector(200,0,0)});
    auto* Branch=MakeRoute(W,{FVector(100,0,0),FVector(100,100,60)});
    auto& Cue=Approach->PointCues.Add(1); Cue.bWaitForSignal=true; Cue.WaitSeconds=.2f; Cue.BranchRoute=Branch;
    Follower->StartRoute(Approach); Follower->TickComponent(1.f,LEVELTICK_All,&Follower->PrimaryComponentTick);
    TestTrue(TEXT("Junction waits for an external signal"),Follower->IsWaiting() && FMath::IsNearlyEqual(Follower->GetDistanceAlongRoute(),100.f));
    Follower->TickComponent(10.f,LEVELTICK_All,&Follower->PrimaryComponentTick);
    TestEqual(TEXT("Waiting does not branch automatically"),Follower->GetCurrentRoute(),Approach);
    Follower->ResumeRoute(); Follower->TickComponent(1.f,LEVELTICK_All,&Follower->PrimaryComponentTick);
    TestTrue(TEXT("Resume joins the separate branch continuously"),Follower->GetCurrentRoute()==Branch && Follower->HasCompletedRoute());
    Follower->StartRoute(Approach); Follower->ResumeRoute();
    Follower->TickComponent(2.f,LEVELTICK_All,&Follower->PrimaryComponentTick);
    TestTrue(TEXT("A signal received before the ally reaches its gate is not lost"),Follower->GetCurrentRoute()==Branch && Follower->HasCompletedRoute());
    auto* Far=MakeRoute(W,{FVector(5000,0,0),FVector(5100,0,0)});
    Follower->StartRoute(Approach); Follower->QueueBranch(Far); Follower->ResumeRoute();
    AddExpectedError(TEXT("Ally route join rejected"),EAutomationExpectedErrorFlags::Contains,1);
    Follower->TickComponent(2.f,LEVELTICK_All,&Follower->PrimaryComponentTick);
    TestTrue(TEXT("Disconnected branches do not teleport the ally"),Follower->GetCurrentRoute()==Approach && Follower->IsWaiting()
        && Ally->GetActorLocation().X<200.f);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBEnemyEncounterBehaviorTest,"OperationBreakpoint.Infiltration.EnemyEntryAndReaction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOBEnemyEncounterBehaviorTest::RunTest(const FString&)
{
    FTestWorld Fixture; auto* W=Fixture.World;
    auto* Player=W->SpawnActor<AOBPlayerCharacter>(FVector(0,400,0),FRotator::ZeroRotator);
    auto* PC=W->SpawnActor<APlayerController>(); W->AddController(PC); PC->Possess(Player);
    auto* EnemyClass=LoadClass<AOBEnemyCharacter>(nullptr,TEXT("/Game/Blueprints/Enemy/BP_EnemyCharacter.BP_EnemyCharacter_C"));
    if (!TestNotNull(TEXT("Configured enemy Blueprint loads"),EnemyClass)) return false;
    auto* Enemy=W->SpawnActorDeferred<AOBEnemyCharacter>(EnemyClass,FTransform::Identity,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    Enemy->AutoPossessAI=EAutoPossessAI::Disabled; Enemy->FinishSpawning(FTransform::Identity);
    auto* ProductionPatterns=Enemy->CombatPatterns.Get();
    if (!TestNotNull(TEXT("Production Blueprint has the StateTree pattern set"),ProductionPatterns)) return false;
    // This part tests the legacy controller-owned reaction. The production
    // Blueprint now assigns CombatPatterns, whose StateTree owns the first crouch.
    Enemy->CombatPatterns=nullptr;
    auto* AI=W->SpawnActor<AOBAIController>(); AI->Possess(Enemy);
    Enemy->EncounterBehavior.EntryMode=EOBEnemyEntryMode::WaitAtSpawn;
    Enemy->EncounterBehavior.CoverMode=EOBEnemyCoverMode::CrouchInPlace;
    Enemy->EncounterBehavior.bFacePlayer=true; Enemy->EncounterBehavior.bStartStateTreeAfterReaction=false;
    Enemy->EncounterBehavior.ReactionDelay=.3f;
    const FVector Before=Enemy->GetActorLocation();
    TestTrue(TEXT("Guard entry requires no navigation or staging run"),AI->PrepareEncounterEntry(FTransform(FVector(1000,0,0)),FTransform::Identity));
    TestTrue(TEXT("Waiting guard is ready without changing position"),AI->HasReachedStagingPoint() && Enemy->GetActorLocation().Equals(Before));
    static_cast<AActor*>(AI)->Tick(.5f);
    AddInfo(FString::Printf(TEXT("Facing diagnostic | player=%s enemy=%s yaw=%.2f world_pc=%s"),*Player->GetActorLocation().ToString(),*Enemy->GetActorLocation().ToString(),Enemy->GetActorRotation().Yaw,*GetNameSafe(W->GetFirstPlayerController())));
    TestTrue(TEXT("Waiting guard smoothly faces the player"),FMath::IsNearlyEqual(Enemy->GetActorRotation().Yaw,90.f,1.f));
    AI->StartEncounterBehavior(); AI->StartEncounterBehavior();
    TestTrue(TEXT("Reaction delay prevents immediate cover"),Enemy->GetCombatState()==EOBEnemyCombatState::Idle);
    AdvanceTimers(W,.5f);
    TestTrue(TEXT("Delayed reaction enters cover"),Enemy->GetCombatState()==EOBEnemyCombatState::InCover);
    TestTrue(TEXT("Cover requests actual Character crouch"),Enemy->GetCharacterMovement()->bWantsToCrouch);
    Enemy->SetCombatState(EOBEnemyCombatState::Attacking);
    TestFalse(TEXT("Leaving cover releases crouch"),Enemy->GetCharacterMovement()->bWantsToCrouch);
    auto* Spawner=W->SpawnActor<AOBEncounterSpawner>();
    Spawner->PlacedEnemy=Enemy; Spawner->bOverrideEnemyBehavior=true; Spawner->EnemyBehavior=Enemy->EncounterBehavior;
    Spawner->bMoveToStagingPoint=false;
    Spawner->EnemyBehavior.RunSpeed=444.f; Spawner->PrepareEncounter(7);
    TestTrue(TEXT("Spawner can register a level-placed guard without spawning a replacement"),Spawner->GetSpawnedEnemy()==Enemy && Spawner->IsPrepared());
    TestEqual(TEXT("Per-spawner behavior overrides reach the pawn"),Enemy->GetCharacterMovement()->MaxWalkSpeed,444.f);
    Spawner->PrepareEncounter(8);
    TestEqual(TEXT("Prepared spawner does not duplicate or reset the same guard"),Spawner->GetPreparedPointIndex(),7);
    Enemy->CombatPatterns=ProductionPatterns;
    Player->GetActionComponent()->SetExternalActionState(true);
    AI->PrepareEncounterEntry(FTransform::Identity,FTransform::Identity);
    TestTrue(TEXT("The production pattern is eligible after staging and player arrival"),Enemy->CanRunCombatPattern());
    AI->StartEncounterBehavior();AdvanceTimers(W,.5f);
    TestTrue(TEXT("A pattern-driven guard leaves the first posture to its StateTree"),
        Enemy->GetCombatState()==EOBEnemyCombatState::Idle && !Enemy->GetCharacterMovement()->bWantsToCrouch);
    TestFalse(TEXT("Disabling automatic tree start does not silently start a pattern"),Enemy->IsCombatPatternActive() || AI->IsCombatTreeRunning());
    Enemy->BeginCombatPattern();
    Enemy->BeginPatternAction(EOBEnemyPatternAction::Posture,EOBEnemyPosture::Crouch);
    TestTrue(TEXT("The pattern posture action enters cover and requests the crouch"),
        Enemy->GetCombatState()==EOBEnemyCombatState::InCover && Enemy->GetCharacterMovement()->bWantsToCrouch);
    Enemy->StopCombatPattern();
    AI->PrepareEncounterEntry(FTransform::Identity,FTransform::Identity); AI->StartEncounterBehavior();
    Enemy->SetCombatState(EOBEnemyCombatState::Dead); AI->StopStateTree(); AdvanceTimers(W,1.f);
    TestTrue(TEXT("Death/stop cancels pending reactions"),Enemy->GetCombatState()==EOBEnemyCombatState::Dead);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBAllyPresentationTest,"OperationBreakpoint.Infiltration.AllyTriggerAndActionVisibility",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOBAllyPresentationTest::RunTest(const FString&)
{
    FTestWorld Fixture; auto* W=Fixture.World;
    auto* Player=W->SpawnActor<AOBPlayerCharacter>(); auto* PC=W->SpawnActor<APlayerController>(); W->AddController(PC); PC->Possess(Player);
    auto* Ally=W->SpawnActor<AOBAllyCharacter>();
    auto* Route=MakeRoute(W,{FVector::ZeroVector,FVector(500,0,0)});
    Ally->GetRouteComponent()->AssignedRoute=Route;
    auto* Trigger=W->SpawnActor<AOBAllyCueTrigger>();
    auto& Command=Trigger->Commands.AddDefaulted_GetRef(); Command.Ally=Ally;
    TestTrue(TEXT("Cue starts the assigned ally route"),Trigger->ActivateForPlayer(Player) && Ally->GetRouteComponent()->HasRouteControl());
    TestFalse(TEXT("One-shot cue cannot restart a running route"),Trigger->ActivateForPlayer(Player));
    TestFalse(TEXT("Cinematic ally cannot be counted as an enemy"),Ally->IsA<AOBEnemyCharacter>());
    TestFalse(TEXT("Cinematic ally takes no damage"),Ally->CanBeDamaged());
    Ally->bHideWeaponDuringPlayerAction=true; // Old serialized asset values must not restore hiding.
    Ally->GetWeaponMesh()->SetHiddenInGame(true);
    Player->GetActionComponent()->SetExternalActionState(true); static_cast<AActor*>(Ally)->Tick(.016f);
    TestFalse(TEXT("The rifle remains visible during combat even with the old hide flag"),Ally->GetWeaponMesh()->bHiddenInGame);
    Player->GetActionComponent()->SetExternalActionState(false); static_cast<AActor*>(Ally)->Tick(.016f);
    TestFalse(TEXT("The rifle remains visible outside combat"),Ally->GetWeaponMesh()->bHiddenInGame);
    Ally->GetWeaponMesh()->SetVisibility(false);static_cast<AActor*>(Ally)->Tick(5.f);
    TestTrue(TEXT("Weapon visibility remains enabled without support firing"),Ally->GetWeaponMesh()->IsVisible() && !Ally->GetWeaponMesh()->bHiddenInGame);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBAllyRedeployTest,"OperationBreakpoint.Infiltration.AllyRedeployAndDeparture",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOBAllyRedeployTest::RunTest(const FString&)
{
    FTestWorld Fixture; auto* W=Fixture.World;
    auto* Player=W->SpawnActor<AOBPlayerCharacter>();
    auto* PC=W->SpawnActor<APlayerController>(); W->AddController(PC); PC->Possess(Player);
    auto* Rail=W->SpawnActor<AOBRailPath>(); auto* Path=Rail->GetSpline();
    Path->ClearSplinePoints(false);
    for (int32 I=0; I<8; ++I) Path->AddSplinePoint(FVector(I*300,0,0),ESplineCoordinateSpace::World,false);
    Path->UpdateSpline();
    auto* PlayerSpline=Player->GetPlayerSplineComponent();
    PlayerSpline->PlayerCharacter=Player; PlayerSpline->PlayerCamera=Player->GetPlayerCamera(); PlayerSpline->RailPath=Rail;
    PlayerSpline->CurrentPointIndex=5; PlayerSpline->NextSplineCuePoint=4;
    PlayerSpline->CurrentDistance=Path->GetDistanceAlongSplineAtSplinePoint(5);
    PlayerSpline->BeginAction();
    auto* Action=Player->GetActionComponent(); Action->PlayerSplineComponent=PlayerSpline; Action->SetExternalActionState(true);
    auto* OldRoute=MakeRoute(W,{FVector::ZeroVector,FVector(50,0,0)});
    auto* NextA=MakeRoute(W,{FVector(4000,100,100),FVector(4600,100,200)});
    auto* NextB=MakeRoute(W,{FVector(4000,-100,100),FVector(4600,-100,200)});
    auto* Trigger=W->SpawnActor<AOBAllyCueTrigger>();
    Trigger->Activation=EOBAllyCueActivation::PlayerSplinePoint; Trigger->PlayerPointIndex=5;
    TArray<AOBAllyCharacter*> Allies;
    for (auto* Next : {NextA,NextB})
    {
        auto* Ally=W->SpawnActor<AOBAllyCharacter>(); Allies.Add(Ally);
        auto* C=Ally->GetRouteComponent(); C->bSweepMovement=false; C->bPauseDuringPlayerAction=false;
        C->StartRoute(OldRoute); C->TickComponent(1,LEVELTICK_All,&C->PrimaryComponentTick);
        TestTrue(TEXT("Previous route completed before the same ally is reused"),C->HasCompletedRoute());
        auto& Entry=Trigger->Commands.AddDefaulted_GetRef();
        Entry.Ally=Ally; Entry.Route=Next; Entry.Command=EOBAllyCueCommand::StageForPlayerDeparture;
    }
    PlayerSpline->CurrentPointIndex=4;
    Trigger->TryBindPlayer();
    PlayerSpline->CurrentPointIndex=5;
    PlayerSpline->OnSplinePointReached.Broadcast(3);
    TestEqual(TEXT("Other player points do not redeploy"),Allies[0]->GetRouteComponent()->GetCurrentRoute(),OldRoute);
    // A long frame crosses both point 4 and 5. Point 5 must fire once, including preview-style traversal.
    PlayerSpline->NotifySplinePointsReached();
    TestTrue(TEXT("Both existing allies stage from a player point event"),
        Allies[0]->GetRouteComponent()->GetCurrentRoute()==NextA && Allies[1]->GetRouteComponent()->GetCurrentRoute()==NextB);
    for (auto* Ally : Allies)
    {
        auto* C=Ally->GetRouteComponent(); const FVector Staged=Ally->GetActorLocation();
        FVector Expected=C->GetCurrentRoute()->GetSpline()->GetLocationAtSplinePoint(0,ESplineCoordinateSpace::World);
        Expected.Z+=Ally->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+C->GroundClearance;
        TestTrue(TEXT("Redeploy teleports over disconnected routes with capsule height preserved"),Staged.Equals(Expected,.01));
        TestTrue(TEXT("Staging resets completion and animation velocity"),!C->HasCompletedRoute() && Ally->GetVelocity().IsNearlyZero());
        C->ResumeRoute(); C->TickComponent(2,LEVELTICK_All,&C->PrimaryComponentTick);
        TestTrue(TEXT("A generic Resume cannot release the player departure gate"),C->IsWaitingForPlayerDeparture() && Ally->GetActorLocation().Equals(Staged));
    }
    Action->CompleteAction();
    TestTrue(TEXT("Combat completion actually starts player travel"),PlayerSpline->IsMoving());
    for (auto* Ally : Allies)
    {
        auto* C=Ally->GetRouteComponent();
        TestFalse(TEXT("Player departure releases both staged allies"),C->IsWaitingForPlayerDeparture());
        C->TickComponent(.5f,LEVELTICK_All,&C->PrimaryComponentTick);
        TestTrue(TEXT("Allies proceed down their separate next routes"),C->GetDistanceAlongRoute()>100 && Ally->GetVelocity().Size()>0);
    }
    TestFalse(TEXT("One-shot point cue cannot teleport the squad a second time"),Trigger->ActivateForPlayer(Player));
    auto* C=Allies[0]->GetRouteComponent();
    C->StageRouteForPlayerDeparture(NextB,Player); C->StopRoute(); PlayerSpline->OnTravelStarted.Broadcast();
    TestFalse(TEXT("Stopped staging removes its departure subscription"),C->HasRouteControl() || C->IsWaitingForPlayerDeparture());
    C->StageRouteForPlayerDeparture(NextB,Player); C->StartRoute(NextA);
    TestFalse(TEXT("Explicit route replacement cancels old staging"),C->IsWaitingForPlayerDeparture());
    Rail->bPreviewEntireRoute=true; PlayerSpline->StartRoutePreview();
    C->StageRouteForPlayerDeparture(NextB,Player); C->TickComponent(.5f,LEVELTICK_All,&C->PrimaryComponentTick);
    TestTrue(TEXT("Route preview has no combat wait and continues the staged route"),!C->IsWaitingForPlayerDeparture() && C->GetDistanceAlongRoute()>0);
    // The entire squad configuration is validated before any teleport is applied.
    auto* Invalid=W->SpawnActor<AOBAllyCueTrigger>(); Invalid->Commands=Trigger->Commands;
    Invalid->Commands[1].Route=nullptr; const FVector Before=Allies[0]->GetActorLocation();
    AddExpectedError(TEXT("Invalid ally cue configuration"),EAutomationExpectedErrorFlags::Contains,1);
    TestFalse(TEXT("A missing destination rejects the entire squad cue"),Invalid->ActivateForPlayer(Player));
    TestTrue(TEXT("Invalid squad setup leaves already configured allies untouched"),Allies[0]->GetActorLocation().Equals(Before));
    return !HasAnyErrors();
}
#endif

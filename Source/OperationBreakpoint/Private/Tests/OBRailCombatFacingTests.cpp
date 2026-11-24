#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Camera/CameraComponent.h"
#include "Components/SplineComponent.h"
#include "GameFramework/PlayerController.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "World/OBRailPath.h"
#include "World/OBEncounterTrigger.h"
#include "World/OBEncounterSpawner.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBRailCombatFacingTest,"OperationBreakpoint.Infiltration.CombatFacingAndDeparture",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOBRailCombatFacingTest::RunTest(const FString&)
{
    auto* W=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
    ON_SCOPE_EXIT { W->DestroyWorld(false);GEngine->DestroyWorldContext(W); };
    auto* Player=W->SpawnActor<AOBPlayerCharacter>();
    auto* PC=W->SpawnActor<APlayerController>(); PC->Possess(Player);
    auto* Rail=W->SpawnActor<AOBRailPath>();
    auto* Path=Rail->GetSpline();Path->ClearSplinePoints(false);
    for (int32 I=0;I<3;++I)
    {
        Path->AddSplinePoint(FVector(I*300,0,0),ESplineCoordinateSpace::World,false);
        Path->SetSplinePointType(I,ESplinePointType::Linear,false);
    }
    Path->UpdateSpline();
    auto& Stops=*FindFProperty<FMapProperty>(AOBRailPath::StaticClass(),TEXT("StopPointSettings"))
        ->ContainerPtrToValuePtr<TMap<int32,FOBRailStopSettings>>(Rail);
    auto* Trigger=W->SpawnActor<AOBEncounterTrigger>();Trigger->PointIndex=1;
    auto* Runner=W->SpawnActor<AOBEncounterSpawner>();Runner->SetActorLocation(FVector(0,1000,0));
    auto* Guard=W->SpawnActor<AOBEncounterSpawner>();Guard->bMoveToStagingPoint=false;
    Guard->SetActorLocation(FVector(300,500,0));
    auto* Hostage=W->SpawnActor<AOBEncounterSpawner>();Hostage->bParticipatesInCombat=false;
    Hostage->SetActorLocation(FVector(10000,-10000,0));
    Trigger->Spawners={Runner,Guard,Hostage,nullptr};
    FVector Focus;
    TestTrue(TEXT("Combat focus resolves without spawning enemies"),Trigger->GetCombatFacingLocation(Focus));
    TestTrue(TEXT("Focus uses runner destination and guard spawn, excluding hostage"),Focus.Equals(FVector(300,750,0),.01));
    auto* C=Player->GetPlayerSplineComponent();
    C->PlayerCharacter=Player;C->PlayerCamera=Player->GetPlayerCamera();C->RailPath=Rail;
    C->FirstPersonCameraLocation=C->PlayerCamera->GetRelativeLocation();
    C->FirstPersonCameraRotation=C->PlayerCamera->GetRelativeRotation();
    using EState=UOBPlayerSplineComponent::EOBRailState;
    int32 Actions=0,Departures=0;
    C->OnActionPointReached.AddLambda([&](int32){++Actions;});
    C->OnTravelStarted.AddLambda([&](){++Departures;});
    auto Arrive=[&](float IncomingYaw) {
        C->CurrentPointIndex=0;C->TargetPointIndex=1;
        C->CurrentDistance=C->TargetDistance=Path->GetDistanceAlongSplineAtSplinePoint(1);
        C->RailState=EState::Moving;C->ApplySplineLocation(C->CurrentDistance);
        C->ApplyPlayerYaw(IncomingYaw);C->UpdateMovement(0);
    };
    auto Tick=[&](float Seconds){C->TickComponent(Seconds,LEVELTICK_All,&C->PrimaryComponentTick);};
    Arrive(0);
    const FVector StopLocation=Player->GetActorLocation();
    TestFalse(TEXT("Arriving does not activate combat before facing the zone"),C->IsAtActionPoint());
    Tick(.1f);
    TestTrue(TEXT("Entry turn interpolates instead of snapping"),Player->GetActorRotation().Yaw>0 && Player->GetActorRotation().Yaw<90);
    TestTrue(TEXT("Entry turn remains at the spline point"),Player->GetActorLocation().Equals(StopLocation));
    for (int32 I=0;I<5;++I) Tick(.1f);
    TestTrue(TEXT("Combat activates after the turn completes"),C->IsAtActionPoint());
    TestTrue(TEXT("Player and controller face the combat zone"),
        FMath::IsNearlyEqual(Player->GetActorRotation().Yaw,90.f,.01f) && FMath::IsNearlyEqual(PC->GetControlRotation().Yaw,90.f,.01f));
    TestEqual(TEXT("Combat entry fires exactly once"),Actions,1);
    TestTrue(TEXT("Combat uses the first-person camera"),C->PlayerCamera->GetRelativeLocation().Equals(C->FirstPersonCameraLocation));
    C->StartMoveToNextPoint();C->StartMoveToNextPoint();
    TestTrue(TEXT("Departure first turns in place"),C->RailState==EState::AligningForTravel);
    TestFalse(TEXT("Walk animation waits until departure alignment finishes"),C->IsMoving());
    Tick(.1f);
    TestTrue(TEXT("Return turn is gradual and keeps the player stationary"),
        Player->GetActorRotation().Yaw>0 && Player->GetActorRotation().Yaw<90 && Player->GetActorLocation().Equals(StopLocation));
    TestEqual(TEXT("Allies are not released during the return turn"),Departures,0);
    TestFalse(TEXT("Return begins blending toward the travel camera"),C->PlayerCamera->GetRelativeLocation().Equals(C->FirstPersonCameraLocation));
    for (int32 I=0;I<4;++I) Tick(.1f);
    TestTrue(TEXT("Travel starts after matching the spline heading"),C->IsMoving() && FMath::IsNearlyZero(Player->GetActorRotation().Yaw,.01f));
    TestEqual(TEXT("Departure releases allies once"),Departures,1);
    TestEqual(TEXT("Return alignment never re-enters combat"),Actions,1);
    Tick(.1f);
    TestTrue(TEXT("Player then moves forward along the existing spline"),Player->GetActorLocation().X>StopLocation.X);

    Stops.FindChecked(1).bOverrideFacingYaw=true;Stops.FindChecked(1).FacingYaw=-90;
    Arrive(0);Tick(.5f);
    TestTrue(TEXT("Authored FacingYaw overrides automatic zone focus"),C->IsAtActionPoint() && FMath::IsNearlyEqual(Player->GetActorRotation().Yaw,-90.f,.01f));
    Stops.FindChecked(1).bOverrideFacingYaw=false;
    Trigger->Spawners={Hostage,nullptr};
    TestFalse(TEXT("An empty combat zone has no target"),Trigger->GetCombatFacingLocation(Focus));
    Arrive(45);Tick(.1f);
    TestTrue(TEXT("No combat target preserves incoming heading"),C->IsAtActionPoint() && FMath::IsNearlyEqual(Player->GetActorRotation().Yaw,45.f,.01f));

    C->ApplyPlayerYaw(179);C->CurrentStopYaw=179;C->TargetStopRotation=FRotator(0,-179,0);
    C->RailState=EState::AligningForTravel;Tick(.01f);
    TestTrue(TEXT("Wraparound rotation takes the short path"),FMath::Abs(FMath::FindDeltaAngleDegrees(Player->GetActorRotation().Yaw,-179.f))<.3f);
    Rail->bExternallyControlled=true;
    C->RailState=EState::Action;C->CurrentPointIndex=1;C->ApplyPlayerYaw(90);
    C->StartMoveToNextPoint();
    TestTrue(TEXT("Training retains its director-controlled turns and immediate departure"),C->IsMoving());
    return !HasAnyErrors();
}
#endif

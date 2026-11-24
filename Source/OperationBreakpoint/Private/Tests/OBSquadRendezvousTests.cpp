#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "World/OBSquadRendezvous.h"
#include "World/OBRailPath.h"
#include "World/OBAllyRoute.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Character/OBAllyCharacter.h"
#include "Character/OBAllySplineComponent.h"
#include "Components/SplineComponent.h"
#include "Components/CapsuleComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBSquadRendezvousTest,"OperationBreakpoint.Infiltration.SquadRendezvous",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOBSquadRendezvousTest::RunTest(const FString&)
{
    auto* W=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
    auto* P=W->SpawnActor<AOBPlayerCharacter>();
    auto* Rail=W->SpawnActor<AOBRailPath>();
    auto* S=Rail->GetSpline();S->ClearSplinePoints(false);
    for (int I=0;I<3;++I) S->AddSplinePoint(FVector(I*100,0,0),ESplineCoordinateSpace::World,false);
    S->UpdateSpline();
    P->GetPlayerSplineComponent()->RailPath=Rail;
    P->GetPlayerSplineComponent()->PlayerCharacter=P;
    P->GetPlayerSplineComponent()->PlayerCamera=P->GetPlayerCamera();
    auto* Gate=W->SpawnActor<AOBSquadRendezvous>();Gate->bUseNavigation=false;Gate->bEnforceTravelOrder=false;Gate->PlayerRendezvousPoint=1;
    for (int I=0;I<2;++I)
    {
        FOBSquadRouteMember M;M.Ally=W->SpawnActor<AOBAllyCharacter>();M.Route=W->SpawnActor<AOBAllyRoute>();M.RendezvousPoint=1;
        auto* AS=M.Route->GetSpline();AS->ClearSplinePoints(false);
        for (int J=0;J<3;++J) AS->AddSplinePoint(FVector(J*200,I*100,0),ESplineCoordinateSpace::World,false);
        for (int J=0;J<3;++J) AS->SetSplinePointType(J,ESplinePointType::Linear,false);
        AS->UpdateSpline();M.Route->PointCues.Add(1).bWaitForSignal=true;
        M.Ally->GetRouteComponent()->bSweepMovement=false;Gate->Members.Add(M);
    }
    auto TickMember=[&](int I,float Seconds) {
        auto* C=Gate->Members[I].Ally->GetRouteComponent();C->TickComponent(Seconds,LEVELTICK_All,&C->PrimaryComponentTick);
    };
    Gate->Members[1].DepartureDelay=.5f;
    Gate->bDepartDuringNarration=true;
    Gate->DepartAfterZone=1;Gate->DepartFromPlayerPoint=7;
    TestTrue(TEXT("Arrival pre-stages both existing allies"),Gate->StageForPlayer(P));
    TestTrue(TEXT("Forward-staged sections retain the combat camera until the squad is ready"),Gate->IsHoldingPlayerDeparture(P));
    const FVector StagedPosition=Gate->Members[0].Ally->GetActorLocation();
    TickMember(0,2.f);TickMember(1,2.f);
    TestTrue(TEXT("Combat holds the squad at its green starts"),Gate->Members[0].Ally->GetActorLocation().Equals(StagedPosition)
        && Gate->Members[0].Ally->GetRouteComponent()->IsWaiting() && !Gate->IsSectionStarted());
    TestFalse(TEXT("A repeated arrival cannot restage the same squad"),Gate->StageForPlayer(P));
    TestTrue(TEXT("Departure releases the pre-staged section"),Gate->StartForPlayer(P));
    TestTrue(TEXT("Departure preserves the staged transform instead of teleporting again"),Gate->Members[0].Ally->GetActorLocation().Equals(StagedPosition));
    TickMember(0,.1f);
    TestTrue(TEXT("The staged section actually moves after departure"),Gate->Members[0].Ally->GetRouteComponent()->GetDistanceAlongRoute()>0.f);
    TickMember(1,.1f);
    TestEqual(TEXT("The second member holds its authored departure gap"),Gate->Members[1].Ally->GetRouteComponent()->GetDistanceAlongRoute(),0.f);
    static_cast<AActor*>(Gate)->Tick(.6f);TickMember(1,.1f);
    TestTrue(TEXT("The delayed member proceeds after the lead member"),Gate->Members[1].Ally->GetRouteComponent()->GetDistanceAlongRoute()>0.f);
    Gate->Members[1].DepartureDelay=0.f;
    TickMember(0,10.f);TickMember(1,10.f);
    TestFalse(TEXT("Both ready signal points release the player departure hold"),Gate->IsHoldingPlayerDeparture(P));
    for (int32 Scenario : {0,1,2})
    {
        Gate->bStaged=Gate->bStarted=Gate->bReleased=Gate->bPlayerArrived=false;
        TestTrue(TEXT("Valid section starts"),Gate->StartForPlayer(P));
        if (Scenario==0)
        {
            auto* C=P->GetPlayerSplineComponent();
            C->CurrentDistance=0;C->TargetDistance=S->GetSplineLength();C->TargetPointIndex=2;
            C->RailState=UOBPlayerSplineComponent::EOBRailState::Moving;
            C->MoveSpeed=5000.f;C->UpdateMovement(1.f);
            TestEqual(TEXT("Fast player movement cannot skip gate in a long frame"),C->GetDistanceAlongRoute(),S->GetDistanceAlongSplineAtSplinePoint(1));
            TestTrue(TEXT("Waiting at red marker does not activate combat"),C->IsMoving());
            TestFalse(TEXT("Player alone cannot release gate"),Gate->IsRendezvousReleased());
        }
        TickMember(0,10.f);
        TestTrue(TEXT("Fast ally waits at its exact route cue"),Gate->Members[0].Ally->GetRouteComponent()->IsWaitingAtSignalPoint(1));
        if (Scenario!=2) Gate->ConstrainPlayerDistance(P,5000.f);
        TestFalse(TEXT("Second ally is required"),Gate->IsRendezvousReleased());
        TickMember(1,10.f);
        TestTrue(TEXT("Late ally reaches its own cue"),Gate->Members[1].Ally->GetRouteComponent()->IsWaitingAtSignalPoint(1));
        if (Scenario==2) TestFalse(TEXT("Both fast allies keep waiting for the player"),Gate->HasPlayerArrived() || Gate->IsRendezvousReleased());
        Gate->ConstrainPlayerDistance(P,5000.f);
        TestTrue(TEXT("Player and both allies release together"),Gate->IsRendezvousReleased());
        TestEqual(TEXT("Released gate no longer constrains travel"),Gate->ConstrainPlayerDistance(P,150.f),150.f);
        TickMember(0,2.f);TickMember(1,2.f);
        TestTrue(TEXT("Both continue to blue endpoints"),Gate->Members[0].Ally->GetRouteComponent()->HasCompletedRoute()
            && Gate->Members[1].Ally->GetRouteComponent()->HasCompletedRoute());
    }
    Gate->bStarted=false;
    Gate->Members[1].Ally=Gate->Members[0].Ally;
    AddExpectedError(TEXT("Squad section rejected"),EAutomationExpectedErrorFlags::Contains,1);
    TestFalse(TEXT("Duplicate member configuration is rejected before route changes"),Gate->StartForPlayer(P));
    W->DestroyWorld(false);GEngine->DestroyWorldContext(W);
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBAllySpacingTest,"OperationBreakpoint.Infiltration.AllySpacing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOBAllySpacingTest::RunTest(const FString&)
{
    auto* W=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
    auto* Leader=W->SpawnActor<AOBAllyCharacter>();auto* Follower=W->SpawnActor<AOBAllyCharacter>();
    auto MakePath=[&](float Start) {
        auto* R=W->SpawnActor<AOBAllyRoute>();auto* S=R->GetSpline();S->ClearSplinePoints(false);
        S->AddSplinePoint(FVector(Start,0,0),ESplineCoordinateSpace::World,false);
        S->AddSplinePoint(FVector(2000,0,0),ESplineCoordinateSpace::World,false);
        S->SetSplinePointType(0,ESplinePointType::Linear,false);S->SetSplinePointType(1,ESplinePointType::Linear,false);S->UpdateSpline();return R;
    };
    auto* LC=Leader->GetRouteComponent();auto* FC=Follower->GetRouteComponent();
    LC->bSweepMovement=FC->bSweepMovement=false;
    FC->YieldToAlly=Leader;FC->FollowSpacing=140;FC->MoveSpeed=300;
    LC->StartRoute(MakePath(300));FC->StartRoute(MakePath(0));
    static_cast<UActorComponent*>(FC)->TickComponent(3.f,LEVELTICK_All,&FC->PrimaryComponentTick);
    TestTrue(TEXT("A long frame cannot pass through the leader"),FC->GetDistanceAlongRoute()==0.f && !FC->IsBlocked());
    for (int32 Frame=0;Frame<180;++Frame)
    {
        static_cast<UActorComponent*>(LC)->TickComponent(1.f/60,LEVELTICK_All,&LC->PrimaryComponentTick);
        static_cast<UActorComponent*>(FC)->TickComponent(1.f/60,LEVELTICK_All,&FC->PrimaryComponentTick);
        if (!TestTrue(TEXT("Single-file travel preserves body and weapon clearance"),FVector::Dist2D(Leader->GetActorLocation(),Follower->GetActorLocation())>=139.9f)) break;
    }
    TestTrue(TEXT("Yielding follows a moving leader without an obstacle failure"),FC->GetDistanceAlongRoute()>100.f && !FC->IsBlocked());
    W->DestroyWorld(false);GEngine->DestroyWorldContext(W);return !HasAnyErrors();
}
#endif

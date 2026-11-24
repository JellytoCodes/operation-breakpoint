#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Character/OBAllyCharacter.h"
#include "Character/OBAllyNavigationComponent.h"
#include "World/OBSquadRendezvous.h"
#include "World/OBRailPath.h"
#include "Components/SplineComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBAllyNavigationOrderTest,"OperationBreakpoint.Infiltration.NavSquadOrder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOBAllyNavigationOrderTest::RunTest(const FString&)
{
    auto* W=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
    auto* P=W->SpawnActor<AOBPlayerCharacter>();auto* Rail=W->SpawnActor<AOBRailPath>();
    auto* S=Rail->GetSpline();S->ClearSplinePoints(false);
    S->AddSplinePoint(FVector(0,0,0),ESplineCoordinateSpace::World,false);
    S->AddSplinePoint(FVector(1000,0,0),ESplineCoordinateSpace::World,false);S->UpdateSpline();
    auto* C=P->GetPlayerSplineComponent();C->RailPath=Rail;C->PlayerCharacter=P;C->PlayerCamera=P->GetPlayerCamera();
    C->CurrentDistance=0;C->TargetDistance=S->GetSplineLength();C->TargetPointIndex=1;
    C->RailState=UOBPlayerSplineComponent::EOBRailState::Moving;C->MoveSpeed=100000;
    auto* Section=W->SpawnActor<AOBSquadRendezvous>();Section->Player=P;Section->bStarted=true;
    Section->PlayerSectionStart=0;Section->PlayerSectionEnd=S->GetSplineLength();
    for (int32 I=0;I<2;++I)
    {
        auto& M=Section->Members.AddDefaulted_GetRef();M.Ally=W->SpawnActor<AOBAllyCharacter>();
        auto* N=M.Ally->GetNavigationComponent();N->OwnerSection=Section;N->InitialPathLength=I ? 2000.f : 1000.f;
    }
    auto* One=Section->Members[0].Ally->GetNavigationComponent();auto* Two=Section->Members[1].Ally->GetNavigationComponent();
    C->UpdateMovement(10.f);
    TestEqual(TEXT("Player cannot depart while both navigation leaders are waiting"),C->GetDistanceAlongRoute(),0.f);
    Section->Members[0].Ally->SetActorLocation(FVector(800,0,88));Section->Members[1].Ally->SetActorLocation(FVector(400,0,88));
    One->Progress=.8f;Two->Progress=.4f;C->UpdateMovement(10.f);
    const float Followed=C->GetDistanceAlongRoute();
    TestTrue(TEXT("A fast player stays behind Jindo 2 and Jindo 1 on different-length nav paths"),Followed>0.f && Followed<.4f*S->GetSplineLength());
    C->UpdateMovement(10.f);
    TestEqual(TEXT("A blocked lead pair also holds the player on a long frame"),C->GetDistanceAlongRoute(),Followed);
    Section->Members[0].Ally->SetActorLocation(FVector(270,0,88));Two->Progress=.7f;C->UpdateMovement(10.f);
    TestEqual(TEXT("A shortcut cannot let the player pass Jindo 1's real position"),C->GetDistanceAlongRoute(),Followed);
    Section->Members[0].Ally->SetActorLocation(FVector(800,0,88));Two->Progress=.4f;
    One->Progress=.1f;C->UpdateMovement(10.f);
    TestEqual(TEXT("The player also checks Jindo 1 directly when it replans or falls behind"),C->GetDistanceAlongRoute(),Followed);
    One->Progress=1;One->bArrived=true;Two->Progress=.9f;C->UpdateMovement(10.f);
    TestTrue(TEXT("Jindo 1 arriving does not let the player pass unfinished Jindo 2"),C->GetDistanceAlongRoute()<S->GetSplineLength());
    Two->Progress=1;Two->bArrived=true;
    TestEqual(TEXT("The player may finish only after both navigation leaders arrive"),Section->ConstrainPlayerDistance(P,S->GetSplineLength()),S->GetSplineLength());
    const float SecondLimit=UOBAllyNavigationComponent::AllowedProgress(.5f,1000.f,180.f,false);
    TestTrue(TEXT("The second member has a positive gap behind its moving leader"),SecondLimit>=0 && SecondLimit<.5f);
    TestEqual(TEXT("A stopped leader keeps the same follower limit"),UOBAllyNavigationComponent::AllowedProgress(.5f,1000.f,180.f,false),SecondLimit);
    W->DestroyWorld(false);GEngine->DestroyWorldContext(W);return !HasAnyErrors();
}
#endif

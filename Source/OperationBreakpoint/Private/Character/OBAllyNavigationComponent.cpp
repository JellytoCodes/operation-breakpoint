#include "Character/OBAllyNavigationComponent.h"
#include "Character/OBAllyCharacter.h"
#include "Character/OBAllySplineComponent.h"
#include "AI/OBAllyAIController.h"
#include "World/OBSquadRendezvous.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Navigation/PathFollowingComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "World/OBBulletActor.h"

DEFINE_LOG_CATEGORY_STATIC(LogOBAllyNav,Log,All);
UOBAllyNavigationComponent::UOBAllyNavigationComponent() { PrimaryComponentTick.bCanEverTick=true; }
bool UOBAllyNavigationComponent::BelongsTo(const AOBSquadRendezvous* Section) const { return OwnerSection.Get()==Section; }
float UOBAllyNavigationComponent::AllowedProgress(float P,float Length,float Gap,bool bFinished)
{
    return bFinished ? 1.f : FMath::Clamp(P-FMath::Max(0.f,Gap)/FMath::Max(1.f,Length),0.f,1.f);
}
bool UOBAllyNavigationComponent::Prepare(AActor* Start,AActor* End,AOBSquadRendezvous* Section)
{
    auto* Ally=Cast<AOBAllyCharacter>(GetOwner());
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if (!Ally || !Nav || !IsValid(Start) || !IsValid(End) || Start->GetWorld()!=GetWorld() || End->GetWorld()!=GetWorld()) return false;
    FNavLocation From,To;
    // Ship decks are stacked. Never project a marker onto the floor above it.
    const FVector Extent(100,100,100);
    // The stair landing has a central pillar; its side gaps fit a torso, not
    // the original wide capsule. Keep an 8 cm navigation margin around the
    // physical torso so smoothed corners clear leaning pallets and their ends.
    Ally->GetCapsuleComponent()->SetCapsuleRadius(18.f,true);
    Ally->GetCharacterMovement()->SetUpdateNavAgentWithOwnersCollisions(true);
    Ally->GetCharacterMovement()->UpdateNavAgent(*Ally->GetCapsuleComponent());
    Ally->GetCharacterMovement()->GetNavAgentPropertiesRef().AgentRadius=26.f;
    Ally->GetCharacterMovement()->SetUpdateNavAgentWithOwnersCollisions(false);
    const auto& Agent=Ally->GetCharacterMovement()->GetNavAgentPropertiesRef();
    if (!Nav->ProjectPointToNavigation(Start->GetActorLocation(),From,Extent,&Agent)
        || !Nav->ProjectPointToNavigation(End->GetActorLocation(),To,Extent,&Agent))
    { UE_LOG(LogOBAllyNav,Warning,TEXT("Nav marker is outside NavMesh | Ally=%s Start=%s End=%s"),*Ally->GetActorNameOrLabel(),*Start->GetActorNameOrLabel(),*End->GetActorNameOrLabel());return false; }
    StopNavigation();Ally->GetRouteComponent()->StopRoute();
    auto* AI=Cast<AOBAllyAIController>(Ally->GetController());
    if (!AI)
    {
        if (auto* Old=Ally->GetController()) Old->UnPossess();
        AI=GetWorld()->SpawnActor<AOBAllyAIController>();
        if (!AI) return false;
        AI->Possess(Ally);
    }
    Controller=AI;OwnerSection=Section;Goal=To.Location;
    auto* Movement=Ally->GetCharacterMovement();
    Movement->PrimaryComponentTick.AddPrerequisite(this,PrimaryComponentTick);
    Movement->StopMovementImmediately();Movement->SetMovementMode(MOVE_Walking);
    Movement->SetWalkableFloorAngle(60.f); // Match the Squad NavMesh on the ship's steep stairs.
    Movement->bOrientRotationToMovement=true;Movement->bUseControllerDesiredRotation=false;
    Movement->SetAvoidanceEnabled(true);Movement->AvoidanceConsiderationRadius=400.f;
    Ally->bUseControllerRotationYaw=false;
    Ally->SetActorLocation(From.Location+FVector(0,0,Ally->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3),false,nullptr,ETeleportType::TeleportPhysics);
    Ally->SetActorHiddenInGame(false);
    InitialPathLength=0;Progress=0;RetryRemaining=0;bPrepared=true;bArrived=bPathFailure=bStagePaused=false;
    Movement->MaxWalkSpeed=0;
    return true;
}
void UOBAllyNavigationComponent::BeginNavigation(UOBAllyNavigationComponent* Leader,float Headway)
{
    if (!bPrepared) return;
    PrecedingMember=Leader;HeadwayDistance=FMath::Max(0.f,Headway);bActive=true;RetryRemaining=0;
    RequestPath();
}
bool UOBAllyNavigationComponent::RequestPath()
{
    auto* AI=Controller.Get();auto* Ally=Cast<AOBAllyCharacter>(GetOwner());
    if (!AI || !Ally) return false;
    FAIMoveRequest Request(Goal);Request.SetUsePathfinding(true);Request.SetAllowPartialPath(false);
    Request.SetNavigationFilter(UOBAllyNavigationFilter::StaticClass());
    Request.SetProjectGoalLocation(false);Request.SetRequireNavigableEndLocation(true);
    Request.SetAcceptanceRadius(AcceptanceRadius);Request.SetReachTestIncludesAgentRadius(false);Request.SetCanStrafe(false);
    const auto Result=AI->MoveTo(Request);
    const bool Failed=Result.Code==EPathFollowingRequestResult::Failed;
    if (Failed && !bPathFailure) UE_LOG(LogOBAllyNav,Warning,TEXT("No complete NavMesh path | Ally=%s Goal=%s"),*Ally->GetActorNameOrLabel(),*Goal.ToString());
    bPathFailure=Failed;
    if (Result.Code==EPathFollowingRequestResult::AlreadyAtGoal && (!PrecedingMember.IsValid() || PrecedingMember->HasArrived())) { bArrived=true;Progress=1.f; }
    if (InitialPathLength<=0.f && !bPathFailure)
        InitialPathLength=FMath::Max(1.f,RemainingDistance());
    RetryRemaining=.5f;
    return !bPathFailure;
}
float UOBAllyNavigationComponent::RemainingDistance() const
{
    const auto* AI=Controller.Get();const auto* Ally=Cast<AOBAllyCharacter>(GetOwner());
    const auto* Follow=AI ? AI->GetPathFollowingComponent() : nullptr;
    const auto Path=Follow ? Follow->GetPath() : FNavPathSharedPtr();
    if (!Path.IsValid() || !Ally) return InitialPathLength*(1.f-Progress);
    const auto& Points=Path->GetPathPoints();
    if (Points.IsEmpty()) return InitialPathLength;
    const int32 Next=FMath::Clamp(int32(Follow->GetNextPathIndex()),0,Points.Num()-1);
    float Remaining=FVector::Distance(Ally->GetNavAgentLocation(),Points[Next].Location);
    for (int32 I=Next+1;I<Points.Num();++I) Remaining+=FVector::Distance(Points[I-1].Location,Points[I].Location);
    return Remaining;
}
void UOBAllyNavigationComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Function)
{
    Super::TickComponent(Dt,Type,Function);
    auto* Ally=Cast<AOBAllyCharacter>(GetOwner());auto* AI=Controller.Get();
    if (!bActive || !Ally || !AI) return;
    auto* Movement=Ally->GetCharacterMovement();
    for (TActorIterator<APawn> It(GetWorld());It;++It)
        if (*It!=Ally) Ally->GetCapsuleComponent()->IgnoreActorWhenMoving(*It,true);
    for (TActorIterator<AOBBulletActor> It(GetWorld());It;++It) Ally->GetCapsuleComponent()->IgnoreActorWhenMoving(*It,true);
    if (FVector::Distance(Ally->GetNavAgentLocation(),Goal)<=AcceptanceRadius+5.f
        && (!PrecedingMember.IsValid() || PrecedingMember->HasArrived()))
    { bArrived=true;Progress=1.f;AI->StopMovement();Movement->StopMovementImmediately();Movement->MaxWalkSpeed=0;return; }
    if (InitialPathLength>0.f)
        Progress=FMath::Max(Progress,FMath::Clamp(1.f-RemainingDistance()/InitialPathLength,0.f,.9999f));
    RetryRemaining-=Dt;
    if (InitialPathLength<=0.f && RetryRemaining<=0.f) RequestPath();
    float Allowed=1.f;
    if (auto* Leader=PrecedingMember.Get()) Allowed=AllowedProgress(Leader->GetProgress(),FMath::Min(Leader->GetPathLength(),InitialPathLength),HeadwayDistance,Leader->HasArrived());
    float Budget=FMath::Max(0.f,Allowed-Progress)*InitialPathLength;
    // A shorter avoidance route must not put Jindo 2 in front of Jindo 1.
    // When both use the same corridor, pace against the leader's real position
    // projected onto this path as well as their overall section progress.
    if (const auto* Leader=PrecedingMember.Get(); Leader && !Leader->HasArrived())
    {
        const auto Path=AI->GetPathFollowingComponent()->GetPath();
        const auto* Ahead=Cast<AOBAllyCharacter>(Leader->GetOwner());
        if (Path.IsValid() && Ahead)
        {
            const auto& Points=Path->GetPathPoints();float Best=FMath::Square(250.f),LeaderRemaining=0.f,Tail=0.f;bool Near=false;
            for (int32 I=Points.Num()-1;I>0;--I)
            {
                const FVector Closest=FMath::ClosestPointOnSegment(Ahead->GetNavAgentLocation(),Points[I-1].Location,Points[I].Location);
                const float Distance=FVector::DistSquared(Ahead->GetNavAgentLocation(),Closest);
                if (Distance<Best) { Best=Distance;LeaderRemaining=Tail+FVector::Distance(Closest,Points[I].Location);Near=true; }
                Tail+=FVector::Distance(Points[I-1].Location,Points[I].Location);
            }
            if (Near) Budget=FMath::Min(Budget,FMath::Max(0.f,RemainingDistance()-LeaderRemaining-HeadwayDistance));
        }
    }
    if (bStagePaused || (PrecedingMember.IsValid() && Budget<=.1f))
    { Movement->StopMovementImmediately();Movement->MaxWalkSpeed=0;return; }
    if (AI->GetMoveStatus()==EPathFollowingStatus::Idle && RetryRemaining<=0.f) RequestPath();
    const auto* Section=OwnerSection.Get();
    const float Speed=Section && Section->MoveSpeedLimit>0 ? FMath::Min(MoveSpeed,Section->MoveSpeedLimit) : MoveSpeed;
    Movement->MaxWalkSpeed=PrecedingMember.IsValid() ? FMath::Min(Speed,Budget/FMath::Max(Dt,.001f)) : Speed;
}
void UOBAllyNavigationComponent::StopNavigation()
{
    if (auto* AI=Controller.Get()) AI->StopMovement();
    if (auto* Ally=Cast<AOBAllyCharacter>(GetOwner())) Ally->GetCharacterMovement()->StopMovementImmediately();
    bActive=bPrepared=false;PrecedingMember.Reset();OwnerSection.Reset();
}
void UOBAllyNavigationComponent::EndPlay(const EEndPlayReason::Type Reason) { StopNavigation();Super::EndPlay(Reason); }

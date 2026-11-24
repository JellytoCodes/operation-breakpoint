#include "World/OBSquadRendezvous.h"
#include "World/OBAllyRoute.h"
#include "World/OBRailPath.h"
#include "Character/OBAllyCharacter.h"
#include "Character/OBAllySplineComponent.h"
#include "Character/OBAllyNavigationComponent.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Stage/OBStageFlowComponent.h"
#include "Components/SplineComponent.h"
#include "Components/SceneComponent.h"
#include "Components/BoxComponent.h"
#include "GameFramework/PlayerController.h"

DEFINE_LOG_CATEGORY_STATIC(LogOBSquad,Log,All);
AOBSquadRendezvous::AOBSquadRendezvous()
{
    PrimaryActorTick.bCanEverTick=true;
    auto* Marker=CreateDefaultSubobject<UBoxComponent>(TEXT("RendezvousMarker"));
    Marker->SetBoxExtent(FVector(65,100,100));
    Marker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Marker->SetCanEverAffectNavigation(false);
    Marker->ShapeColor=FColor::Red;
    SetRootComponent(Marker);
}
void AOBSquadRendezvous::BeginPlay()
{
    Super::BeginPlay();
    if (!bEnabled) { SetActorTickEnabled(false);return; }
    TryBindPlayer();
}
void AOBSquadRendezvous::TryBindPlayer()
{
    if (Player.IsValid()) return;
    auto* PC=GetWorld()->GetFirstPlayerController();
    auto* P=PC ? Cast<AOBPlayerCharacter>(PC->GetPawn()) : nullptr;
    if (!P) return;
    Player=P;
    DepartureHandle=P->GetPlayerSplineComponent()->OnTravelStarted.AddUObject(this,&ThisClass::HandleDeparture);
    PointHandle=P->GetPlayerSplineComponent()->OnSplinePointReached.AddUObject(this,&ThisClass::HandlePlayerPoint);
    ArrivalHandle=P->GetPlayerSplineComponent()->OnActionPointReached.AddUObject(this,&ThisClass::HandleArrival);
    // Initial rail travel can begin before level actors receive BeginPlay.
    if (!bStartOnSplinePoint && DepartAfterZone==0 && P->GetPlayerSplineComponent()->IsMoving()) HandleDeparture();
}
void AOBSquadRendezvous::HandleArrival(int32 Point)
{
    if (bUseNavigation || !bStageOnArrival || bStartOnSplinePoint || DepartAfterZone<1 || DepartAfterZone>5
        || Point!=DepartFromPlayerPoint || bStaged || bStarted) return;
    auto* P=Player.Get();
    const auto* Stage=P ? P->GetStageFlowComponent() : nullptr;
    if (Stage && Stage->IsStageActive() && !P->GetPlayerSplineComponent()->IsRoutePreview()) StageForPlayer(P);
}
void AOBSquadRendezvous::HandlePlayerPoint(int32 Point)
{
    if (bStartOnSplinePoint && Point==DepartFromPlayerPoint) HandleDeparture();
}
void AOBSquadRendezvous::HandleDeparture()
{
    auto* P=Player.Get();
    auto* Stage=P ? P->GetStageFlowComponent() : nullptr;
    if (!bEnabled || bStarted || !Stage || !Stage->IsStageActive() || Stage->GetCurrentZone()!=DepartAfterZone
        || Stage->GetPhase()!=EOBStagePhase::Travel || P->GetPlayerSplineComponent()->IsRoutePreview()) return;
    if (!bStartOnSplinePoint && DepartFromPlayerPoint!=INDEX_NONE && P->GetPlayerSplineComponent()->GetCurrentPointIndex()!=DepartFromPlayerPoint) return;
    if (bStartOnSplinePoint)
    {
        auto* Rail=P->GetPlayerSplineComponent()->GetRailPath();
        if (!Rail || P->GetPlayerSplineComponent()->GetDistanceAlongRoute()+KINDA_SMALL_NUMBER
            <Rail->GetSpline()->GetDistanceAlongSplineAtSplinePoint(DepartFromPlayerPoint)) return;
    }
    StartForPlayer(P);
}
bool AOBSquadRendezvous::ValidateSection(AOBPlayerCharacter* P) const
{
    if (!IsValid(P) || P->GetWorld()!=GetWorld() || Members.IsEmpty()) return false;
    auto* Rail=P->GetPlayerSplineComponent()->GetRailPath();
    if (bUseNavigation)
    {
        TSet<AOBAllyCharacter*> Seen;
        for (const auto& M : Members)
        {
            if (!IsValid(M.Ally) || !IsValid(M.StartPoint) || !IsValid(M.EndPoint) || Seen.Contains(M.Ally)
                || M.Ally->GetWorld()!=GetWorld() || M.StartPoint->GetWorld()!=GetWorld() || M.EndPoint->GetWorld()!=GetWorld()) return false;
            Seen.Add(M.Ally);
        }
        return Rail!=nullptr;
    }
    if (PlayerRendezvousPoint!=INDEX_NONE && (!Rail || PlayerRendezvousPoint<1
        || PlayerRendezvousPoint>=Rail->GetSpline()->GetNumberOfSplinePoints())) return false;
    TSet<AOBAllyCharacter*> Seen;
    for (const auto& M : Members)
    {
        if (!IsValid(M.Ally) || !IsValid(M.Route) || M.Ally->GetWorld()!=GetWorld() || M.Route->GetWorld()!=GetWorld()
            || Seen.Contains(M.Ally) || M.Route->GetSpline()->IsClosedLoop()
            || M.Route->GetSpline()->GetNumberOfSplinePoints()<2 || M.Route->GetSpline()->GetSplineLength()<=KINDA_SMALL_NUMBER) return false;
        Seen.Add(M.Ally);
        if (PlayerRendezvousPoint!=INDEX_NONE)
        {
            const auto* Cue=M.Route->PointCues.Find(M.RendezvousPoint);
            if (M.RendezvousPoint<1 || M.RendezvousPoint>=M.Route->GetSpline()->GetNumberOfSplinePoints()
                || !Cue || !Cue->bWaitForSignal) return false;
        }
    }
    return true;
}
bool AOBSquadRendezvous::StartForPlayer(AOBPlayerCharacter* P)
{
    if (!bEnabled || bStarted || !ValidateSection(P))
    {
        UE_LOG(LogOBSquad,Warning,TEXT("Squad section rejected | Section=%s Zone=%d"),*GetName(),DepartAfterZone);
        return false;
    }
    Player=P;
    PlayerSectionStart=P->GetPlayerSplineComponent()->GetDistanceAlongRoute();
    if (auto* Rail=P->GetPlayerSplineComponent()->GetRailPath(); Rail && DepartFromPlayerPoint>=0
        && DepartFromPlayerPoint<Rail->GetSpline()->GetNumberOfSplinePoints())
        PlayerSectionStart=Rail->GetSpline()->GetDistanceAlongSplineAtSplinePoint(DepartFromPlayerPoint);
    if (bUseNavigation)
    {
        if (!bStaged && !StageForPlayer(P)) return false;
        const auto* Rail=P->GetPlayerSplineComponent()->GetRailPath();
        const int32 End=PlayerEndPoint!=INDEX_NONE ? PlayerEndPoint : Rail->GetNextStopPointIndex(DepartFromPlayerPoint);
        if (End<0 || End>=Rail->GetSpline()->GetNumberOfSplinePoints()) return false;
        PlayerSectionEnd=Rail->GetSpline()->GetDistanceAlongSplineAtSplinePoint(End);
        UOBAllyNavigationComponent* Leader=nullptr;
        for (const auto& M : Members)
        {
            auto* Nav=M.Ally->GetNavigationComponent();
            Nav->BeginNavigation(bEnforceTravelOrder ? Leader : nullptr,TravelHeadway);Leader=Nav;
        }
        bStarted=true;bReleased=true;
        UE_LOG(LogOBSquad,Display,TEXT("Nav squad started | Section=%s Zone=%d Members=%d"),*GetName(),DepartAfterZone,Members.Num());
        return true;
    }
    for (const auto& M : Members)
    {
        auto* C=M.Ally->GetRouteComponent();
        if (bStaged && C->GetCurrentRoute()==M.Route) C->ResumeRoute();
        else C->StartRoute(M.Route);
        C->SetTravelOrderSection(this);
        if (M.DepartureDelay>0.f) { C->PauseRoute();PendingDepartures.Add(M.Ally); }
        M.Ally->SetActorHiddenInGame(false);
    }
    DepartureElapsed=0.f;
    bStarted=true;bPlayerArrived=false;bReleased=PlayerRendezvousPoint==INDEX_NONE;
    UE_LOG(LogOBSquad,Display,TEXT("Squad section started | Section=%s AfterZone=%d Gate=%d Members=%d"),*GetName(),DepartAfterZone,PlayerRendezvousPoint,Members.Num());
    return true;
}
bool AOBSquadRendezvous::StageForPlayer(AOBPlayerCharacter* P)
{
    if (!bEnabled || bStaged || bStarted || !ValidateSection(P)) return false;
    Player=P;
    if (bUseNavigation)
    {
        for (const auto& M : Members)
            if (!M.Ally->GetNavigationComponent()->Prepare(M.StartPoint,M.EndPoint,this)) return false;
        bStaged=true;
        UE_LOG(LogOBSquad,Display,TEXT("Nav squad staged | Section=%s Zone=%d"),*GetName(),DepartAfterZone);
        return true;
    }
    for (const auto& M : Members)
    {
        auto* C=M.Ally->GetRouteComponent();
        C->StartRoute(M.Route);C->PauseRoute();
        M.Ally->SetActorHiddenInGame(false);
    }
    bStaged=true;
    UE_LOG(LogOBSquad,Display,TEXT("Squad staged at zone arrival | Section=%s Zone=%d Point=%d"),*GetName(),DepartAfterZone,DepartFromPlayerPoint);
    return true;
}
void AOBSquadRendezvous::GetMemberSegment(int32 Index,float& Start,float& End) const
{
    const auto& M=Members[Index];
    const auto* Spline=M.Route->GetSpline();
    Start=0.f;End=Spline->GetSplineLength();
    if (PlayerRendezvousPoint!=INDEX_NONE && M.RendezvousPoint>=0 && M.RendezvousPoint<Spline->GetNumberOfSplinePoints())
    {
        const float Cue=Spline->GetDistanceAlongSplineAtSplinePoint(M.RendezvousPoint);
        if (bReleased) Start=Cue;else End=Cue;
    }
}
float AOBSquadRendezvous::LimitBehindMember(int32 Index,float Proposed,float OwnStart,float OwnEnd,float OwnCurrent) const
{
    const auto& M=Members[Index];
    const auto* Leader=IsValid(M.Ally) ? M.Ally->GetRouteComponent() : nullptr;
    if (!Leader || !IsValid(M.Route) || Leader->GetCurrentRoute()!=M.Route) return OwnCurrent;
    float Start,End;GetMemberSegment(Index,Start,End);
    const float Length=End-Start;
    const float Progress=Length>KINDA_SMALL_NUMBER ? FMath::Clamp((Leader->GetDistanceAlongRoute()-Start)/Length,0.f,1.f) : 1.f;
    // At a signal or endpoint followers may close the gap, but never pass it.
    // This also lets arbitrarily different route lengths share one rendezvous.
    const float Headway=Progress>=1.f-KINDA_SMALL_NUMBER ? 0.f : FMath::Max(0.f,TravelHeadway)/FMath::Max(Length,1.f);
    const float Limit=OwnStart+FMath::Clamp(Progress-Headway,0.f,1.f)*FMath::Max(0.f,OwnEnd-OwnStart);
    return FMath::Min(Proposed,FMath::Max(OwnCurrent,Limit));
}
float AOBSquadRendezvous::ConstrainAllyDistance(const UOBAllySplineComponent* Follower,float Proposed) const
{
    if (!bEnabled || !bEnforceTravelOrder || !bStarted) return Proposed;
    for (int32 Index=1;Index<Members.Num();++Index)
    {
        const auto& M=Members[Index];
        if (M.Ally && M.Ally->GetRouteComponent()==Follower && Follower->GetCurrentRoute()==M.Route)
        {
            float Start,End;GetMemberSegment(Index,Start,End);
            return LimitBehindMember(Index-1,Proposed,Start,End,Follower->GetDistanceAlongRoute());
        }
    }
    return Proposed;
}
float AOBSquadRendezvous::ConstrainPlayerDistance(AOBPlayerCharacter* P,float Proposed)
{
    if (bUseNavigation)
    {
        if (!bEnabled || P!=Player.Get() || Members.IsEmpty() || !bEnforceTravelOrder) return Proposed;
        const auto* Spline=P->GetPlayerSplineComponent();
        if (!bStarted)
        {
            const auto* Stage=P->GetStageFlowComponent();
            return Stage->IsStageActive() && Stage->GetCurrentZone()==DepartAfterZone && Spline->GetCurrentPointIndex()==DepartFromPlayerPoint
                ? Spline->GetDistanceAlongRoute() : Proposed;
        }
        float Allowed=1.f;
        for (const auto& M : Members)
        {
            const auto* Nav=M.Ally->GetNavigationComponent();
            if (!Nav->BelongsTo(this)) return Proposed;
            Allowed=FMath::Min(Allowed,UOBAllyNavigationComponent::AllowedProgress(Nav->GetProgress(),FMath::Min(Nav->GetPathLength(),PlayerSectionEnd-PlayerSectionStart),TravelHeadway,Nav->HasArrived()));
        }
        float Limit=PlayerSectionStart+Allowed*FMath::Max(0.f,PlayerSectionEnd-PlayerSectionStart);
        // Different NavMesh routes can have very different lengths. In a shared
        // corridor also check the actual leaders' positions on the player's rail.
        if (const auto* Path=Spline->GetRailPath())
            for (const auto& M : Members)
            {
                if (M.Ally->GetNavigationComponent()->HasArrived()) continue;
                const FVector Feet=M.Ally->GetNavAgentLocation();const auto* Line=Path->GetSpline();
                const float Key=Line->FindInputKeyClosestToWorldLocation(Feet);
                const float Distance=FMath::Clamp(Line->GetDistanceAlongSplineAtSplineInputKey(Key),PlayerSectionStart,PlayerSectionEnd);
                if (FVector::DistSquared(Feet,Line->GetLocationAtDistanceAlongSpline(Distance,ESplineCoordinateSpace::World))<FMath::Square(250.f))
                    Limit=FMath::Min(Limit,FMath::Max(PlayerSectionStart,Distance-TravelHeadway));
            }
        return FMath::Min(Proposed,FMath::Max(Spline->GetDistanceAlongRoute(),Limit));
    }
    if (!bEnabled || !bStarted || P!=Player.Get()) return Proposed;
    const auto* Stage=P->GetStageFlowComponent();
    if (Stage->IsStageActive() && Stage->GetPhase()!=EOBStagePhase::Travel) return Proposed;
    auto* Rail=P->GetPlayerSplineComponent()->GetRailPath();
    if (!Rail) return Proposed;
    if (bEnforceTravelOrder && !Members.IsEmpty())
    {
        const auto& Last=Members.Last();
        // A later section can reuse the same actors. Retire the earlier limit.
        if (!Last.Ally || Last.Ally->GetRouteComponent()->GetCurrentRoute()!=Last.Route) return Proposed;
        float Start=PlayerSectionStart,End=P->GetPlayerSplineComponent()->GetTravelTargetDistance();
        if (PlayerRendezvousPoint!=INDEX_NONE)
        {
            const float Cue=Rail->GetSpline()->GetDistanceAlongSplineAtSplinePoint(PlayerRendezvousPoint);
            if (bReleased) Start=Cue;else End=Cue;
        }
        if (End>Start+KINDA_SMALL_NUMBER)
            Proposed=LimitBehindMember(Members.Num()-1,Proposed,Start,End,P->GetPlayerSplineComponent()->GetDistanceAlongRoute());
    }
    if (bReleased) return Proposed;
    const float Gate=Rail->GetSpline()->GetDistanceAlongSplineAtSplinePoint(PlayerRendezvousPoint);
    if (Proposed+KINDA_SMALL_NUMBER<Gate) return Proposed;
    bPlayerArrived=true;
    TryRelease();
    // Always clamp the first arriving frame to the exact marker, even after a hitch.
    return Gate;
}
void AOBSquadRendezvous::TryRelease()
{
    if (!bStarted || bReleased || !bPlayerArrived) return;
    for (const auto& M : Members)
    {
        const auto* C=IsValid(M.Ally) ? M.Ally->GetRouteComponent() : nullptr;
        if (!C || C->GetCurrentRoute()!=M.Route || !C->IsWaitingAtSignalPoint(M.RendezvousPoint)) return;
    }
    for (const auto& M : Members) M.Ally->GetRouteComponent()->ResumeRoute();
    bReleased=true;
    UE_LOG(LogOBSquad,Display,TEXT("Squad rendezvous released | Section=%s AfterZone=%d PlayerPoint=%d"),*GetName(),DepartAfterZone,PlayerRendezvousPoint);
}
bool AOBSquadRendezvous::IsHoldingPlayerDeparture(AOBPlayerCharacter* P) const
{
    if (bUseNavigation)
    {
        if (!bEnabled || !bDepartDuringNarration || !bStaged || P!=Player.Get() || !bEnforceTravelOrder) return false;
        if (!bStarted || Members.IsEmpty()) return true;
        const auto* Last=Members.Last().Ally->GetNavigationComponent();
        return Last->BelongsTo(this) && UOBAllyNavigationComponent::AllowedProgress(Last->GetProgress(),Last->GetPathLength(),TravelHeadway,Last->HasArrived())<=KINDA_SMALL_NUMBER;
    }
    if (!bEnabled || !bDepartDuringNarration || !bStaged || bReleased || P!=Player.Get()) return false;
    if (!bStarted) return true;
    for (const auto& M : Members)
    {
        const auto* Route=M.Ally ? M.Ally->GetRouteComponent() : nullptr;
        if (!Route || Route->GetCurrentRoute()!=M.Route || !Route->IsWaitingAtSignalPoint(M.RendezvousPoint)) return true;
    }
    return false;
}
void AOBSquadRendezvous::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    TryBindPlayer();
    const auto* P=Player.Get();
    const auto* Stage=P ? P->GetStageFlowComponent() : nullptr;
    // OnTravelStarted is emitted by the player rail only after departure dialogue.
    // Preparing or starting here during AwaitingDeparture bypasses that gate.
    if (!bUseNavigation && bStarted && (!Stage || !Stage->IsStageActive() || (Stage->GetCurrentZone()==DepartAfterZone
        && (Stage->GetPhase()==EOBStagePhase::Travel || Stage->GetPhase()==EOBStagePhase::AwaitingDeparture
            || (DepartAfterZone==6 && (Stage->GetPhase()==EOBStagePhase::Zone6Intro || Stage->GetPhase()==EOBStagePhase::Combat))))))
    {
        DepartureElapsed+=DeltaSeconds;
        for (const auto& M : Members)
            if (PendingDepartures.Contains(M.Ally) && DepartureElapsed>=M.DepartureDelay)
            {
                auto* Route=M.Ally->GetRouteComponent();
                if (Route->GetCurrentRoute()==M.Route) Route->ResumeRoute();
                PendingDepartures.Remove(M.Ally);
            }
    }
    if (Stage && Stage->GetPhase()==EOBStagePhase::Travel) TryRelease();
    if (bUseNavigation && bStarted)
        for (const auto& M : Members)
        {
            auto* Nav=M.Ally->GetNavigationComponent();
            if (bUseArrivalRotation && Nav->BelongsTo(this) && Nav->HasArrived() && IsValid(M.EndPoint))
                M.Ally->SetActorRotation(FRotator(0,M.EndPoint->GetActorRotation().Yaw,0));
            if (Nav->BelongsTo(this)) Nav->SetStagePaused(Stage && Stage->IsStageActive()
                && (Stage->GetPhase()==EOBStagePhase::Opening || Stage->GetPhase()==EOBStagePhase::AwaitingDeparture || Stage->GetPhase()==EOBStagePhase::Ending
                    || Stage->GetPhase()==EOBStagePhase::Complete || Stage->GetPhase()==EOBStagePhase::QA || Stage->GetPhase()==EOBStagePhase::ShortcutTransition));
        }
}
void AOBSquadRendezvous::EndPlay(const EEndPlayReason::Type Reason)
{
    if (auto* P=Player.Get())
    {
        P->GetPlayerSplineComponent()->OnTravelStarted.Remove(DepartureHandle);
        P->GetPlayerSplineComponent()->OnSplinePointReached.Remove(PointHandle);
        P->GetPlayerSplineComponent()->OnActionPointReached.Remove(ArrivalHandle);
    }
    Player.Reset();DepartureHandle.Reset();
    Super::EndPlay(Reason);
}

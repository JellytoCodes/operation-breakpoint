#include "Character/OBAllySplineComponent.h"
#include "Character/OBAllyCharacter.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBActionComponent.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Stage/OBStageFlowComponent.h"
#include "World/OBAllyRoute.h"
#include "World/OBBulletActor.h"
#include "World/OBSquadRendezvous.h"
#include "EngineUtils.h"
#include "Components/CapsuleComponent.h"
#include "Components/SplineComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"

DEFINE_LOG_CATEGORY_STATIC(LogOBAllyRoute,Log,All);
UOBAllySplineComponent::UOBAllySplineComponent() { PrimaryComponentTick.bCanEverTick=true; }
void UOBAllySplineComponent::SetTravelOrderSection(AOBSquadRendezvous* Section) { TravelOrderSection=Section; }
void UOBAllySplineComponent::BeginPlay() { Super::BeginPlay(); if (bAutoStart) StartAssignedRoute(); }
void UOBAllySplineComponent::EndPlay(const EEndPlayReason::Type Reason) { StopRoute(); Super::EndPlay(Reason); }
bool UOBAllySplineComponent::IsValidRoute(AOBAllyRoute* Route,int32 StartPoint) const
{
    return IsValid(Route) && Route->GetWorld()==GetWorld() && Route->GetSpline()->GetNumberOfSplinePoints()>1
        && !Route->GetSpline()->IsClosedLoop() && StartPoint>=0 && StartPoint<Route->GetSpline()->GetNumberOfSplinePoints()-1
        && Route->GetSpline()->GetSplineLength()>KINDA_SMALL_NUMBER;
}
bool UOBAllySplineComponent::StartAssignedRoute() { return StartRoute(AssignedRoute); }
FVector UOBAllySplineComponent::PointFeet(int32 Point) const
{
    return CurrentRoute->GetSpline()->GetLocationAtSplinePoint(Point,ESplineCoordinateSpace::World);
}
bool UOBAllySplineComponent::StartRoute(AOBAllyRoute* Route,int32 StartPoint,bool bSnapToStart)
{
    auto* Ally=Cast<AOBAllyCharacter>(GetOwner());
    if (!Ally || !IsValidRoute(Route,StartPoint))
    { UE_LOG(LogOBAllyRoute,Warning,TEXT("Cannot start ally route: invalid actor, route or point | Ally=%s"),*GetNameSafe(GetOwner())); return false; }
    const FVector StartFeet=Route->GetSpline()->GetLocationAtSplinePoint(StartPoint,ESplineCoordinateSpace::World);
    const FVector CurrentFeet=Ally->GetActorLocation()-FVector(0,0,Ally->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+GroundClearance);
    if (!bSnapToStart && FVector::Distance(StartFeet,CurrentFeet)>FMath::Max(0.f,MaxBranchJoinDistance))
    { UE_LOG(LogOBAllyRoute,Warning,TEXT("Ally route join rejected: connect spline endpoints within %.0f cm | Ally=%s"),MaxBranchJoinDistance,*Ally->GetName()); return false; }
    ClearPlayerDepartureWait();
    TravelOrderSection.Reset();
    if (!bActive) PreviousMovementMode=Ally->GetCharacterMovement()->MovementMode;
    Ally->GetCharacterMovement()->StopMovementImmediately(); Ally->GetCharacterMovement()->DisableMovement();
    Ally->UnCrouch();
    CurrentRoute=Route; Distance=Route->GetSpline()->GetDistanceAlongSplineAtSplinePoint(StartPoint); NextPoint=StartPoint+1;
    PendingBranch=nullptr; WaitRemaining=0; Velocity=FVector::ZeroVector;
    bActive=true; bCompleted=bManualPause=bWaitingForSignal=bSignalQueued=bAtPoint=bBlocked=false; bJoining=!bSnapToStart;
    ++RouteRevision;
    if (bSnapToStart)
    {
        FVector Location=StartFeet; Location.Z+=Ally->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+GroundClearance;
        Ally->SetActorLocationAndRotation(Location,FRotator(0,Route->GetSpline()->GetDirectionAtSplinePoint(StartPoint,ESplineCoordinateSpace::World).Rotation().Yaw,0),false,nullptr,ETeleportType::TeleportPhysics);
    }
    UE_LOG(LogOBAllyRoute,Display,TEXT("Ally route started | Ally=%s Route=%s Point=%d"),*Ally->GetName(),*Route->GetName(),StartPoint);
    return true;
}
bool UOBAllySplineComponent::StageRouteForPlayerDeparture(AOBAllyRoute* Route,AOBPlayerCharacter* Player,int32 StartPoint)
{
    auto* Spline=IsValid(Player) ? Player->GetPlayerSplineComponent() : nullptr;
    if (!Spline || Player->GetWorld()!=GetWorld()) return false;
    if (!StartRoute(Route,StartPoint,true)) return false;
    DepartureSpline=Spline;
    bWaitingForPlayerDeparture=true;
    DepartureHandle=Spline->OnTravelStarted.AddUObject(this,&ThisClass::HandlePlayerTravelStarted);
    UE_LOG(LogOBAllyRoute,Display,TEXT("Ally staged for player departure | Ally=%s Route=%s Point=%d"),*GetNameSafe(GetOwner()),*GetNameSafe(Route),StartPoint);
    return true;
}
void UOBAllySplineComponent::ClearPlayerDepartureWait()
{
    if (auto* Spline=DepartureSpline.Get()) Spline->OnTravelStarted.Remove(DepartureHandle);
    DepartureHandle.Reset(); DepartureSpline.Reset(); bWaitingForPlayerDeparture=false;
}
void UOBAllySplineComponent::HandlePlayerTravelStarted()
{
    ClearPlayerDepartureWait();
    UE_LOG(LogOBAllyRoute,Display,TEXT("Staged ally released with player | Ally=%s"),*GetNameSafe(GetOwner()));
}
void UOBAllySplineComponent::PauseRoute() { bManualPause=true; Velocity=FVector::ZeroVector; }
void UOBAllySplineComponent::ResumeRoute()
{
    // A player trigger can be crossed before the squad reaches its signal gate.
    if (bActive && !bWaitingForSignal && !bManualPause) bSignalQueued=true;
    bManualPause=bWaitingForSignal=false;
}
void UOBAllySplineComponent::StopRoute()
{
    ClearPlayerDepartureWait();
    if (bActive)
        if (auto* Ally=Cast<AOBAllyCharacter>(GetOwner()))
            Ally->GetCharacterMovement()->SetMovementMode(static_cast<EMovementMode>(PreviousMovementMode));
    bActive=false; Velocity=FVector::ZeroVector; ++RouteRevision;
}
bool UOBAllySplineComponent::QueueBranch(AOBAllyRoute* Route,int32 StartPoint)
{
    if (!bActive || !IsValidRoute(Route,StartPoint)) return false;
    PendingBranch=Route; PendingStartPoint=StartPoint;
    return true;
}
bool UOBAllySplineComponent::LeavePoint()
{
    const int32 Arrived=NextPoint-1;
    const auto* Cue=CurrentRoute->PointCues.Find(Arrived);
    AOBAllyRoute* Branch=PendingBranch ? PendingBranch.Get() : Cue ? Cue->BranchRoute.Get() : nullptr;
    const int32 Start=PendingBranch ? PendingStartPoint : Cue ? Cue->BranchStartPoint : 0;
    if (Branch)
    {
        if (StartRoute(Branch,Start,false)) return true;
        // Remain stopped at the junction; never teleport through a malformed branch.
        bWaitingForSignal=true; return false;
    }
    if (NextPoint>=CurrentRoute->GetSpline()->GetNumberOfSplinePoints())
    {
        StopRoute(); bCompleted=true;
        OnRouteFinished.Broadcast(Cast<AOBAllyCharacter>(GetOwner())); return false;
    }
    bAtPoint=false; return true;
}
bool UOBAllySplineComponent::HasTravelClearance(const FVector& Feet) const
{
    // The ordered section owns pacing. World-distance yielding can deadlock
    // unrelated curves when a follower stands beside an earlier part of a loop.
    if (auto* Order=TravelOrderSection.Get(); Order && Order->bEnforceTravelOrder) return true;
    const auto* Ally=CastChecked<AOBAllyCharacter>(GetOwner());
    FVector Target=Feet;Target.Z+=Ally->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+GroundClearance;
    const FVector Before=Ally->GetActorLocation();
    auto CanPass=[&](const AActor* Other,float Spacing) {
        if (!IsValid(Other) || Other==Ally || Other->IsHidden() || Spacing<=0.f
            || FMath::Abs(Other->GetActorLocation().Z-Target.Z)>150.f) return true;
        const FVector2D Center(Other->GetActorLocation()),From(Before),To(Target);
        // Let a pawn already inside the clearance move out. Test the entire step
        // so a long frame cannot jump through a teammate and appear beyond it.
        if (FVector2D::DistSquared(From,Center)<FMath::Square(Spacing)
            && FVector2D::DistSquared(To,Center)>FVector2D::DistSquared(From,Center)) return true;
        const FVector Closest=FMath::ClosestPointOnSegment(FVector(Center,0),FVector(From,0),FVector(To,0));
        return FVector2D::DistSquared(FVector2D(Closest),Center)>=FMath::Square(Spacing);
    };
    if (!CanPass(YieldToAlly,FollowSpacing)) return false;
    const auto* PC=GetWorld()->GetFirstPlayerController();
    return !bYieldToPlayer || !PC || CanPass(PC->GetPawn(),PlayerClearance);
}
bool UOBAllySplineComponent::MoveCharacter(const FVector& Feet,float Yaw)
{
    auto* Ally=CastChecked<AOBAllyCharacter>(GetOwner());
    FVector Location=Feet;
    auto* Movement=Ally->GetCharacterMovement();
    const float StepHeight=Movement->MaxStepHeight;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(OBAllySplineFloor),false,Ally);
    auto* Capsule=Ally->GetCapsuleComponent();
    // Encounters can spawn midway through a route. Ignore the whole pawn,
    // including weapon/ragdoll components whose object type is not Pawn.
    for (TActorIterator<APawn> It(GetWorld());It;++It)
        if (*It!=Ally) { Capsule->IgnoreActorWhenMoving(*It,true);Query.AddIgnoredActor(*It); }
    for (TActorIterator<AOBBulletActor> It(GetWorld());It;++It)
        { Capsule->IgnoreActorWhenMoving(*It,true);Query.AddIgnoredActor(*It); }
    Query.bTraceComplex=Capsule->bTraceComplexOnMove;
    const FVector Lift(0,0,Capsule->GetScaledCapsuleHalfHeight());
    FHitResult Floor;
    // Probe the whole footprint: a centre ray drops too early at descending
    // stair treads while the capsule's rear still rests on the higher tread.
    const bool bFloor=bSweepMovement && GetWorld()->SweepSingleByChannel(Floor,Feet+Lift+FVector(0,0,StepHeight),
        Feet+Lift-FVector(0,0,StepHeight),FQuat::Identity,Capsule->GetCollisionObjectType(),
        FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),Capsule->GetScaledCapsuleHalfHeight()),Query,
        FCollisionResponseParams(Capsule->GetCollisionResponseToChannels()))
        && !Floor.bStartPenetrating && Floor.ImpactNormal.Z>=Movement->GetWalkableFloorZ();
    Location.Z=bFloor ? Floor.Location.Z+GroundClearance : Feet.Z+Lift.Z+GroundClearance;
    const FVector Before=Ally->GetActorLocation();
    FHitResult Hit;
    FString StepDiagnostic;
    Ally->SetActorLocationAndRotation(Location,FRotator(0,Yaw,0),bSweepMovement,&Hit);
    if (bSweepMovement && Hit.bBlockingHit && !Ally->GetActorLocation().Equals(Location,.1f))
    {
        // Use the engine's bounded stair solver. Landing on a tread may leave
        // the capsule slightly above the authored slope; XY must stay on route.
        FVector Remaining=Location-Ally->GetActorLocation();Remaining.Z=0;
        FStepDownResult StepDown;
        if (Movement->StepUp(FVector(0,0,-1),Remaining,Hit,&StepDown)
            && FVector::DistSquared2D(Ally->GetActorLocation(),Location)<.01f
            && FMath::Abs(Ally->GetActorLocation().Z-Location.Z)<=StepHeight+GroundClearance) Hit=FHitResult();
        if (Hit.bBlockingHit && Hit.GetComponent() && Hit.GetComponent()->CanCharacterStepUp(Ally)
            && Hit.ImpactPoint.Z<=Before.Z-Lift.Z+StepHeight+GroundClearance)
        {
            // Authored stair slopes can put the requested feet below the next
            // tread after a signal wait. Resolve that tread with three bounded
            // capsule sweeps instead of treating a walkable step as a wall.
            Ally->SetActorLocation(Before,false,nullptr,ETeleportType::TeleportPhysics);
            const FVector Raised=Before+FVector(0,0,StepHeight+GroundClearance);
            FHitResult Up,Across,Down;
            Ally->SetActorLocation(Raised,true,&Up);
            if (!Up.bBlockingHit)
            {
                Ally->SetActorLocation(FVector(Location.X,Location.Y,Raised.Z),true,&Across);
                if (!Across.bBlockingHit)
                {
                    Ally->SetActorLocation(FVector(Location.X,Location.Y,Before.Z-StepHeight),true,&Down);
                    if (Down.bBlockingHit && !Down.bStartPenetrating && Down.ImpactNormal.Z>=Movement->GetWalkableFloorZ()
                        && Ally->GetActorLocation().Z-Before.Z<=StepHeight+GroundClearance
                        && FVector::DistSquared2D(Ally->GetActorLocation(),Location)<.01f) Hit=FHitResult();
                }
            }
            StepDiagnostic=FString::Printf(TEXT("Up=%d Across=%d Down=%d DownNormal=%.3f From=%s Resolved=%s"),Up.bBlockingHit,Across.bBlockingHit,Down.bBlockingHit,Down.ImpactNormal.Z,*Before.ToString(),*Ally->GetActorLocation().ToString());
        }
        if (Hit.bBlockingHit && Hit.ImpactNormal.Z>.1f
            && Hit.ImpactPoint.Z<=Before.Z-Lift.Z+GroundClearance)
        {
            // Triangle seams can touch just below the capsule base, where
            // StepUp deliberately refuses a step. Clear only this tiny floor lip.
            Ally->SetActorLocation(Before,false,nullptr,ETeleportType::TeleportPhysics);
            FVector Raised=Location;Raised.Z=FMath::Max(Before.Z,Location.Z)+FMath::Min(4.f,StepHeight);
            FHitResult LipHit;
            Ally->SetActorLocation(Raised,true,&LipHit);
            if (!LipHit.bBlockingHit && Ally->GetActorLocation().Equals(Raised,.1f)) Hit=FHitResult();
        }
        if (Hit.bBlockingHit) Ally->SetActorLocation(Before,false,nullptr,ETeleportType::TeleportPhysics);
    }
    const bool bNowBlocked=bSweepMovement && Hit.bBlockingHit && !Ally->GetActorLocation().Equals(Location,.1f);
    if (bNowBlocked && !bBlocked) UE_LOG(LogOBAllyRoute,Warning,TEXT("Ally spline blocked; fix route clearance | Ally=%s Obstacle=%s Component=%s Normal=%s Impact=%s Target=%s CanStep=%d %s"),*Ally->GetName(),Hit.GetActor() ? *Hit.GetActor()->GetActorNameOrLabel() : TEXT("none"),*GetNameSafe(Hit.GetComponent()),*Hit.ImpactNormal.ToString(),*Hit.ImpactPoint.ToString(),*Location.ToString(),Hit.GetComponent() && Hit.GetComponent()->CanCharacterStepUp(Ally),*StepDiagnostic);
    bBlocked=bNowBlocked; return !bBlocked;
}
void UOBAllySplineComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaTime,TickType,TickFunction);
    Velocity=FVector::ZeroVector;
    auto* Ally=Cast<AOBAllyCharacter>(GetOwner());
    if (!bActive || !Ally) return;
    if (!IsValid(CurrentRoute)) { StopRoute(); return; }
    if (bWaitingForPlayerDeparture)
    {
        auto* Spline=DepartureSpline.Get();
        if (!Spline) { StopRoute(); return; }
        // Preview never enters combat or emits a subsequent travel start.
        if (Spline->IsRoutePreview() && Spline->IsMoving()) ClearPlayerDepartureWait();
        else return;
    }
    auto* PC=GetWorld()->GetFirstPlayerController();
    const auto* Player=PC ? Cast<AOBPlayerCharacter>(PC->GetPawn()) : nullptr;
    if (const auto* Stage=Player ? Player->GetStageFlowComponent() : nullptr; Stage && Stage->IsStageActive())
    {
        // Preserve the current route/cues while the player's departure narration holds the squad.
        const auto Phase=Stage->GetPhase();
        if (Phase==EOBStagePhase::Opening || Phase==EOBStagePhase::AwaitingDeparture
            || Phase==EOBStagePhase::Zone6Dialogue || Phase==EOBStagePhase::Zone6Breach
            || Phase==EOBStagePhase::Ending || Phase==EOBStagePhase::Complete || Phase==EOBStagePhase::QA) return;
    }
    if (bManualPause || (bPauseDuringPlayerAction && Player && Player->GetActionComponent()->GetActionState()==EOBActionState::Action)) return;
    const FVector Before=Ally->GetActorLocation();
    float Remaining=FMath::Max(0.f,DeltaTime);
    for (int32 Iteration=0; Iteration<64 && bActive && Remaining>SMALL_NUMBER; ++Iteration)
    {
        if (bAtPoint)
        {
            const float Consumed=FMath::Min(WaitRemaining,Remaining); WaitRemaining-=Consumed; Remaining-=Consumed;
            if (WaitRemaining>SMALL_NUMBER || bWaitingForSignal || Remaining<=SMALL_NUMBER) break;
            if (!LeavePoint()) break;
        }
        auto* Spline=CurrentRoute->GetSpline();
        const float Speed=FMath::Max(1.f,MoveSpeed);
        if (bJoining)
        {
            const FVector Target=PointFeet(NextPoint-1);
            const FVector Feet=Ally->GetActorLocation()-FVector(0,0,Ally->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+GroundClearance);
            const float Gap=FVector::Distance(Feet,Target);
            const float Step=FMath::Min(Gap,Speed*Remaining);
            const FVector Move=Gap>KINDA_SMALL_NUMBER ? (Target-Feet)*(Step/Gap) : FVector::ZeroVector;
            if (!HasTravelClearance(Feet+Move)) break;
            if (!MoveCharacter(Feet+Move,Move.IsNearlyZero() ? Ally->GetActorRotation().Yaw : Move.Rotation().Yaw)) break;
            Remaining-=Step/Speed;
            if (Step+KINDA_SMALL_NUMBER<Gap) break;
            bJoining=false;
        }
        const float End=Spline->GetDistanceAlongSplineAtSplinePoint(NextPoint);
        float TargetDistance=FMath::Min(End,Distance+Speed*Remaining);
        if (auto* Order=TravelOrderSection.Get()) TargetDistance=Order->ConstrainAllyDistance(this,TargetDistance);
        if (TargetDistance<=Distance+KINDA_SMALL_NUMBER && End>Distance+KINDA_SMALL_NUMBER) break;
        const FVector Target=Spline->GetLocationAtDistanceAlongSpline(TargetDistance,ESplineCoordinateSpace::World);
        if (!HasTravelClearance(Target)) break;
        if (!MoveCharacter(Target,Spline->GetDirectionAtDistanceAlongSpline(TargetDistance,ESplineCoordinateSpace::World).Rotation().Yaw)) break;
        Remaining-=(TargetDistance-Distance)/Speed; Distance=TargetDistance;
        if (Distance+KINDA_SMALL_NUMBER<End) break;
        const int32 Arrived=NextPoint++;
        bAtPoint=true;
        const FOBAllyRouteCue* Cue=CurrentRoute->PointCues.Find(Arrived);
        WaitRemaining=Cue ? FMath::Max(0.f,Cue->WaitSeconds) : 0.f;
        bWaitingForSignal=Cue && Cue->bWaitForSignal && !bSignalQueued;
        if (Cue && Cue->bWaitForSignal) bSignalQueued=false;
        const int32 Revision=RouteRevision;
        OnPointCue.Broadcast(Ally,CurrentRoute,Cue ? Cue->EventName : NAME_None);
        if (Revision!=RouteRevision || bManualPause) break;
    }
    if (DeltaTime>SMALL_NUMBER) Velocity=(Ally->GetActorLocation()-Before)/DeltaTime;
}

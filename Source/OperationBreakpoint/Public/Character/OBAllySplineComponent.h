#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "OBAllySplineComponent.generated.h"
class AOBAllyRoute;
class AOBAllyCharacter;
class AOBPlayerCharacter;
class UOBPlayerSplineComponent;
class AOBSquadRendezvous;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOBAllyPointCue, AOBAllyCharacter*, Ally, AOBAllyRoute*, Route, FName, EventName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOBAllyRouteFinished, AOBAllyCharacter*, Ally);

UCLASS(ClassGroup=(OB), meta=(BlueprintSpawnableComponent))
class OPERATIONBREAKPOINT_API UOBAllySplineComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UOBAllySplineComponent();
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="OB|Ally|Route") TObjectPtr<AOBAllyRoute> AssignedRoute;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Ally|Route") bool bAutoStart=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Ally|Route", meta=(ClampMin="1.0", Units="cm/s")) float MoveSpeed=500.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Ally|Route", meta=(ClampMin="0.0", Units="cm")) float GroundClearance=2.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Ally|Route") bool bSweepMovement=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Ally|Route") bool bPauseDuringPlayerAction=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Ally|Route", meta=(ClampMin="0.0", Units="cm")) float MaxBranchJoinDistance=200.f;
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="OB|Ally|Route") TObjectPtr<AOBAllyCharacter> YieldToAlly;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Ally|Route", meta=(ClampMin="0.0", Units="cm")) float FollowSpacing=120.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Ally|Route") bool bYieldToPlayer=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Ally|Route") bool bMoveDuringDeparture=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Ally|Route", meta=(ClampMin="0.0", Units="cm")) float PlayerClearance=160.f;
    UFUNCTION(BlueprintCallable, Category="OB|Ally|Route") bool StartAssignedRoute();
    UFUNCTION(BlueprintCallable, Category="OB|Ally|Route") bool StartRoute(AOBAllyRoute* Route, int32 StartPoint=0, bool bSnapToStart=true);
    // Teleport to a later route, then wait for the player's next actual travel start.
    UFUNCTION(BlueprintCallable, Category="OB|Ally|Route") bool StageRouteForPlayerDeparture(AOBAllyRoute* Route, AOBPlayerCharacter* Player, int32 StartPoint=0);
    UFUNCTION(BlueprintCallable, Category="OB|Ally|Route") void PauseRoute();
    UFUNCTION(BlueprintCallable, Category="OB|Ally|Route") void ResumeRoute();
    UFUNCTION(BlueprintCallable, Category="OB|Ally|Route") void StopRoute();
    UFUNCTION(BlueprintCallable, Category="OB|Ally|Route") bool QueueBranch(AOBAllyRoute* Route, int32 StartPoint=0);
    UFUNCTION(BlueprintPure, Category="OB|Ally|Route") bool HasRouteControl() const { return bActive; }
    UFUNCTION(BlueprintPure, Category="OB|Ally|Route") bool IsWaiting() const { return bWaitingForPlayerDeparture || bManualPause || bWaitingForSignal || WaitRemaining>0.f; }
    UFUNCTION(BlueprintPure, Category="OB|Ally|Route") bool IsWaitingForPlayerDeparture() const { return bWaitingForPlayerDeparture; }
    UFUNCTION(BlueprintPure, Category="OB|Ally|Route") bool IsBlocked() const { return bBlocked; }
    bool IsWaitingAtSignalPoint(int32 Point) const { return bActive && bAtPoint && bWaitingForSignal && NextPoint-1==Point; }
    UFUNCTION(BlueprintPure, Category="OB|Ally|Route") bool HasCompletedRoute() const { return bCompleted; }
    UFUNCTION(BlueprintPure, Category="OB|Ally|Route") AOBAllyRoute* GetCurrentRoute() const { return CurrentRoute; }
    UFUNCTION(BlueprintPure, Category="OB|Ally|Route") float GetDistanceAlongRoute() const { return Distance; }
    FVector GetRouteVelocity() const { return Velocity; }
    void SetTravelOrderSection(AOBSquadRendezvous* Section);
    UPROPERTY(BlueprintAssignable, Category="OB|Ally|Route") FOBAllyPointCue OnPointCue;
    UPROPERTY(BlueprintAssignable, Category="OB|Ally|Route") FOBAllyRouteFinished OnRouteFinished;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
private:
    friend class FOBAllyRouteTest;
    friend class FOBAllyRedeployTest;
    friend class FOBSquadRendezvousTest;
    void ClearPlayerDepartureWait();
    void HandlePlayerTravelStarted();
    bool IsValidRoute(AOBAllyRoute* Route,int32 StartPoint) const;
    bool MoveCharacter(const FVector& Feet,float Yaw);
    bool HasTravelClearance(const FVector& Feet) const;
    bool LeavePoint();
    FVector PointFeet(int32 Point) const;
    UPROPERTY(Transient) TObjectPtr<AOBAllyRoute> CurrentRoute;
    UPROPERTY(Transient) TObjectPtr<AOBAllyRoute> PendingBranch;
    TWeakObjectPtr<UOBPlayerSplineComponent> DepartureSpline;
    TWeakObjectPtr<AOBSquadRendezvous> TravelOrderSection;
    FDelegateHandle DepartureHandle;
    bool bWaitingForPlayerDeparture=false;
    int32 PendingStartPoint=0, NextPoint=1, RouteRevision=0;
    float Distance=0, WaitRemaining=0;
    FVector Velocity=FVector::ZeroVector;
    uint8 PreviousMovementMode=0;
    bool bActive=false, bCompleted=false, bManualPause=false, bWaitingForSignal=false, bSignalQueued=false, bAtPoint=false, bJoining=false, bBlocked=false;
};

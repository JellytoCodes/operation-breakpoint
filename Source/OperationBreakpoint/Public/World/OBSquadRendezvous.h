#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OBSquadRendezvous.generated.h"

class AOBAllyCharacter;
class AOBAllyRoute;
class AOBPlayerCharacter;
class UOBAllySplineComponent;

USTRUCT(BlueprintType)
struct OPERATIONBREAKPOINT_API FOBSquadRouteMember
{
    GENERATED_BODY()
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="Squad") TObjectPtr<AOBAllyCharacter> Ally;
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="Squad") TObjectPtr<AOBAllyRoute> Route;
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="Navigation") TObjectPtr<AActor> StartPoint;
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="Navigation") TObjectPtr<AActor> EndPoint;
    // A signal-wait cue on this route. -1 means this section has no rendezvous.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Squad") int32 RendezvousPoint=INDEX_NONE;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Squad", meta=(ClampMin="0.0",Units="s")) float DepartureDelay=0.f;
};

// One authored section of the Stage_01 squad route. The player and every member
// must reach their own spline gate before anyone continues past the red marker.
UCLASS()
class OPERATIONBREAKPOINT_API AOBSquadRendezvous : public AActor
{
    GENERATED_BODY()
public:
    AOBSquadRendezvous();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Squad") bool bEnabled=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Squad|Navigation") bool bUseNavigation=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Squad|Navigation") int32 PlayerEndPoint=INDEX_NONE;
    // A waiting pose authored on the destination marker, independent of the last path tangent.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Squad|Navigation") bool bUseArrivalRotation=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Squad|Navigation",meta=(ClampMin="0",Units="cm/s")) float MoveSpeedLimit=0.f;
    // StageFlow zone at departure: 0 initial, 1..5 after combat, 6 after the breach.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Squad", meta=(ClampMin="0",ClampMax="6")) int32 DepartAfterZone=0;
    // Optional extra departure in the same zone, e.g. point 85 after the breach.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Squad") int32 DepartFromPlayerPoint=INDEX_NONE;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Squad") bool bStartOnSplinePoint=false;
    // Occupy the green start as soon as this combat stop is reached, then hold
    // there until its departure. Later travel must not teleport the squad again.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Squad") bool bStageOnArrival=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Squad") bool bDepartDuringNarration=false;
    // Members[0] = Jindo 1, Members[1] = Jindo 2, then the player (Jindo 3).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Squad|Travel Order") bool bEnforceTravelOrder=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Squad|Travel Order", meta=(ClampMin="0.0",Units="cm")) float TravelHeadway=180.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Squad") int32 PlayerRendezvousPoint=INDEX_NONE;
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="OB|Squad") TArray<FOBSquadRouteMember> Members;
    bool StartForPlayer(AOBPlayerCharacter* InPlayer);
    bool StageForPlayer(AOBPlayerCharacter* InPlayer);
    float ConstrainPlayerDistance(AOBPlayerCharacter* InPlayer,float ProposedDistance);
    float ConstrainAllyDistance(const UOBAllySplineComponent* Follower,float ProposedDistance) const;
    bool IsSectionStarted() const { return bStarted; }
    bool IsSectionStaged() const { return bStaged; }
    bool IsHoldingPlayerDeparture(AOBPlayerCharacter* InPlayer) const;
    bool IsRendezvousReleased() const { return bReleased; }
    bool HasPlayerArrived() const { return bPlayerArrived; }
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Tick(float DeltaSeconds) override;
private:
    friend class FOBSquadRendezvousTest;
    friend class FOBAllyNavigationOrderTest;
    friend class FOBNavSquadMapSmoke;
    void TryBindPlayer();
    void HandleDeparture();
    void HandleArrival(int32 Point);
    void HandlePlayerPoint(int32 Point);
    void TryRelease();
    bool ValidateSection(AOBPlayerCharacter* InPlayer) const;
    void GetMemberSegment(int32 Index,float& Start,float& End) const;
    float LimitBehindMember(int32 LeaderIndex,float Proposed,float OwnStart,float OwnEnd,float OwnCurrent) const;
    TWeakObjectPtr<AOBPlayerCharacter> Player;
    FDelegateHandle DepartureHandle;
    FDelegateHandle PointHandle;
    FDelegateHandle ArrivalHandle;
    bool bStaged=false,bStarted=false,bPlayerArrived=false,bReleased=false;
    float DepartureElapsed=0.f;
    float PlayerSectionStart=0.f;
    float PlayerSectionEnd=0.f;
    TSet<AOBAllyCharacter*> PendingDepartures;
};

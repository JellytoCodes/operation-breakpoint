#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "OBAllyNavigationComponent.generated.h"

class AOBAllyAIController;
class AOBSquadRendezvous;

// AI path following owns movement and obstacle avoidance. This component only
// stages endpoints, requests paths, and paces the next member in the convoy.
UCLASS(ClassGroup=(OB), meta=(BlueprintSpawnableComponent))
class OPERATIONBREAKPOINT_API UOBAllyNavigationComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UOBAllyNavigationComponent();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Ally|Navigation") float MoveSpeed=500.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Ally|Navigation") float AcceptanceRadius=30.f;
    bool Prepare(AActor* StartPoint,AActor* EndPoint,AOBSquadRendezvous* Section);
    void BeginNavigation(UOBAllyNavigationComponent* Leader,float Headway);
    void StopNavigation();
    void SetStagePaused(bool bPaused) { bStagePaused=bPaused; }
    float GetProgress() const { return Progress; }
    float GetPathLength() const { return InitialPathLength; }
    bool HasArrived() const { return bArrived; }
    bool IsActive() const { return bActive; }
    bool HasPathFailure() const { return bPathFailure; }
    bool BelongsTo(const AOBSquadRendezvous* Section) const;
    static float AllowedProgress(float LeaderProgress,float LeaderPathLength,float Headway,bool bLeaderArrived);
protected:
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* Function) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    friend class FOBAllyNavigationOrderTest;
    bool RequestPath();
    float RemainingDistance() const;
    TWeakObjectPtr<AOBAllyAIController> Controller;
    TWeakObjectPtr<AOBSquadRendezvous> OwnerSection;
    TWeakObjectPtr<UOBAllyNavigationComponent> PrecedingMember;
    FVector Goal=FVector::ZeroVector;
    float InitialPathLength=0.f,Progress=0.f,HeadwayDistance=180.f,RetryRemaining=0.f;
    bool bPrepared=false,bActive=false,bArrived=false,bPathFailure=false,bStagePaused=false;
};

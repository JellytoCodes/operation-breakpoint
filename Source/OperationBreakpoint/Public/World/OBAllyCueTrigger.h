#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OBAllyCueTrigger.generated.h"
class AOBAllyCharacter;
class AOBAllyRoute;
class AOBPlayerCharacter;
class UBoxComponent;
class UPrimitiveComponent;
UENUM(BlueprintType)
enum class EOBAllyCueCommand : uint8 { Start, Pause, Resume, Branch, StageForPlayerDeparture };
UENUM(BlueprintType)
enum class EOBAllyCueActivation : uint8 { PlayerOverlap, PlayerSplinePoint };
USTRUCT(BlueprintType)
struct OPERATIONBREAKPOINT_API FOBAllyCueEntry
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cue") TObjectPtr<AOBAllyCharacter> Ally;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cue") EOBAllyCueCommand Command=EOBAllyCueCommand::Start;
    // Start: optional override. Branch / StageForPlayerDeparture: required destination route.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cue") TObjectPtr<AOBAllyRoute> Route;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cue", meta=(ClampMin="0")) int32 StartPoint=0;
};
UCLASS()
class OPERATIONBREAKPOINT_API AOBAllyCueTrigger : public AActor
{
    GENERATED_BODY()
public:
    AOBAllyCueTrigger();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Ally|Cue") EOBAllyCueActivation Activation=EOBAllyCueActivation::PlayerOverlap;
    // Zero-based player spline point index, independent of the trigger box location.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Ally|Cue", meta=(ClampMin="0", EditCondition="Activation == EOBAllyCueActivation::PlayerSplinePoint")) int32 PlayerPointIndex=5;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Ally|Cue") TArray<FOBAllyCueEntry> Commands;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Ally|Cue") bool bTriggerOnce=true;
    UFUNCTION(BlueprintCallable, Category="OB|Ally|Cue") bool ActivateForPlayer(AOBPlayerCharacter* Player);
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick(float DeltaSeconds) override;
private:
    friend class FOBAllyRedeployTest;
    void TryBindPlayer();
    void HandlePlayerSplinePointReached(int32 PointIndex);
    UFUNCTION() void HandleOverlap(UPrimitiveComponent* Overlapped,AActor* Actor,UPrimitiveComponent* Other,int32 BodyIndex,bool bFromSweep,const FHitResult& Hit);
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="OB|Ally|Cue", meta=(AllowPrivateAccess="true")) TObjectPtr<UBoxComponent> TriggerBox;
    bool bTriggered=false;
    TWeakObjectPtr<AOBPlayerCharacter> BoundPlayer;
    FDelegateHandle PlayerPointHandle;
};

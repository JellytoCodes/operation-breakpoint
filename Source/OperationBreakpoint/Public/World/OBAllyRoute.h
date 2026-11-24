#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OBAllyRoute.generated.h"
class AOBAllyRoute;
class USplineComponent;

USTRUCT(BlueprintType)
struct OPERATIONBREAKPOINT_API FOBAllyRouteCue
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cue") FName EventName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cue", meta=(ClampMin="0.0", Units="s")) float WaitSeconds = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cue") bool bWaitForSignal = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cue") TObjectPtr<AOBAllyRoute> BranchRoute;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cue", meta=(ClampMin="0")) int32 BranchStartPoint = 0;
};

// Deliberately independent of OB Rail Path: player rail discovery must never select an ally route.
UCLASS()
class OPERATIONBREAKPOINT_API AOBAllyRoute : public AActor
{
    GENERATED_BODY()
public:
    AOBAllyRoute();
    UFUNCTION(BlueprintPure, Category="OB|Ally|Route") USplineComponent* GetSpline() const { return Spline; }
    // Keys are spline point indices; point 0 is the starting point rather than an arrival cue.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Ally|Route") TMap<int32,FOBAllyRouteCue> PointCues;
private:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="OB|Ally|Route", meta=(AllowPrivateAccess="true"))
    TObjectPtr<USplineComponent> Spline;
};

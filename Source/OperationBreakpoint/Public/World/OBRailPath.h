#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OBRailPath.generated.h"

class USplineComponent;

USTRUCT(BlueprintType)
struct OPERATIONBREAKPOINT_API FOBRailStopSettings
{
    GENERATED_BODY()

    // Off: face this point's encounter center. On: use the explicit world-space FacingYaw.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OB|Rail|Stops")
    bool bOverrideFacingYaw = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OB|Rail|Stops", meta = (EditCondition = "bOverrideFacingYaw", Units = "deg"))
    float FacingYaw = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OB|Rail|Stops")
    bool bIsCrouchMode = false;

    // Adjust the existing PlayerCamera at this stop; no separate camera actor is used.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OB|Rail|Stops")
    bool bOverrideCameraView = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OB|Rail|Stops", meta = (EditCondition = "bOverrideCameraView", ClampMin = "5.0", ClampMax = "170.0", UIMin = "5.0", UIMax = "170.0", Units = "deg"))
    float CameraFOV = 70.f;

    // Added to the player's original first-person camera rotation after the zone-facing body turn.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OB|Rail|Stops", meta = (EditCondition = "bOverrideCameraView"))
    FRotator CameraRotationOffset = FRotator::ZeroRotator;
};

UCLASS()
class OPERATIONBREAKPOINT_API AOBRailPath : public AActor
{
    GENERATED_BODY()

public:
    AOBRailPath();

    // Training: wait at point 0 and let the director decide when the final exercise is done.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "OB|Rail")
    bool bExternallyControlled = false;

    // Temporary route check: run once from the first point to the end, without encounters.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "OB|Rail|Route Preview")
    bool bPreviewEntireRoute = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "OB|Rail|Route Preview", meta = (ClampMin = "1.0", Units = "cm/s", EditCondition = "bPreviewEntireRoute"))
    float RoutePreviewSpeed = 300.0f;

    USplineComponent* GetSpline() const { return RailSpline; }
    int32 GetNextStopPointIndex(int32 AfterPointIndex) const;
    const FOBRailStopSettings* FindStopSettings(int32 PointIndex) const { return StopPointSettings.Find(PointIndex); }

private:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OB|Rail", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<USplineComponent> RailSpline;

    // Map keys are spline point indices (1 and above); registered points are stops.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "OB|Rail|Stops", meta = (AllowPrivateAccess = "true"))
    TMap<int32, FOBRailStopSettings> StopPointSettings;
};

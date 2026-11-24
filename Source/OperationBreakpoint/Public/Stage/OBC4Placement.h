#pragma once
#include "CoreMinimal.h"
#include "MediaPipe/OBBreachGuideTypes.h"

// Stage_01-only: consumes the existing post-mirror wrist feedback, not the tutorial's two-hand boolean.
struct OPERATIONBREAKPOINT_API FOBC4PlacementTracker
{
    // Half the original width/height, centered 15% of camera height higher.
    FVector2D TargetMin=FVector2D(.455,.295),TargetMax=FVector2D(.545,.405);
    FVector2D GetTargetCenter() const { return (TargetMin+TargetMax)*.5; }
    FVector2D HandUV=FVector2D::ZeroVector;
    float HoldSeconds=.25f,Progress=0;
    // Camera UV has its origin at the top: the raised wrist has the smaller Y.
    float HandSwitchHeightTolerance=.03f;
    int32 Hand=0; // 0:none, 1:left, 2:right; prefer the raised wrist, keep ties stable.
    bool bTracked=false,bAttached=false;
    void Reset(double ArmedAt) { LastSampleAt=ArmedAt;HeldSeconds=0;Hand=0;Progress=0;bTracked=bAttached=bInside=false; }
    bool Update(const FOBBreachGuideState& State,double SampleAt);
private:
    double LastSampleAt=0;
    float HeldSeconds=0;
    bool bInside=false;
};

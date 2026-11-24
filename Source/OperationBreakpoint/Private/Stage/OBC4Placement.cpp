#include "Stage/OBC4Placement.h"

bool FOBC4PlacementTracker::Update(const FOBBreachGuideState& S,double SampleAt)
{
    if (bAttached) return false;
    if (!S.bFresh || !S.bBreachMode || !S.bPlayerTracked || !FMath::IsFinite(SampleAt))
    { Hand=0;bTracked=bInside=false;HeldSeconds=Progress=0;return false; }
    if (SampleAt<=LastSampleAt) return false; // A frozen packet cannot finish attachment.
    const double Gap=SampleAt-LastSampleAt;LastSampleAt=SampleAt;
    auto Valid=[](FVector2D UV,float Visibility) {
        return FMath::IsFinite(Visibility) && Visibility>0 && FMath::IsFinite(UV.X) && FMath::IsFinite(UV.Y)
            && UV.X>=0 && UV.X<=1 && UV.Y>=0 && UV.Y<=1;
    };
    const bool Left=Valid(S.LeftWrist,S.LeftVisibility),Right=Valid(S.RightWrist,S.RightVisibility);
    const int32 PreviousHand=Hand;
    if (Left && Right)
    {
        if (Hand==1 && S.RightWrist.Y+HandSwitchHeightTolerance<S.LeftWrist.Y) Hand=2;
        else if (Hand==2 && S.LeftWrist.Y+HandSwitchHeightTolerance<S.RightWrist.Y) Hand=1;
        else if (Hand!=1 && Hand!=2) Hand=S.LeftWrist.Y<=S.RightWrist.Y ? 1 : 2;
    }
    else Hand=Left ? 1 : Right ? 2 : 0;
    bTracked=Hand!=0;
    if (!bTracked) { bInside=false;HeldSeconds=Progress=0;return false; }
    HandUV=Hand==1 ? S.LeftWrist : S.RightWrist;
    const bool Inside=HandUV.X>=TargetMin.X && HandUV.X<=TargetMax.X && HandUV.Y>=TargetMin.Y && HandUV.Y<=TargetMax.Y;
    if (!Inside || !bInside || Hand!=PreviousHand || Gap>.2) HeldSeconds=0;
    else HeldSeconds+=float(FMath::Min(Gap,.15));
    bInside=Inside;
    Progress=Inside ? FMath::Clamp(HeldSeconds/FMath::Max(.05f,HoldSeconds),0.f,1.f) : 0.f;
    if (Progress<1) return false;
    bAttached=true;return true;
}

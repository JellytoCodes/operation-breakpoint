#include "MediaPipe/OBBreachGuideTypes.h"

FBox2D FOBBreachGuideLayout::CameraRect(const FVector2D& WidgetSize, const FVector2D& CameraSize)
{
    if (!FMath::IsFinite(WidgetSize.X) || !FMath::IsFinite(WidgetSize.Y) ||
        !FMath::IsFinite(CameraSize.X) || !FMath::IsFinite(CameraSize.Y) ||
        WidgetSize.X <= 0 || WidgetSize.Y <= 0 || CameraSize.X <= 0 || CameraSize.Y <= 0)
        return FBox2D(FVector2D::ZeroVector, FVector2D::ZeroVector);
    const double Scale = FMath::Min(WidgetSize.X / CameraSize.X, WidgetSize.Y / CameraSize.Y);
    const FVector2D Size = CameraSize * Scale;
    const FVector2D Offset = (WidgetSize - Size) * 0.5;
    return FBox2D(Offset, Offset + Size);
}
FVector2D FOBBreachGuideLayout::MapPoint(const FVector2D& UV, const FBox2D& Rect)
{
    // Python already mirrors before detection. Mirror neither landmarks nor ROIs again.
    return Rect.Min + UV * Rect.GetSize();
}
bool FOBBreachGuideLayout::DecodePacket(const TArray<float>& V, FOBBreachGuideState& Out)
{
    if (V.Num() != 24 || V[0] != 1 || V[1] < 1 || V[2] < 1 || V[1] > 16384 || V[2] > 16384) return false;
    for (float Value : V) if (!FMath::IsFinite(Value)) return false;
    for (int32 I : {3, 5, 20, 21, 23}) if (V[I] != 0 && V[I] != 1) return false;
    if (V[4] < 1 || V[4] > 3 || V[4] != FMath::FloorToFloat(V[4])) return false;
    for (int32 I = 6; I < 14; ++I) if (V[I] < 0 || V[I] > 1) return false;
    if (V[6] >= V[8] || V[7] >= V[9] || V[10] >= V[12] || V[11] >= V[13]) return false;
    for (int32 I : {16, 19, 22}) if (V[I] < 0 || V[I] > 1) return false;
    FOBBreachGuideState S;
    S.CameraSize = FVector2D(V[1], V[2]);
    S.bFresh = true; S.bMirrored = V[3] != 0; S.bBreachMode = V[4] == 1; S.bPlayerTracked = V[5] != 0;
    S.LeftMin = FVector2D(V[6], V[7]); S.LeftMax = FVector2D(V[8], V[9]);
    S.RightMin = FVector2D(V[10], V[11]); S.RightMax = FVector2D(V[12], V[13]);
    if (S.bBreachMode && S.bPlayerTracked)
    {
        S.LeftWrist = FVector2D(V[14], V[15]); S.LeftVisibility = V[16];
        S.RightWrist = FVector2D(V[17], V[18]); S.RightVisibility = V[19];
        S.bLeftInside = V[20] != 0 && S.LeftVisibility > 0;
        S.bRightInside = V[21] != 0 && S.RightVisibility > 0;
        S.Progress = S.bLeftInside && S.bRightInside ? V[22] : 0;
        S.bReady = S.bLeftInside && S.bRightInside && V[23] != 0;
    }
    Out = S;
    return true;
}

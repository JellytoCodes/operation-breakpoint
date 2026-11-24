#pragma once

#include "CoreMinimal.h"

// OB's own loopback protocol. These are normalized screen coordinates, not IMU rotations.
struct OPERATIONBREAKPOINT_API FOBOpenFireFrame
{
    FString Session, Device;
    int32 Sequence = 0, TriggerCount = 0, ReloadCount = 0, CalibrationCount = 0;
    int32 InteractionTriggerCount = 0;
    FVector2D Position = FVector2D(.5, .5);
    double AimYOffsetUp = 0.; // Already applied by the bridge, normalized screen units.
    bool bConnected = false, bAimValid = false, bDemo = false, bCalibrated = false;
    bool bTrigger = false, bReload = false, bCalibration = false;
    bool bInteractionTrigger = false;
    static bool Decode(const FString& Json, FOBOpenFireFrame& Out);
    bool IsOnScreen() const;
    FVector2D ViewportPixel(FIntPoint Size) const;
};

struct OPERATIONBREAKPOINT_API FOBOpenFireEvents
{
    bool bTriggerHeld = false, bTriggerPressed = false;
    bool bInteractionHeld = false, bInteractionPressed = false;
    bool bReloadPressed = false, bCalibrationPressed = false;
};

struct OPERATIONBREAKPOINT_API FOBOpenFireState
{
    FOBOpenFireFrame Frame;
    FOBOpenFireEvents Events;
    double ReceivedAt = 0;
    bool bHasFrame = false;
    bool Accept(const FOBOpenFireFrame& Incoming, double Now, bool bAllowDemo);
    bool IsFresh(double Now) const;
    bool CanAim(double Now) const { return IsFresh(Now) && Frame.IsOnScreen(); }
    static constexpr double Timeout = .35;

private:
    bool bTriggerArmed = false, bReloadArmed = false, bCalibrationArmed = false;
    bool bInteractionArmed = false;
};

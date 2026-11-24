#include "Input/OBOpenFireTypes.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
bool ReadNumber(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, double& Out)
{
    const auto Field = Object->TryGetField(Key);
    return Field.IsValid() && Field->Type == EJson::Number && Field->TryGetNumber(Out);
}
bool ReadBool(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, bool& Out)
{
    const auto Field = Object->TryGetField(Key);
    return Field.IsValid() && Field->Type == EJson::Boolean && Field->TryGetBool(Out);
}
bool ReadInteger(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, int32& Out)
{
    double Value = 0;
    if (!ReadNumber(Object, Key, Value) || !FMath::IsFinite(Value)
        || Value < 0 || Value > MAX_int32 || Value != FMath::FloorToDouble(Value)) return false;
    Out = static_cast<int32>(Value);
    return true;
}
}

bool FOBOpenFireFrame::Decode(const FString& Json, FOBOpenFireFrame& Out)
{
    if (Json.Len() > 4096) return false;
    TSharedPtr<FJsonObject> Object;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object) || !Object.IsValid()) return false;
    FOBOpenFireFrame Result;
    int32 Version = 0;
    double X = 0, Y = 0;
    if (!ReadInteger(Object, TEXT("v"), Version) || Version != 1
        || !Object->TryGetStringField(TEXT("session"), Result.Session) || Result.Session.IsEmpty() || Result.Session.Len() > 64
        || !Object->TryGetStringField(TEXT("device"), Result.Device) || Result.Device.IsEmpty() || Result.Device.Len() > 512
        || !ReadInteger(Object, TEXT("sequence"), Result.Sequence)
        || !ReadInteger(Object, TEXT("trigger_count"), Result.TriggerCount)
        || !ReadInteger(Object, TEXT("reload_count"), Result.ReloadCount)
        || !ReadInteger(Object, TEXT("calibrate_count"), Result.CalibrationCount)
        || !ReadNumber(Object, TEXT("x"), X) || !ReadNumber(Object, TEXT("y"), Y)
        || !FMath::IsFinite(X) || !FMath::IsFinite(Y) || X < -4 || X > 4 || Y < -4 || Y > 4
        || !ReadBool(Object, TEXT("connected"), Result.bConnected)
        || !ReadBool(Object, TEXT("aim_valid"), Result.bAimValid)
        || !ReadBool(Object, TEXT("demo"), Result.bDemo)
        || !ReadBool(Object, TEXT("calibrated"), Result.bCalibrated)
        || !ReadBool(Object, TEXT("trigger"), Result.bTrigger)
        || !ReadBool(Object, TEXT("reload"), Result.bReload)
        || !ReadBool(Object, TEXT("calibrate"), Result.bCalibration)) return false;
    // Older senders have one trigger channel. New IR bridges keep a raw UI
    // channel alongside the aim-qualified combat channel.
    Result.InteractionTriggerCount=Result.TriggerCount;
    Result.bInteractionTrigger=Result.bTrigger;
    const bool bHasInteraction=Object->HasField(TEXT("interaction_trigger"));
    if (bHasInteraction!=Object->HasField(TEXT("interaction_trigger_count"))) return false;
    if (bHasInteraction && (!ReadBool(Object,TEXT("interaction_trigger"),Result.bInteractionTrigger)
        || !ReadInteger(Object,TEXT("interaction_trigger_count"),Result.InteractionTriggerCount))) return false;
    if (!Result.bConnected && (Result.bAimValid || Result.bTrigger || Result.bReload || Result.bCalibration || Result.bInteractionTrigger)) return false;
    if (Object->HasField(TEXT("aim_y_up")) && (!ReadNumber(Object,TEXT("aim_y_up"),Result.AimYOffsetUp)
        || !FMath::IsFinite(Result.AimYOffsetUp) || FMath::Abs(Result.AimYOffsetUp)>2.)) return false;
    Result.Position = FVector2D(X, Y);
    Out = MoveTemp(Result);
    return true;
}

bool FOBOpenFireFrame::IsOnScreen() const
{
    return bConnected && bAimValid && FMath::IsFinite(Position.X) && FMath::IsFinite(Position.Y)
        && Position.X >= 0 && Position.X <= 1 && Position.Y >= 0 && Position.Y <= 1;
}

FVector2D FOBOpenFireFrame::ViewportPixel(FIntPoint Size) const
{
    return Position * FVector2D(FMath::Max(0, Size.X-1), FMath::Max(0, Size.Y-1));
}

bool FOBOpenFireState::IsFresh(double Now) const
{
    return bHasFrame && Frame.bConnected && Now >= ReceivedAt && Now-ReceivedAt <= Timeout;
}

bool FOBOpenFireState::Accept(const FOBOpenFireFrame& Incoming, double Now, bool bAllowDemo)
{
    if (!FMath::IsFinite(Now) || (Incoming.bDemo && !bAllowDemo)) return false;
    const bool bSameSession = bHasFrame && Incoming.Session == Frame.Session && Incoming.Device == Frame.Device;
    // One active gun/bridge owns the stream until it stops or times out.
    if (!bSameSession && IsFresh(Now)) return false;
    if (bSameSession && (Incoming.Sequence <= Frame.Sequence || Incoming.TriggerCount < Frame.TriggerCount
        || Incoming.ReloadCount < Frame.ReloadCount || Incoming.CalibrationCount < Frame.CalibrationCount
        || Incoming.InteractionTriggerCount < Frame.InteractionTriggerCount)) return false;
    const bool bContinuing = bSameSession && IsFresh(Now) && Incoming.bConnected;
    Events = {};
    if (!bContinuing)
    {
        // Connecting with a held button cannot fire or activate a training menu.
        bTriggerArmed = !Incoming.bTrigger;
        bReloadArmed = !Incoming.bReload;
        bCalibrationArmed = !Incoming.bCalibration;
        bInteractionArmed = !Incoming.bInteractionTrigger;
    }
    else
    {
        // Cumulative counters retain a short tap even when its down packet is lost.
        Events.bTriggerPressed = bTriggerArmed && Incoming.TriggerCount > Frame.TriggerCount;
        Events.bReloadPressed = bReloadArmed && Incoming.ReloadCount > Frame.ReloadCount;
        Events.bCalibrationPressed = bCalibrationArmed && Incoming.CalibrationCount > Frame.CalibrationCount;
        Events.bInteractionPressed = bInteractionArmed && Incoming.InteractionTriggerCount > Frame.InteractionTriggerCount;
        bTriggerArmed |= !Incoming.bTrigger;
        bReloadArmed |= !Incoming.bReload;
        bCalibrationArmed |= !Incoming.bCalibration;
        bInteractionArmed |= !Incoming.bInteractionTrigger;
    }
    Events.bTriggerHeld = Incoming.bConnected && bTriggerArmed && Incoming.bTrigger;
    Events.bInteractionHeld = Incoming.bConnected && bInteractionArmed && Incoming.bInteractionTrigger;
    Frame = Incoming;
    ReceivedAt = Now;
    bHasFrame = true;
    return true;
}

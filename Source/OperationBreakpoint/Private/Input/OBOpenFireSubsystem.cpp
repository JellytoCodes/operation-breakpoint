#include "Input/OBOpenFireSubsystem.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Input/OBDeviceSettingsSubsystem.h"
#include "Misc/ConfigCacheIni.h"
#include "OSCAddress.h"
#include "OSCManager.h"
#include "OSCMessage.h"
#include "OSCServer.h"

DEFINE_LOG_CATEGORY_STATIC(LogOBOpenFire, Log, All);

namespace { bool GOBRuntimeGunInput=false; }

bool UOBOpenFireSubsystem::IsRequested()
{
    return GOBRuntimeGunInput || FParse::Param(FCommandLine::Get(), TEXT("OBOpenFire"));
}

bool UOBOpenFireSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    return Super::ShouldCreateSubsystem(Outer);
}

void UOBOpenFireSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    if (IsRequested() || FParse::Param(FCommandLine::Get(),TEXT("OBOpenFireDemo"))) EnableRuntimeInput();
}

void UOBOpenFireSubsystem::EnableRuntimeInput()
{
    if (Server && Server->IsActive()) return;
    int32 Port = 9011;
    FConfigFile Calibration; Calibration.Read(UOBDeviceSettingsSubsystem::SettingsPath());
    Calibration.GetInt(TEXT("Gun"),TEXT("udp_port"),Port);
    FParse::Value(FCommandLine::Get(), TEXT("OBOpenFirePort="), Port);
    bAllowDemo = FParse::Param(FCommandLine::Get(), TEXT("OBOpenFireDemo"));
    if (Port < 1024 || Port > 65535)
    {
        UE_LOG(LogOBOpenFire, Error, TEXT("OBOpenFirePort must be between 1024 and 65535."));
        return;
    }
    // UE 5.8 substitutes the LAN address for a loopback bind; bind any + allowlist.
    Server = UOSCManager::CreateOSCServer(TEXT("0.0.0.0"), Port, false, false, TEXT("OBOpenFire"), this);
    if (Server)
    {
        Server->SetAllowlistClientsEnabled(true);
        Server->AddAllowlistedClient(TEXT("127.0.0.1"));
        Server->OnOscMessageReceivedNative.AddUObject(this, &ThisClass::HandleMessage);
        Server->Listen();
    }
    if (!Server || !Server->IsActive())
    {
        UE_LOG(LogOBOpenFire, Error, TEXT("OpenFire receiver could not listen on port %d. Close other game/PIE instances."), Port);
    }
    else
    {
        UE_LOG(LogOBOpenFire, Display, TEXT("OPENFIRE_READY port=%d allow_demo=%d"), Port, bAllowDemo);
    }
}

void UOBOpenFireSubsystem::Deinitialize()
{
    OnInputReceived.Clear();
    if (Server)
    {
        Server->OnOscMessageReceivedNative.RemoveAll(this);
        Server->Stop();
        Server = nullptr;
    }
    State = {};
    Super::Deinitialize();
}

bool UOBOpenFireSubsystem::IsConnected() const
{
    return State.IsFresh(FPlatformTime::Seconds());
}

bool UOBOpenFireSubsystem::GetAimPosition(FVector2D& Position) const
{
    if (!State.CanAim(FPlatformTime::Seconds())) return false;
    Position = State.Frame.Position;
    return true;
}

void UOBOpenFireSubsystem::HandleMessage(const FOSCMessage& Message, const FString& Sender, uint16)
{
    if (Sender != TEXT("127.0.0.1") || UOSCManager::GetOSCMessageAddress(Message).GetFullPath() != TEXT("/ob/openfire/input")) return;
    FString Json;
    FOBOpenFireFrame Frame;
    if (!UOSCManager::GetString(Message, 0, Json) || !FOBOpenFireFrame::Decode(Json, Frame)) return;
    const bool bWasConnected = IsConnected();
    const bool bNewSession = !State.bHasFrame || Frame.Session != State.Frame.Session;
    if (!State.Accept(Frame, FPlatformTime::Seconds(), bAllowDemo)) return;
    if ((bNewSession || !bWasConnected) && Frame.bConnected)
        UE_LOG(LogOBOpenFire, Display, TEXT("OPENFIRE_CONNECTED device=%s demo=%d calibrated=%d"), *Frame.Device, Frame.bDemo, Frame.bCalibrated);
    if (bWasConnected && !Frame.bConnected)
        UE_LOG(LogOBOpenFire, Display, TEXT("OPENFIRE_STOPPED"));
    GOBRuntimeGunInput=Frame.bConnected;
    OnInputReceived.Broadcast(State.Events);
}

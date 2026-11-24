#include "MediaPipe/OBMediaPipeOSCSubsystem.h"

#include "OSCAddress.h"
#include "Engine/GameInstance.h"
#include "OSCManager.h"
#include "OSCMessage.h"
#include "OSCServer.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogOBMediaPipeOSC, Log, All);

namespace OBOSC
{
    constexpr int32 Port = 8000;
    constexpr double ConnectionTimeout = 2.5;
    constexpr float DroneControlTimeout = 0.35f;
    constexpr float CrouchTimeout = 0.75f;
    constexpr const TCHAR* ReceiveIPAddress = TEXT("0.0.0.0");
    constexpr const TCHAR* LocalSenderIPAddress = TEXT("127.0.0.1");
    constexpr const TCHAR* HeartbeatAddress = TEXT("/system/mediapipe/heartbeat");
    constexpr const TCHAR* FingerHeartbeatAddress = TEXT("/system/mediapipe/finger/heartbeat");
    constexpr const TCHAR* DroneControlAddress = TEXT("/mediapipe/drone/control");
    constexpr const TCHAR* CrouchAddress = TEXT("/motion/crouch");
    constexpr const TCHAR* BreachAddress = TEXT("/motion/breach");
    constexpr const TCHAR* BreachGuideAddress = TEXT("/mediapipe/breach/guide");
    constexpr const TCHAR* LeftWristAddress = TEXT("/mediapipe/left_wrist");
    constexpr const TCHAR* RightWristAddress = TEXT("/mediapipe/right_wrist");
}

void UOBMediaPipeOSCSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // UE 5.8's OSC receiver rewrites a loopback bind to the LAN adapter.
    // A wildcard bind plus the OSC allowlist keeps the sender local.
    OSCServer = UOSCManager::CreateOSCServer(OBOSC::ReceiveIPAddress, OBOSC::Port, false, false, TEXT("OBMediaPipeOSCServer"), this);
    OSCServer->AddAllowlistedClient(OBOSC::LocalSenderIPAddress, 0);
    OSCServer->SetAllowlistClientsEnabled(true);
    OSCServer->OnOscMessageReceivedNative.AddUObject(this, &ThisClass::HandleOSCMessage);
    OSCServer->Listen();
}

void UOBMediaPipeOSCSubsystem::Deinitialize()
{
    if (UGameInstance* GameInstance = GetGameInstance())
    {
        GameInstance->GetTimerManager().ClearTimer(DroneControlTimeoutHandle);
        GameInstance->GetTimerManager().ClearTimer(BreachTimeoutHandle);
        GameInstance->GetTimerManager().ClearTimer(CrouchTimeoutHandle);
    }
    OSCServer->OnOscMessageReceivedNative.RemoveAll(this);
    OSCServer->Stop();
    OSCServer = nullptr;

    Super::Deinitialize();
}

bool UOBMediaPipeOSCSubsystem::IsBridgeConnected() const
{
    return LastMessageReceivedTime > 0.0 && FPlatformTime::Seconds() - LastMessageReceivedTime <= OBOSC::ConnectionTimeout;
}

bool UOBMediaPipeOSCSubsystem::IsFingerBridgeConnected() const
{
    return LastFingerMessageReceivedTime > 0.0
        && FPlatformTime::Seconds() - LastFingerMessageReceivedTime <= OBOSC::ConnectionTimeout;
}

FOBBreachGuideState UOBMediaPipeOSCSubsystem::GetBreachGuideState() const
{
    auto State = LatestBreachGuide;
    if (LastBreachGuideTime <= 0 || FPlatformTime::Seconds() - LastBreachGuideTime > 0.5)
    {
        State.bFresh = State.bPlayerTracked = State.bLeftInside = State.bRightInside = State.bReady = false;
        State.LeftVisibility = State.RightVisibility = State.Progress = 0;
    }
    return State;
}

FOBDroneControlState UOBMediaPipeOSCSubsystem::GetDroneControlState() const
{
    if (LastDroneControlReceivedTime <= 0.0
        || FPlatformTime::Seconds() - LastDroneControlReceivedTime > OBOSC::DroneControlTimeout)
    {
        return FOBDroneControlState{};
    }
    return LatestDroneControl;
}

void UOBMediaPipeOSCSubsystem::HandleOSCMessage(const FOSCMessage& Message, const FString&, uint16)
{
    const FString AddressPath = UOSCManager::GetOSCMessageAddress(Message).GetFullPath();
    if (AddressPath == OBOSC::BreachGuideAddress)
    {
        TArray<float> Values;
        UOSCManager::GetAllFloats(Message, Values);
        if (FOBBreachGuideLayout::DecodePacket(Values, LatestBreachGuide))
        {
            LastBreachGuideTime = LastMessageReceivedTime = FPlatformTime::Seconds();
        }
        return;
    }

    if (AddressPath == OBOSC::HeartbeatAddress)
    {
        LastMessageReceivedTime = FPlatformTime::Seconds();
        return;
    }

    if (AddressPath == OBOSC::FingerHeartbeatAddress)
    {
        LastFingerMessageReceivedTime = FPlatformTime::Seconds();
        return;
    }

    if (AddressPath == OBOSC::DroneControlAddress)
    {
        UpdateDroneControl(Message);
        return;
    }

    if (AddressPath == OBOSC::LeftWristAddress)
    {
        UpdateWrist(Message, LatestLeftWrist, LeftWristVisibility);
        return;
    }

    if (AddressPath == OBOSC::RightWristAddress)
    {
        UpdateWrist(Message, LatestRightWrist, RightWristVisibility);
        return;
    }

    if (AddressPath == OBOSC::BreachAddress)
    {
        bool bRecognized = false;
        if (!UOSCManager::GetBool(Message, 0, bRecognized))
        {
            TArray<int32> Values;
            UOSCManager::GetAllInt32s(Message, Values);
            if (Values.Num() != 1 || (Values[0] != 0 && Values[0] != 1)) return;
            bRecognized = Values[0] == 1;
        }
        LastMessageReceivedTime = FPlatformTime::Seconds();
        GetGameInstance()->GetTimerManager().SetTimer(BreachTimeoutHandle, this, &ThisClass::ResetBreachState, 0.75f, false);
        if (bBreachRecognized != bRecognized)
        {
            bBreachRecognized = bRecognized;
            OnBreachStateChanged.Broadcast(bRecognized);
        }
        return;
    }

    if (AddressPath == OBOSC::CrouchAddress)
    {
	    TArray<int32> Values;
	    UOSCManager::GetAllInt32s(Message, Values);
	    if (Values.Num() != 1 || (Values[0] != 0 && Values[0] != 1)) return;

	    const bool bNewCrouching = Values[0] != 0;
	    LastMessageReceivedTime = FPlatformTime::Seconds();
        GetGameInstance()->GetTimerManager().SetTimer(CrouchTimeoutHandle, this, &ThisClass::ResetCrouchState, OBOSC::CrouchTimeout, false);

	    if (bIsCrouching == bNewCrouching) return;

	    bIsCrouching = bNewCrouching;
        UE_LOG(LogOBMediaPipeOSC,Display,TEXT("MEDIAPIPE_CROUCH received=%s"),bIsCrouching ? TEXT("Crouch") : TEXT("Stand"));
	    OnCrouchStateChanged.Broadcast(bIsCrouching);
    }
}

void UOBMediaPipeOSCSubsystem::UpdateWrist(const FOSCMessage& Message, FVector& OutWrist, float& OutVisibility)
{
    TArray<float> Values;
    UOSCManager::GetAllFloats(Message, Values);
    if (Values.Num() != 4) return;

    OutWrist = FVector(Values[0], Values[1], Values[2]);
    OutVisibility = Values[3];
    LastMessageReceivedTime = FPlatformTime::Seconds();
}

void UOBMediaPipeOSCSubsystem::ResetBreachState()
{
    if (!bBreachRecognized) return;
    bBreachRecognized = false;
    OnBreachStateChanged.Broadcast(false);
}

void UOBMediaPipeOSCSubsystem::ResetCrouchState()
{
    if (!bIsCrouching) return;
    bIsCrouching = false;
    UE_LOG(LogOBMediaPipeOSC, Display, TEXT("MEDIAPIPE_CROUCH timed out -> Stand"));
    OnCrouchStateChanged.Broadcast(false);
}

void UOBMediaPipeOSCSubsystem::UpdateDroneControl(const FOSCMessage& Message)
{
    TArray<float> Values;
    UOSCManager::GetAllFloats(Message, Values);
    if (Values.Num() != 5) return;

    const double Now = FPlatformTime::Seconds();
    const bool bWasTimedOut = LastDroneControlReceivedTime <= 0.0
        || Now - LastDroneControlReceivedTime > OBOSC::DroneControlTimeout;

    FOBDroneControlState NewControl;
    NewControl.bRightHandTracked = Values[3] >= 0.5f;
    NewControl.bLeftHandTracked = Values[4] >= 0.5f;
    NewControl.ForwardInput = NewControl.bRightHandTracked ? FMath::Clamp(Values[0], 0.0f, 1.0f) : 0.0f;
    NewControl.YawInput = NewControl.bLeftHandTracked ? FMath::Clamp(Values[1], -1.0f, 1.0f) : 0.0f;
    NewControl.VerticalInput = NewControl.bLeftHandTracked ? FMath::Clamp(Values[2], -1.0f, 1.0f) : 0.0f;

    const bool bChanged = !FMath::IsNearlyEqual(LatestDroneControl.ForwardInput, NewControl.ForwardInput)
        || !FMath::IsNearlyEqual(LatestDroneControl.YawInput, NewControl.YawInput)
        || !FMath::IsNearlyEqual(LatestDroneControl.VerticalInput, NewControl.VerticalInput)
        || LatestDroneControl.bRightHandTracked != NewControl.bRightHandTracked
        || LatestDroneControl.bLeftHandTracked != NewControl.bLeftHandTracked;

    LatestDroneControl = NewControl;
    LastFingerMessageReceivedTime = Now;
    LastDroneControlReceivedTime = Now;
    if (UGameInstance* GameInstance = GetGameInstance())
    {
        GameInstance->GetTimerManager().SetTimer(
            DroneControlTimeoutHandle,
            this,
            &ThisClass::ResetDroneControl,
            OBOSC::DroneControlTimeout,
            false
        );
    }
    if (bChanged || bWasTimedOut) OnDroneControlChanged.Broadcast(LatestDroneControl);
}

void UOBMediaPipeOSCSubsystem::ResetDroneControl()
{
    const bool bHadControl = !FMath::IsNearlyZero(LatestDroneControl.ForwardInput)
        || !FMath::IsNearlyZero(LatestDroneControl.YawInput)
        || !FMath::IsNearlyZero(LatestDroneControl.VerticalInput)
        || LatestDroneControl.bRightHandTracked
        || LatestDroneControl.bLeftHandTracked;
    LatestDroneControl = FOBDroneControlState{};
    LastDroneControlReceivedTime = 0.0;
    if (bHadControl) OnDroneControlChanged.Broadcast(LatestDroneControl);
}

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MediaPipe/OBBreachGuideTypes.h"
#include "OBMediaPipeOSCSubsystem.generated.h"

class UOSCServer;
struct FOSCMessage;

DECLARE_MULTICAST_DELEGATE_OneParam(FOBOnCrouchStateChanged, bool);

USTRUCT(BlueprintType)
struct OPERATIONBREAKPOINT_API FOBDroneControlState
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "OB|MediaPipe|Drone")
    float ForwardInput = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "OB|MediaPipe|Drone")
    float YawInput = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "OB|MediaPipe|Drone")
    float VerticalInput = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "OB|MediaPipe|Drone")
    bool bRightHandTracked = false;

    UPROPERTY(BlueprintReadOnly, Category = "OB|MediaPipe|Drone")
    bool bLeftHandTracked = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOBDroneControlChangedSignature, FOBDroneControlState, ControlState);

UCLASS()
class OPERATIONBREAKPOINT_API UOBMediaPipeOSCSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    UFUNCTION(BlueprintPure, Category = "OB|MediaPipe")
    bool IsBridgeConnected() const;

    UFUNCTION(BlueprintPure, Category = "OB|MediaPipe")
    bool IsCrouching() const { return bIsCrouching; }
    UFUNCTION(BlueprintPure, Category = "OB|MediaPipe")
    bool IsBreachRecognized() const { return bBreachRecognized; }
    UFUNCTION(BlueprintPure, Category = "OB|MediaPipe")
    FOBBreachGuideState GetBreachGuideState() const;
    double GetBreachGuideSampleTime() const { return LastBreachGuideTime; }

    FOBOnCrouchStateChanged OnCrouchStateChanged;
    FOBOnCrouchStateChanged OnBreachStateChanged;

    UFUNCTION(BlueprintPure, Category = "OB|MediaPipe")
    FVector GetLeftWrist() const { return LatestLeftWrist; }

    UFUNCTION(BlueprintPure, Category = "OB|MediaPipe")
    FVector GetRightWrist() const { return LatestRightWrist; }

    UFUNCTION(BlueprintPure, Category = "OB|MediaPipe")
    float GetLeftWristVisibility() const { return LeftWristVisibility; }

    UFUNCTION(BlueprintPure, Category = "OB|MediaPipe")
    float GetRightWristVisibility() const { return RightWristVisibility; }

    UFUNCTION(BlueprintPure, Category = "OB|MediaPipe|Drone")
    bool IsFingerBridgeConnected() const;

    UFUNCTION(BlueprintPure, Category = "OB|MediaPipe|Drone")
    FOBDroneControlState GetDroneControlState() const;

    UPROPERTY(BlueprintAssignable, Category = "OB|MediaPipe|Drone")
    FOBDroneControlChangedSignature OnDroneControlChanged;

private:
    friend class FOBUICaptureSmoke;
    friend class FOBBreachGuideTest;
    friend class FOBTrainingMediaPipePoseTest;
    friend class FOBStageZone6GateTest;
    friend class FOBStageZone6MapSmoke;
    friend class FOBC4BreachMapSmoke;
    void HandleOSCMessage(const FOSCMessage& Message, const FString& SenderIPAddress, uint16 SenderPort);
    void UpdateWrist(const FOSCMessage& Message, FVector& OutWrist, float& OutVisibility);
    void UpdateDroneControl(const FOSCMessage& Message);
    void ResetDroneControl();
    void ResetBreachState();
    void ResetCrouchState();

    UPROPERTY()
    TObjectPtr<UOSCServer> OSCServer;

    FVector LatestLeftWrist = FVector::ZeroVector;
    FVector LatestRightWrist = FVector::ZeroVector;
    float LeftWristVisibility = 0.0f;
    float RightWristVisibility = 0.0f;
    bool bIsCrouching = false;
    bool bBreachRecognized = false;
    FTimerHandle BreachTimeoutHandle;
    FTimerHandle CrouchTimeoutHandle;
    FOBBreachGuideState LatestBreachGuide;
    double LastBreachGuideTime = 0;
    double LastMessageReceivedTime = 0.0;
    FOBDroneControlState LatestDroneControl;
    double LastFingerMessageReceivedTime = 0.0;
    double LastDroneControlReceivedTime = 0.0;
    FTimerHandle DroneControlTimeoutHandle;
};

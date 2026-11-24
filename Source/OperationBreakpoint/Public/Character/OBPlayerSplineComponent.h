#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "OBPlayerSplineComponent.generated.h"

class AOBPlayerCharacter;
class AOBRailPath;
class UCameraComponent;
class AOBEncounterTrigger;

DECLARE_MULTICAST_DELEGATE_OneParam(FOBOnActionPointReached, int32);

UCLASS(ClassGroup = (OB), meta = (BlueprintSpawnableComponent))
class OPERATIONBREAKPOINT_API UOBPlayerSplineComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UOBPlayerSplineComponent();

    void StartMoveToNextPoint();
    void SetStageMovementBlocked(bool bBlocked) { bStageMovementBlocked=bBlocked; }
    void StopTrainingMovement();
    bool JumpToTrainingPointForQA(int32 PointIndex, float YawOffset, FString& Error);
    bool JumpToStagePointForQA(int32 PointIndex,FString& Error);
    bool StartAtStageStop(int32 PointIndex,FString& Error);
    bool TurnTrainingToYaw(float WorldYaw);
    bool SnapTrainingToYaw(float WorldYaw);
    void FocusTrainingCamera(float PitchOffset, float FocusFOV, float BlendSeconds, float YawOffset = 0.f);
    void ClearTrainingCameraFocus(bool bImmediate = false);
    bool IsTrainingCameraFocusActive() const { return bTrainingFocus || bTrainingFocusBlending; }
    bool EnterCameraTuningForQA(int32 PointIndex,FString& Error);
    bool StartCameraTuningForQA();
    bool AdjustCameraForQA(float FOVDelta,float PitchDelta,float YawDelta=0.f,float RollDelta=0.f);
    void EndCameraTuningForQA();
    bool IsCameraTuningForQA() const { return bQACameraTuning; }
    bool IsCameraEditingForQA() const { return bQACameraTuning && bQACameraEditing; }
    FString GetCameraTuningStatusForQA() const;

    int32 GetCurrentPointIndex() const { return CurrentPointIndex; }
    bool IsAtActionPoint() const { return RailPath && RailState == EOBRailState::Action; }
    AOBRailPath* GetRailPath() const { return RailPath; }
    bool IsMoving() const { return RailState == EOBRailState::Moving || RailState == EOBRailState::RoutePreview; }

    UFUNCTION(BlueprintPure, Category = "OB|Rail|Route Preview")
    bool IsRoutePreview() const;
    bool HasFinishedRoutePreview() const { return RailState == EOBRailState::RoutePreviewComplete; }
    float GetDistanceAlongRoute() const { return CurrentDistance; }
    float GetTravelTargetDistance() const { return TargetDistance; }
    FVector GetRailVelocity() const { return RailVelocity; }

    FOBOnActionPointReached OnActionPointReached;
    // Every spline point passed, including non-combat points and route preview.
    FOBOnActionPointReached OnSplinePointReached;
    FSimpleMulticastDelegate OnTravelStarted;
    FSimpleMulticastDelegate OnTrainingTurnCompleted;
    FSimpleMulticastDelegate OnRoutePreviewCompleted;

protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    friend class FOBOpenFireInputModesTest;
    friend class FOBStageAmmoTest;
    friend class FOBTrainingSoundTest;
    friend class FOBPackagePerformance;
    friend class FOBRailCameraFOVTest;
    friend class FOBTrainingFlowTest;
    friend class FOBTrainingSplinePrototypeTest;
    friend class FOBRoutePreviewTest;
    friend class FOBAllyRedeployTest;
    friend class FOBSquadRendezvousTest;
    friend class FOBSquadTravelOrderTest;
    friend class FOBAllyNavigationOrderTest;
    friend class FOBStageSquadMapSmoke;
    friend class FOBStageEncounterMapSmoke;
    friend class FOBStageFlowMapSmoke;
    friend class FOBStageDepartureMapSmoke;
    friend class FOBQATrainingCheckpointTest;
    friend class FOBRailCombatFacingTest;
    friend class FOBStageTravelCameraTest;
    friend class FOBQACameraTuningTest;
    friend class FOBStageZone6GateTest;
    friend class FOBStageZone6MapSmoke;

    enum class EOBRailState : uint8
    {
        Action,
        Moving,
        AligningAtStop,
        AligningForTravel,
        TurningAtTrainingStop,
        RoutePreview,
        RoutePreviewComplete
    };

    void UpdateMovement(float DeltaTime);
    void BeginStopAlignment(int32 ReachedPointIndex);
    void NotifySplinePointsReached();
    void UpdateCamera(float DeltaTime);
    bool UsesFirstPersonTravelCamera() const;
    float GetStopCameraFOV() const;
    FRotator GetStopCameraRotation() const;
    void UpdateCameraFOV(float DeltaTime);
    void UpdateTrainingCameraFocus(float DeltaTime);
    void UpdateStopRotation(float DeltaTime);
    void ApplySplineTransform(float Distance);
    void ApplySplineLocation(float Distance);
    void ApplyPlayerYaw(float WorldYaw);
    bool IsStopRotationComplete() const;
    void BeginAction();
    void BeginTravel();
    void StartRoutePreview();
    void UpdateRoutePreview(float DeltaTime);

    UPROPERTY(EditDefaultsOnly, Category = "OB|Rail")
    float MoveSpeed = 500.0f;
    UPROPERTY(EditDefaultsOnly, Category = "OB|Rail|Rotation", meta = (ClampMin = "1.0", UIMin = "1.0"))
    float StopTurnSpeed = 180.0f;
    UPROPERTY(EditDefaultsOnly, Category = "OB|Rail|Rotation", meta = (ClampMin = "0.01", UIMin = "0.01"))
    float StopRotationTolerance = 0.5f;
    UPROPERTY(EditDefaultsOnly, Category = "OB|Rail|Camera")
    FVector TravelCameraRelativeLocation = FVector(-300.0f, 0.0f, 180.0f);
    UPROPERTY(EditDefaultsOnly, Category = "OB|Rail|Camera")
    FRotator TravelCameraRelativeRotation = FRotator(-12.0f, 0.0f, 0.0f);
    UPROPERTY(EditDefaultsOnly, Category = "OB|Rail|Camera")
    float CameraInterpSpeed = 5.0f;
    // During play these state targets drive PlayerCamera's Field Of View directly.
    UPROPERTY(EditDefaultsOnly, Category = "OB|Rail|Camera|FOV", meta = (ClampMin = "5.0", ClampMax = "170.0", Units = "deg"))
    float TravelFOV = 80.0f;
    UPROPERTY(EditDefaultsOnly, Category = "OB|Rail|Camera|FOV", meta = (ClampMin = "5.0", ClampMax = "170.0", Units = "deg"))
    float ActionFOV = 70.0f;
    UPROPERTY(EditDefaultsOnly, Category = "OB|Rail|Camera|FOV", meta = (ClampMin = "0.1", ClampMax = "30.0"))
    float FOVInterpSpeed = 8.0f;
    UPROPERTY(Transient)
    TObjectPtr<AOBPlayerCharacter> PlayerCharacter;
    UPROPERTY(Transient)
    TObjectPtr<AOBRailPath> RailPath;
    UPROPERTY(Transient)
    TObjectPtr<UCameraComponent> PlayerCamera;

    FVector FirstPersonCameraLocation;
    FRotator FirstPersonCameraRotation;
    FRotator TrainingFocusBaseRotation;
    float TrainingFocusPitch = 0, TrainingFocusYaw = 0, TrainingFocusFOV = 50;
    float TrainingFocusWeight = 0, TrainingFocusStartWeight = 0, TrainingFocusStartFOV = 70;
    float TrainingFocusElapsed = 0, TrainingFocusBlendSeconds = .65f;
    bool bTrainingFocus = false, bTrainingFocusBlending = false;
    FRotator TargetStopRotation = FRotator::ZeroRotator;
    float CurrentStopYaw = 0.0f;

    EOBRailState RailState = EOBRailState::Action;

    int32 CurrentPointIndex = 0;
    int32 TargetPointIndex = INDEX_NONE;
    int32 NextSplineCuePoint = 1;

    float CurrentDistance = 0.0f;
    float TargetDistance = 0.0f;
    FVector RailVelocity = FVector::ZeroVector;
    // Runtime previews only. These never modify the authored rail's StopPointSettings.
    TMap<int32,FVector4> QACameraViews; // FOV, Pitch, Yaw, Roll offsets.
    TWeakObjectPtr<AOBEncounterTrigger> QACameraEncounter;
    bool bQACameraTuning=false,bQACameraEditing=false,bQASavedDamageable=false,bQASavedAutoAdvance=false;
    bool bStageMovementBlocked=false;
    uint8 QASavedMovementMode=0,QASavedCustomMode=0;
};

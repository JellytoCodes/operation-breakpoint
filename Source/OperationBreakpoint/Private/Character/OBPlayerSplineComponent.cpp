#include "Character/OBPlayerSplineComponent.h"

#include "Camera/CameraComponent.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBActionComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SplineComponent.h"
#include "Core/OBPlayerController.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "MediaPipe/OBMediaPipeInputComponent.h"
#include "World/OBRailPath.h"
#include "World/OBEncounterTrigger.h"
#include "World/OBSquadRendezvous.h"
#include "Stage/OBStageFlowComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogOBPlayerSpline, Log, All);

UOBPlayerSplineComponent::UOBPlayerSplineComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UOBPlayerSplineComponent::BeginPlay()
{
    Super::BeginPlay();

    PlayerCharacter = Cast<AOBPlayerCharacter>(GetOwner());
    if (!IsValid(PlayerCharacter))
    {
        UE_LOG(LogOBPlayerSpline, Error, TEXT("PlayerSplineComponent requires AOBPlayerCharacter owner"));
        SetComponentTickEnabled(false);
        return;
    }

    PlayerCamera = PlayerCharacter->GetPlayerCamera();
    if (!IsValid(PlayerCamera))
    {
        UE_LOG(LogOBPlayerSpline, Error, TEXT("Player camera is invalid | Owner=%s"), *GetNameSafe(PlayerCharacter));
        SetComponentTickEnabled(false);
        return;
    }

    for (TActorIterator<AOBRailPath> It(GetWorld()); It; ++It)
    {
        RailPath = *It;
        break;
    }

    if (!IsValid(RailPath))
    {
        UE_LOG(LogOBPlayerSpline, Error, TEXT("OBRailPath was not found in the level"));
        SetComponentTickEnabled(false);
        return;
    }

    FirstPersonCameraLocation = PlayerCamera->GetRelativeLocation();
    FirstPersonCameraRotation = PlayerCamera->GetRelativeRotation();

    USplineComponent* Spline = RailPath->GetSpline();
    CurrentDistance = Spline->GetDistanceAlongSplineAtSplinePoint(CurrentPointIndex);
    NextSplineCuePoint = CurrentPointIndex + 1;

    if (RailPath->bPreviewEntireRoute)
    {
        StartRoutePreview();
        return;
    }

    ApplySplineTransform(CurrentDistance);
    if (RailPath->bExternallyControlled)
    {
        TargetStopRotation = PlayerCharacter->GetActorRotation();
        BeginAction();
    }
    else StartMoveToNextPoint();
    PlayerCamera->SetFieldOfView(FMath::Clamp(
        RailState == EOBRailState::Action ? GetStopCameraFOV() : TravelFOV, 5.0f, 170.0f));
}

void UOBPlayerSplineComponent::StartMoveToNextPoint()
{
    if (bStageMovementBlocked) return;
    if (bQACameraTuning) return;
    if (IsRoutePreview()) return;
    if (RailState != EOBRailState::Action) return;
    if (!IsValid(RailPath)) return;
    if (PlayerCharacter->GetStageFlowComponent()->DeferInitialTravelUntilNarrationDone()) return;

    USplineComponent* Spline = RailPath->GetSpline();
    TargetPointIndex = PlayerCharacter->GetStageFlowComponent()->GetNextStageStop(
        CurrentPointIndex,RailPath->GetNextStopPointIndex(CurrentPointIndex));
    if (TargetPointIndex>=Spline->GetNumberOfSplinePoints()) TargetPointIndex=INDEX_NONE;
    if (TargetPointIndex == INDEX_NONE)
    {
        if (!RailPath->bExternallyControlled && CurrentPointIndex == Spline->GetNumberOfSplinePoints()-1)
            if (!PlayerCharacter->GetStageFlowComponent()->IsStageActive())
                if (auto* Controller=Cast<AOBPlayerController>(PlayerCharacter->GetController())) Controller->ShowResultWidget();
        return;
    }

    PlayerCharacter->UnCrouch();
    ClearTrainingCameraFocus(true);
    TargetDistance = Spline->GetDistanceAlongSplineAtSplinePoint(TargetPointIndex);
    // Training owns its narration turns. Combat rails first face the outgoing route in place.
    if (!RailPath->bExternallyControlled)
    {
        CurrentStopYaw=FRotator::NormalizeAxis(PlayerCharacter->GetActorRotation().Yaw);
        const FVector Direction=Spline->GetDirectionAtDistanceAlongSpline(CurrentDistance,ESplineCoordinateSpace::World);
        TargetStopRotation=FRotator(0,Direction.SizeSquared2D()<=SMALL_NUMBER ? CurrentStopYaw : Direction.Rotation().Yaw,0);
        if (!IsStopRotationComplete())
        {
            RailState=EOBRailState::AligningForTravel;
            RailVelocity=FVector::ZeroVector;
            UE_LOG(LogOBPlayerSpline,Display,TEXT("Travel Alignment Started | Point=%d | CurrentYaw=%.2f | TargetYaw=%.2f"),
                CurrentPointIndex,CurrentStopYaw,TargetStopRotation.Yaw);
            return;
        }
        ApplyPlayerYaw(TargetStopRotation.Yaw);
    }
    BeginTravel();
}

void UOBPlayerSplineComponent::BeginTravel()
{
    RailState = EOBRailState::Moving;
    if (UsesFirstPersonTravelCamera())
    {
        PlayerCamera->SetRelativeLocation(FirstPersonCameraLocation);
        PlayerCamera->SetRelativeRotation(FirstPersonCameraRotation);
    }
    UE_LOG(LogOBPlayerSpline,Display,TEXT("Travel Started | From=%d | To=%d | Yaw=%.2f"),
        CurrentPointIndex,TargetPointIndex,PlayerCharacter->GetActorRotation().Yaw);
    OnTravelStarted.Broadcast();
}

void UOBPlayerSplineComponent::StopTrainingMovement()
{
    if (!RailPath || !RailPath->bExternallyControlled) return;
    ClearTrainingCameraFocus(true);
    RailState = EOBRailState::Action;
    TargetPointIndex = INDEX_NONE;
    TargetDistance = CurrentDistance;
}

bool UOBPlayerSplineComponent::JumpToTrainingPointForQA(int32 PointIndex, float YawOffset, FString& Error)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    if (!PlayerCharacter || !PlayerCamera || !RailPath || !RailPath->bExternallyControlled ||
        RailPath->bPreviewEntireRoute || PointIndex<0 || PointIndex>=RailPath->GetSpline()->GetNumberOfSplinePoints())
    { Error=TEXT("Training player, camera or external rail point is not ready."); return false; }
    ClearTrainingCameraFocus(true);
    PlayerCharacter->GetCharacterMovement()->StopMovementImmediately();
    CurrentPointIndex=PointIndex; TargetPointIndex=INDEX_NONE;
    NextSplineCuePoint=PointIndex+1;
    CurrentDistance=TargetDistance=RailPath->GetSpline()->GetDistanceAlongSplineAtSplinePoint(PointIndex);
    RailVelocity=FVector::ZeroVector;
    RailState=EOBRailState::Action;
    ApplySplineTransform(CurrentDistance);
    CurrentStopYaw=PlayerCharacter->GetActorRotation().Yaw+YawOffset;
    TargetStopRotation=FRotator(0,CurrentStopYaw,0);
    ApplyPlayerYaw(CurrentStopYaw);
    PlayerCamera->SetRelativeLocation(FirstPersonCameraLocation);
    PlayerCamera->SetRelativeRotation(GetStopCameraRotation());
    PlayerCamera->SetFieldOfView(GetStopCameraFOV());
    if (auto* PC=Cast<APlayerController>(PlayerCharacter->GetController())) PC->SetViewTarget(PlayerCharacter);
    return true;
#else
    Error=TEXT("QA is disabled in this build."); return false;
#endif
}

bool UOBPlayerSplineComponent::JumpToStagePointForQA(int32 PointIndex,FString& Error)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    return StartAtStageStop(PointIndex,Error);
#else
    Error=TEXT("QA is disabled in Shipping and Test builds.");return false;
#endif
}

bool UOBPlayerSplineComponent::StartAtStageStop(int32 PointIndex,FString& Error)
{
    if (!PlayerCharacter || !PlayerCamera || !RailPath || !PlayerCharacter->GetStageFlowComponent()->IsStageActive()
        || RailPath->bExternallyControlled || RailPath->bPreviewEntireRoute || bQACameraTuning
        || PointIndex<1 || PointIndex>=RailPath->GetSpline()->GetNumberOfSplinePoints() || !RailPath->FindStopSettings(PointIndex))
    { Error=TEXT("Stage_01 player/rail or requested stop is not ready.");return false; }
    ClearTrainingCameraFocus(true);
    PlayerCharacter->UnCrouch();
    PlayerCharacter->GetCharacterMovement()->UnCrouch(false);
    PlayerCharacter->GetCharacterMovement()->StopMovementImmediately();
    CurrentPointIndex=PointIndex;TargetPointIndex=INDEX_NONE;NextSplineCuePoint=PointIndex+1;
    CurrentDistance=TargetDistance=RailPath->GetSpline()->GetDistanceAlongSplineAtSplinePoint(PointIndex);
    RailVelocity=FVector::ZeroVector;bStageMovementBlocked=false;
    ApplySplineTransform(CurrentDistance);
    if (auto* PC=Cast<AOBPlayerController>(PlayerCharacter->GetController()))
    { PC->ResetQAInputState(false);PC->SetViewTarget(PlayerCharacter); }
    BeginStopAlignment(PointIndex);BeginAction();
    PlayerCamera->SetFieldOfView(GetStopCameraFOV());
    return true;
}

bool UOBPlayerSplineComponent::TurnTrainingToYaw(float WorldYaw)
{
    if (!RailPath || !RailPath->bExternallyControlled || RailState != EOBRailState::Action || !PlayerCharacter) return false;
    ClearTrainingCameraFocus(true);
    CurrentStopYaw = PlayerCharacter->GetActorRotation().Yaw;
    TargetStopRotation = FRotator(0, FRotator::NormalizeAxis(WorldYaw), 0);
    RailState = EOBRailState::TurningAtTrainingStop;
    return true;
}

bool UOBPlayerSplineComponent::SnapTrainingToYaw(float WorldYaw)
{
    if (!RailPath || !RailPath->bExternallyControlled || RailState != EOBRailState::Action || !PlayerCharacter) return false;
    ClearTrainingCameraFocus(true);
    CurrentStopYaw = FRotator::NormalizeAxis(WorldYaw);
    TargetStopRotation = FRotator(0, CurrentStopYaw, 0);
    ApplyPlayerYaw(CurrentStopYaw);
    if (PlayerCamera)
    {
        PlayerCamera->SetRelativeLocation(FirstPersonCameraLocation);
        PlayerCamera->SetRelativeRotation(GetStopCameraRotation());
    }
    return true;
}

void UOBPlayerSplineComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (bStageMovementBlocked)
    {
        RailVelocity=FVector::ZeroVector;
        // Narration/breach holds freeze travel, while the authored stop lens still settles.
        if (RailState==EOBRailState::Action) UpdateCameraFOV(DeltaTime);
        return;
    }

    if (IsRoutePreview())
    {
        UpdateCamera(DeltaTime);
        UpdateCameraFOV(DeltaTime);
        const FVector Before=PlayerCharacter->GetActorLocation();
        if (RailState == EOBRailState::RoutePreview) UpdateRoutePreview(DeltaTime);
        RailVelocity=RailState == EOBRailState::RoutePreview && DeltaTime>SMALL_NUMBER
            ? (PlayerCharacter->GetActorLocation()-Before)/DeltaTime : FVector::ZeroVector;
        return;
    }

    // FOV continues blending during Action; camera location stays under crouch control.
    UpdateTrainingCameraFocus(DeltaTime);
    UpdateCameraFOV(DeltaTime);
    if (RailState == EOBRailState::Action) { RailVelocity=FVector::ZeroVector; return; }

    UpdateCamera(DeltaTime);

    if (RailState == EOBRailState::Moving)
    {
        const FVector Before=PlayerCharacter->GetActorLocation();
        UpdateMovement(DeltaTime);
        RailVelocity=RailState == EOBRailState::Moving && DeltaTime>SMALL_NUMBER
            ? (PlayerCharacter->GetActorLocation()-Before)/DeltaTime : FVector::ZeroVector;
        return;
    }
    RailVelocity=FVector::ZeroVector;

    UpdateStopRotation(DeltaTime);
    if (RailState == EOBRailState::AligningForTravel)
    {
        if (IsStopRotationComplete())
        {
            ApplyPlayerYaw(TargetStopRotation.Yaw);
            BeginTravel();
        }
        return;
    }
    if (RailState == EOBRailState::TurningAtTrainingStop)
    {
        if (IsStopRotationComplete())
        {
            ApplyPlayerYaw(TargetStopRotation.Yaw);
            RailState = EOBRailState::Action;
            OnTrainingTurnCompleted.Broadcast();
        }
        return;
    }
    if (IsStopRotationComplete()) BeginAction();
}

bool UOBPlayerSplineComponent::IsRoutePreview() const
{
    return RailPath && RailPath->bPreviewEntireRoute;
}

void UOBPlayerSplineComponent::StartRoutePreview()
{
    USplineComponent* Spline = RailPath->GetSpline();
    if (Spline->GetNumberOfSplinePoints() < 2 || Spline->GetSplineLength() <= KINDA_SMALL_NUMBER)
    {
        UE_LOG(LogOBPlayerSpline, Error, TEXT("Route preview requires a non-empty spline | Rail=%s"), *GetNameSafe(RailPath));
        SetComponentTickEnabled(false);
        return;
    }
    PlayerCharacter->UnCrouch();
    // Spline owns XYZ, including stairs/slopes: walking gravity/floor snapping must not fight it.
    PlayerCharacter->GetCharacterMovement()->StopMovementImmediately();
    PlayerCharacter->GetCharacterMovement()->DisableMovement();
    if (UOBMediaPipeInputComponent* PoseInput = PlayerCharacter->FindComponentByClass<UOBMediaPipeInputComponent>())
        PoseInput->SetPoseInputEnabled(false);
    PlayerCharacter->GetActionComponent()->SetExternalActionState(false);
    if (APlayerController* Controller = Cast<APlayerController>(PlayerCharacter->GetController()))
    {
        Controller->SetIgnoreMoveInput(true);
        Controller->SetIgnoreLookInput(true);
        Controller->SetViewTarget(PlayerCharacter);
    }
    CurrentPointIndex = 0;
    NextSplineCuePoint = 1;
    CurrentDistance = 0.f;
    TargetPointIndex = Spline->GetNumberOfSplinePoints() - 1;
    TargetDistance = Spline->GetSplineLength();
    RailState = EOBRailState::RoutePreview;
    ApplySplineTransform(CurrentDistance);
    const bool bFirstPersonTravel=UsesFirstPersonTravelCamera();
    PlayerCamera->SetRelativeLocation(bFirstPersonTravel ? FirstPersonCameraLocation : TravelCameraRelativeLocation);
    PlayerCamera->SetRelativeRotation(bFirstPersonTravel ? FirstPersonCameraRotation : TravelCameraRelativeRotation);
    PlayerCamera->SetFieldOfView(FMath::Clamp(TravelFOV, 5.f, 170.f));
    UE_LOG(LogOBPlayerSpline, Display, TEXT("Route Preview Started | Rail=%s | Length=%.2f | Speed=%.2f"),
        *GetNameSafe(RailPath), TargetDistance, RailPath->RoutePreviewSpeed);
}

void UOBPlayerSplineComponent::UpdateRoutePreview(float DeltaTime)
{
    CurrentDistance = FMath::Min(CurrentDistance + FMath::Max(1.f, RailPath->RoutePreviewSpeed)
        * FMath::Max(0.f, DeltaTime), TargetDistance);
    // Spline points describe the feet. Keep the capsule upright and offset its center vertically.
    ApplySplineTransform(CurrentDistance);
    NotifySplinePointsReached();
    if (CurrentDistance < TargetDistance) return;
    CurrentPointIndex = TargetPointIndex;
    TargetPointIndex = INDEX_NONE;
    RailState = EOBRailState::RoutePreviewComplete;
    UE_LOG(LogOBPlayerSpline, Display, TEXT("Route Preview Completed | Point=%d | Distance=%.2f | Location=%s"),
        CurrentPointIndex, CurrentDistance, *PlayerCharacter->GetActorLocation().ToCompactString());
    OnRoutePreviewCompleted.Broadcast();
}

void UOBPlayerSplineComponent::UpdateMovement(float DeltaTime)
{
    const float PreviousDistance=CurrentDistance;
    const float Speed=PlayerCharacter->GetStageFlowComponent()->GetTravelMoveSpeed(MoveSpeed);
    float ProposedDistance=FMath::Min(CurrentDistance + Speed * DeltaTime, TargetDistance);
    for (TActorIterator<AOBSquadRendezvous> It(GetWorld());It;++It)
        ProposedDistance=It->ConstrainPlayerDistance(PlayerCharacter,ProposedDistance);
    // The authored spline's distance table is approximate. During the slower
    // breach entry, also bound world-space travel so short curved segments
    // cannot exceed the requested entry speed.
    if (Speed<MoveSpeed && ProposedDistance>CurrentDistance && DeltaTime>SMALL_NUMBER)
    {
        const auto* Spline=RailPath->GetSpline();
        const FVector From=Spline->GetLocationAtDistanceAlongSpline(CurrentDistance,ESplineCoordinateSpace::World);
        const float BudgetSquared=FMath::Square(Speed*DeltaTime);
        if (FVector::DistSquared(From,Spline->GetLocationAtDistanceAlongSpline(ProposedDistance,ESplineCoordinateSpace::World))>BudgetSquared)
        {
            float Low=CurrentDistance,High=ProposedDistance;
            for (int32 Iteration=0;Iteration<12;++Iteration)
            {
                const float Mid=(Low+High)*.5f;
                if (FVector::DistSquared(From,Spline->GetLocationAtDistanceAlongSpline(Mid,ESplineCoordinateSpace::World))<=BudgetSquared) Low=Mid;
                else High=Mid;
            }
            ProposedDistance=Low;
        }
    }
    CurrentDistance=ProposedDistance;

    // Use progress after squad pacing, so waiting for allies does not start an arrival line early.
    const float Progress=CurrentDistance-PreviousDistance;
    if (Progress>KINDA_SMALL_NUMBER && DeltaTime>SMALL_NUMBER)
        if (auto* Stage=PlayerCharacter->GetStageFlowComponent())
            Stage->HandleApproachToStop(TargetPointIndex,FMath::Max(0.f,TargetDistance-CurrentDistance)*DeltaTime/Progress);

    if (CurrentDistance < TargetDistance)
    {
        ApplySplineTransform(CurrentDistance);
        NotifySplinePointsReached();
        return;
    }

    ApplySplineLocation(CurrentDistance);
    NotifySplinePointsReached();

    BeginStopAlignment(TargetPointIndex);
}

void UOBPlayerSplineComponent::BeginStopAlignment(int32 ReachedPointIndex)
{
    CurrentPointIndex = ReachedPointIndex;
    TargetPointIndex = INDEX_NONE;

    CurrentStopYaw = FRotator::NormalizeAxis(PlayerCharacter->GetActorRotation().Yaw);
    const FOBRailStopSettings* StopSettings = RailPath->FindStopSettings(ReachedPointIndex);
    const bool bHasFacingOverride = StopSettings && StopSettings->bOverrideFacingYaw;
    float TargetFacingYaw = bHasFacingOverride ? StopSettings->FacingYaw : CurrentStopYaw;
    bool bFacingEncounter=false;
    if (!bHasFacingOverride && !RailPath->bExternallyControlled)
    {
        for (TActorIterator<AOBEncounterTrigger> It(GetWorld());It;++It)
        {
            FVector Focus;
            if (It->GetPointIndex()!=ReachedPointIndex || !It->GetCombatFacingLocation(Focus)) continue;
            const FVector ToEncounter=Focus-PlayerCharacter->GetActorLocation();
            if (ToEncounter.SizeSquared2D()>SMALL_NUMBER)
            {
                TargetFacingYaw=ToEncounter.Rotation().Yaw;
                bFacingEncounter=true;
            }
            break;
        }
    }
    TargetStopRotation = FRotator(0.0f, FRotator::NormalizeAxis(TargetFacingYaw), 0.0f);

    RailState = EOBRailState::AligningAtStop;
    PlayerCharacter->GetStageFlowComponent()->PrepareEncounterAtStop(ReachedPointIndex);
    UE_LOG(LogOBPlayerSpline, Display,
        TEXT("Stop Alignment Started | Point=%d | CurrentYaw=%.2f | TargetYaw=%.2f | Override=%s | Encounter=%s"),
        ReachedPointIndex, CurrentStopYaw, TargetStopRotation.Yaw,
        bHasFacingOverride ? TEXT("True") : TEXT("False"),bFacingEncounter ? TEXT("True") : TEXT("False"));
}

void UOBPlayerSplineComponent::NotifySplinePointsReached()
{
    const auto* Spline = RailPath->GetSpline();
    while (NextSplineCuePoint < Spline->GetNumberOfSplinePoints()
        && CurrentDistance + KINDA_SMALL_NUMBER >= Spline->GetDistanceAlongSplineAtSplinePoint(NextSplineCuePoint))
    {
        const int32 ReachedPoint = NextSplineCuePoint++;
        OnSplinePointReached.Broadcast(ReachedPoint);
    }
}

bool UOBPlayerSplineComponent::UsesFirstPersonTravelCamera() const
{
    // Remove the PIE prefix as well, so editor, standalone and packaged Stage_01 agree.
    return GetWorld() && UGameplayStatics::GetCurrentLevelName(this,true)==TEXT("Stage_01");
}

float UOBPlayerSplineComponent::GetStopCameraFOV() const
{
    if (bQACameraTuning)
        if (const auto* View=QACameraViews.Find(CurrentPointIndex)) return float(View->X);
    const auto* Stop=RailPath ? RailPath->FindStopSettings(CurrentPointIndex) : nullptr;
    const float FOV=Stop && Stop->bOverrideCameraView && FMath::IsFinite(Stop->CameraFOV) ? Stop->CameraFOV : ActionFOV;
    return FMath::Clamp(FOV,5.f,170.f);
}

FRotator UOBPlayerSplineComponent::GetStopCameraRotation() const
{
    if (bQACameraTuning)
        if (const auto* View=QACameraViews.Find(CurrentPointIndex))
            return (FirstPersonCameraRotation+FRotator(View->Y,View->Z,View->W)).GetNormalized();
    const auto* Stop=RailPath ? RailPath->FindStopSettings(CurrentPointIndex) : nullptr;
    return Stop && Stop->bOverrideCameraView && !Stop->CameraRotationOffset.ContainsNaN()
        ? (FirstPersonCameraRotation+Stop->CameraRotationOffset).GetNormalized() : FirstPersonCameraRotation;
}

void UOBPlayerSplineComponent::UpdateCamera(float DeltaTime)
{
    const bool bTravelCamera = RailState == EOBRailState::Moving
        || RailState == EOBRailState::AligningForTravel
        || RailState == EOBRailState::RoutePreview || RailState == EOBRailState::RoutePreviewComplete;
    const bool bThirdPersonTravel=bTravelCamera && !UsesFirstPersonTravelCamera();
    const FVector TargetLocation = bThirdPersonTravel ? TravelCameraRelativeLocation : FirstPersonCameraLocation;
    const FRotator TargetRotation = bTravelCamera
        ? (bThirdPersonTravel ? TravelCameraRelativeRotation : FirstPersonCameraRotation) : GetStopCameraRotation();
    PlayerCamera->SetRelativeLocation(FMath::VInterpTo(
        PlayerCamera->GetRelativeLocation(), TargetLocation, DeltaTime, CameraInterpSpeed));
    PlayerCamera->SetRelativeRotation(FMath::RInterpTo(
        PlayerCamera->GetRelativeRotation(), TargetRotation, DeltaTime, CameraInterpSpeed));
}

void UOBPlayerSplineComponent::UpdateCameraFOV(float DeltaTime)
{
    if (!IsValid(PlayerCamera)) return;
    if (IsTrainingCameraFocusActive()) return;

    const float TargetFOV = FMath::Clamp(
        RailState == EOBRailState::Action ? GetStopCameraFOV() : TravelFOV, 5.0f, 170.0f);
    const float Alpha = 1.0f - FMath::Exp(-FMath::Max(FOVInterpSpeed, 0.1f) * FMath::Max(DeltaTime, 0.0f));
    const float BlendedFOV = FMath::Lerp(PlayerCamera->FieldOfView, TargetFOV, Alpha);
    PlayerCamera->SetFieldOfView(FMath::IsNearlyEqual(BlendedFOV, TargetFOV, 0.01f) ? TargetFOV : BlendedFOV);
}

void UOBPlayerSplineComponent::FocusTrainingCamera(float PitchOffset, float FocusFOV, float BlendSeconds, float YawOffset)
{
    if (!IsValid(PlayerCamera) || RailState != EOBRailState::Action) return;
    if (!IsTrainingCameraFocusActive()) TrainingFocusBaseRotation = PlayerCamera->GetRelativeRotation();
    TrainingFocusPitch = FMath::Clamp(PitchOffset, -80.f, 80.f);
    TrainingFocusYaw = FMath::Clamp(YawOffset, -80.f, 80.f);
    TrainingFocusFOV = FMath::Clamp(FocusFOV, 5.f, 170.f);
    TrainingFocusBlendSeconds = FMath::Max(.01f, BlendSeconds);
    TrainingFocusStartWeight = TrainingFocusWeight;
    TrainingFocusStartFOV = PlayerCamera->FieldOfView;
    TrainingFocusElapsed = 0;
    bTrainingFocus = bTrainingFocusBlending = true;
}

void UOBPlayerSplineComponent::ClearTrainingCameraFocus(bool bImmediate)
{
    if (!IsTrainingCameraFocusActive() || !IsValid(PlayerCamera)) return;
    bTrainingFocus = false;
    if (bImmediate)
    {
        bTrainingFocusBlending = false;
        TrainingFocusWeight = 0;
        PlayerCamera->SetRelativeRotation(TrainingFocusBaseRotation);
        PlayerCamera->SetFieldOfView(GetStopCameraFOV());
        return;
    }
    TrainingFocusStartWeight = TrainingFocusWeight;
    TrainingFocusStartFOV = PlayerCamera->FieldOfView;
    TrainingFocusElapsed = 0;
    bTrainingFocusBlending = true;
}

void UOBPlayerSplineComponent::UpdateTrainingCameraFocus(float DeltaTime)
{
    if (!IsValid(PlayerCamera) || !IsTrainingCameraFocusActive()) return;
    TrainingFocusElapsed += FMath::Max(0.f, DeltaTime);
    const float Progress = FMath::Clamp(TrainingFocusElapsed / TrainingFocusBlendSeconds, 0.f, 1.f);
    const float Alpha = FMath::SmoothStep(0.f, 1.f, Progress);
    TrainingFocusWeight = FMath::Lerp(TrainingFocusStartWeight, bTrainingFocus ? 1.f : 0.f, Alpha);
    FRotator Rotation = TrainingFocusBaseRotation;
    Rotation.Pitch += TrainingFocusPitch * TrainingFocusWeight;
    Rotation.Yaw += TrainingFocusYaw * TrainingFocusWeight;
    // Leave camera position to posture control; only the inspection pitch and FOV change.
    PlayerCamera->SetRelativeRotation(Rotation);
    PlayerCamera->SetFieldOfView(FMath::Lerp(TrainingFocusStartFOV,
        bTrainingFocus ? TrainingFocusFOV : GetStopCameraFOV(), Alpha));
    bTrainingFocusBlending = Progress < 1.f;
}

void UOBPlayerSplineComponent::UpdateStopRotation(float DeltaTime)
{
    const float EffectiveTurnSpeed = StopTurnSpeed > KINDA_SMALL_NUMBER ? StopTurnSpeed : 180.0f;
    CurrentStopYaw = FMath::FixedTurn(CurrentStopYaw, TargetStopRotation.Yaw, EffectiveTurnSpeed * DeltaTime);
    ApplyPlayerYaw(CurrentStopYaw);
}

void UOBPlayerSplineComponent::ApplySplineTransform(float Distance)
{
    USplineComponent* Spline = RailPath->GetSpline();

    FVector Location = Spline->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
    const FVector Direction = Spline->GetDirectionAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
    Location.Z += PlayerCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    PlayerCharacter->SetActorLocation(Location);
    ApplyPlayerYaw(Direction.Rotation().Yaw);
}

void UOBPlayerSplineComponent::ApplySplineLocation(float Distance)
{
    FVector Location = RailPath->GetSpline()->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
    Location.Z += PlayerCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    PlayerCharacter->SetActorLocation(Location);
}

void UOBPlayerSplineComponent::ApplyPlayerYaw(float WorldYaw)
{
    const float NormalizedYaw = FRotator::NormalizeAxis(WorldYaw);
    PlayerCharacter->SetActorRotation(FRotator(0.0f, NormalizedYaw, 0.0f));

    if (AController* Controller = PlayerCharacter->GetController())
    {
        FRotator ControlRotation = Controller->GetControlRotation();
        ControlRotation.Yaw = NormalizedYaw;
        Controller->SetControlRotation(ControlRotation);
    }
}

bool UOBPlayerSplineComponent::IsStopRotationComplete() const
{
    const float YawDifference = FMath::Abs(FMath::FindDeltaAngleDegrees(CurrentStopYaw, TargetStopRotation.Yaw));
    return YawDifference <= StopRotationTolerance;
}

void UOBPlayerSplineComponent::BeginAction()
{
    CurrentStopYaw = TargetStopRotation.Yaw;
    ApplyPlayerYaw(CurrentStopYaw);
    PlayerCamera->SetRelativeLocation(FirstPersonCameraLocation);
    PlayerCamera->SetRelativeRotation(GetStopCameraRotation());

    const FOBRailStopSettings* StopSettings = RailPath->FindStopSettings(CurrentPointIndex);
    if (AOBPlayerController* Controller = Cast<AOBPlayerController>(PlayerCharacter->GetController()))
    {
        Controller->SetCrouchMode(StopSettings && StopSettings->bIsCrouchMode);
        if (!bQACameraTuning && !PlayerCharacter->GetStageFlowComponent()->IsStageActive() && !RailPath->bExternallyControlled && CurrentPointIndex == RailPath->GetSpline()->GetNumberOfSplinePoints() - 1
            && !PlayerCharacter->GetActionComponent()->HasEncounterEnemies()) Controller->ShowResultWidget();
    }

    RailState = EOBRailState::Action;
    UE_LOG(LogOBPlayerSpline, Display,
        TEXT("Stop Alignment Completed | Point=%d | FinalYaw=%.2f"),
        CurrentPointIndex, TargetStopRotation.Yaw);
    OnActionPointReached.Broadcast(CurrentPointIndex);
}

bool UOBPlayerSplineComponent::EnterCameraTuningForQA(int32 PointIndex,FString& Error)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    if (!UsesFirstPersonTravelCamera() || !PlayerCharacter || !PlayerCamera || !RailPath
        || RailPath->bExternallyControlled || RailPath->bPreviewEntireRoute
        || PointIndex<1 || PointIndex>=RailPath->GetSpline()->GetNumberOfSplinePoints() || !RailPath->FindStopSettings(PointIndex))
    { Error=TEXT("Stage_01 player/rail is not ready, stop is missing, or Preview Entire Route is enabled.");return false; }
    if (UGameplayStatics::IsGamePaused(this)) { Error=TEXT("Resume the game before QA Zone.");return false; }
    AOBEncounterTrigger* Encounter=nullptr;
    for (TActorIterator<AOBEncounterTrigger> It(GetWorld());It;++It) if (It->GetPointIndex()==PointIndex) { Encounter=*It;break; }
    if (!Encounter) { Error=TEXT("This stop has no encounter trigger.");return false; }
    auto* Movement=PlayerCharacter->GetCharacterMovement();
    auto* Action=PlayerCharacter->GetActionComponent();
    PlayerCharacter->GetStageFlowComponent()->SuspendForQA();
    if (!bQACameraTuning)
    {
        QASavedMovementMode=Movement->MovementMode;QASavedCustomMode=Movement->CustomMovementMode;
        bQASavedDamageable=PlayerCharacter->CanBeDamaged();bQASavedAutoAdvance=Action->bAutoAdvanceOnEnemyClear;
    }
    if (!QACameraViews.Contains(PointIndex))
    {
        const auto* Stop=RailPath->FindStopSettings(PointIndex);
        const float FOV=Stop->bOverrideCameraView && FMath::IsFinite(Stop->CameraFOV) ? Stop->CameraFOV : ActionFOV;
        const FRotator Offset=Stop->bOverrideCameraView && !Stop->CameraRotationOffset.ContainsNaN() ? Stop->CameraRotationOffset : FRotator::ZeroRotator;
        QACameraViews.Add(PointIndex,FVector4(FMath::Clamp(FOV,5.f,170.f),Offset.Pitch,Offset.Yaw,Offset.Roll));
    }
    bQACameraTuning=true;bQACameraEditing=false;Action->bAutoAdvanceOnEnemyClear=false;PlayerCharacter->SetCanBeDamaged(false);
    Action->ResetEncounterForQA();
    if (QACameraEncounter.IsValid()) QACameraEncounter->ResetForQA();
    if (QACameraEncounter.Get()!=Encounter) Encounter->ResetForQA();
    QACameraEncounter=Encounter;
    ClearTrainingCameraFocus(true);
    PlayerCharacter->UnCrouch();Movement->UnCrouch(false);
    Movement->StopMovementImmediately();Movement->DisableMovement();
    CurrentPointIndex=PointIndex;TargetPointIndex=INDEX_NONE;NextSplineCuePoint=PointIndex+1;
    CurrentDistance=TargetDistance=RailPath->GetSpline()->GetDistanceAlongSplineAtSplinePoint(PointIndex);
    RailVelocity=FVector::ZeroVector;
    ApplySplineTransform(CurrentDistance);
    Encounter->ActivateForPlayer(PlayerCharacter);
    BeginStopAlignment(PointIndex);BeginAction();
    PlayerCamera->SetFieldOfView(GetStopCameraFOV());
    return true;
#else
    Error=TEXT("Camera tuning is disabled in Shipping and Test builds.");return false;
#endif
}

bool UOBPlayerSplineComponent::StartCameraTuningForQA()
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    if (!bQACameraTuning || !QACameraViews.Contains(CurrentPointIndex)) return false;
    bQACameraEditing=true;return true;
#else
    return false;
#endif
}

bool UOBPlayerSplineComponent::AdjustCameraForQA(float FOVDelta,float PitchDelta,float YawDelta,float RollDelta)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    auto* View=IsCameraEditingForQA() ? QACameraViews.Find(CurrentPointIndex) : nullptr;
    if (!View || !PlayerCamera || !FMath::IsFinite(FOVDelta) || !FMath::IsFinite(PitchDelta)
        || !FMath::IsFinite(YawDelta) || !FMath::IsFinite(RollDelta)) return false;
    View->X=FMath::Clamp(View->X+FOVDelta,5.0,170.0);
    View->Y=FMath::Clamp(View->Y+PitchDelta,-85.0,85.0);
    View->Z=FRotator::NormalizeAxis(float(View->Z+YawDelta));
    View->W=FRotator::NormalizeAxis(float(View->W+RollDelta));
    PlayerCamera->SetFieldOfView(GetStopCameraFOV());PlayerCamera->SetRelativeRotation(GetStopCameraRotation());
    return true;
#else
    return false;
#endif
}

FString UOBPlayerSplineComponent::GetCameraTuningStatusForQA() const
{
    const auto* View=bQACameraTuning ? QACameraViews.Find(CurrentPointIndex) : nullptr;
    if (!View) return TEXT("QA Camera: inactive. Use QA Zone <1~6>.");
    const int32 Points[]={7,19,44,64,72,86};int32 Zone=0;
    for (int32 I=0;I<6;++I) if (Points[I]==CurrentPointIndex) Zone=I+1;
    return FString::Printf(TEXT("QA CAMERA Zone=%d Point=%d | bOverrideCameraView=True CameraFOV=%.2f CameraRotationOffset=(Pitch=%.2f,Yaw=%.2f,Roll=%.2f) | Adjust=%s"),
        Zone,CurrentPointIndex,View->X,View->Y,View->Z,View->W,bQACameraEditing ? TEXT("On") : TEXT("Off (QA Camera Start)"));
}

void UOBPlayerSplineComponent::EndCameraTuningForQA()
{
    if (!bQACameraTuning) return;
    bQACameraEditing=false;
    if (IsValid(PlayerCharacter)) PlayerCharacter->GetActionComponent()->ResetEncounterForQA();
    if (QACameraEncounter.IsValid()) QACameraEncounter->ResetForQA();
    QACameraEncounter.Reset();bQACameraTuning=false;
    if (!IsValid(PlayerCharacter)) return;
    auto* Action=PlayerCharacter->GetActionComponent();Action->bAutoAdvanceOnEnemyClear=bQASavedAutoAdvance;
    PlayerCharacter->SetCanBeDamaged(bQASavedDamageable);
    auto* Movement=PlayerCharacter->GetCharacterMovement();
    Movement->SetMovementMode(static_cast<EMovementMode>(QASavedMovementMode),QASavedCustomMode);
    if (PlayerCamera) { PlayerCamera->SetRelativeRotation(GetStopCameraRotation());PlayerCamera->SetFieldOfView(GetStopCameraFOV()); }
}

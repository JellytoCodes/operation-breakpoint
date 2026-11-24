#include "Stage/OBStageFlowComponent.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "World/OBBreachCharge.h"
#include "World/OBBreachDoor.h"
#include "World/OBRailPath.h"
#include "Components/SplineComponent.h"
#include "Components/StaticMeshComponent.h"
#include "MediaPipe/OBMediaPipeOSCSubsystem.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

void UOBStageFlowComponent::BeginC4Placement()
{
    if (IsValid(BreachCharge)) BreachCharge->Destroy();
    BreachCharge=nullptr;C4Placement.Reset(FPlatformTime::Seconds());
    if (!Player || !IsValid(ActiveBreachDoor)) return;
    FActorSpawnParameters Spawn;Spawn.Owner=Player;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    BreachCharge=GetWorld()->SpawnActor<AOBBreachCharge>(AOBBreachCharge::StaticClass(),Player->GetActorTransform(),Spawn);
}
void UOBStageFlowComponent::TickC4Placement()
{
    if (bBreachRequested || !Player || !IsValid(BreachCharge)) return;
    const auto* Motion=MotionInput.Get();
    const auto State=Motion ? Motion->GetBreachGuideState() : FOBBreachGuideState();
    const bool Placed=C4Placement.Update(State,Motion ? Motion->GetBreachGuideSampleTime() : 0.);
    if (!C4Placement.bTracked) { BreachCharge->SetActorHiddenInGame(true);return; }
    auto* PC=Cast<APlayerController>(Player->GetController());int32 W=0,H=0;if (PC) PC->GetViewportSize(W,H);
    const FBox2D Frame=FOBBreachGuideLayout::CameraRect(FVector2D(W,H),State.CameraSize);
    if (!BreachCharge->FollowScreenPoint(PC,FOBBreachGuideLayout::MapPoint(C4Placement.HandUV,Frame)))
    { BreachCharge->SetActorHiddenInGame(true);C4Placement.Reset(FPlatformTime::Seconds());return; }
    if (Placed && !CompleteC4Placement()) C4Placement.Reset(FPlatformTime::Seconds());
}
bool UOBStageFlowComponent::CompleteC4Placement()
{
    if (!bActive || Phase!=EOBStagePhase::Zone6Breach || bBreachRequested || !Player || !IsValid(BreachCharge)) return false;
    auto* PC=Cast<APlayerController>(Player->GetController());
    if (!PC || !PC->PlayerCameraManager) return false;
    int32 W=0,H=0;PC->GetViewportSize(W,H);
    const auto State=MotionInput.IsValid() ? MotionInput->GetBreachGuideState() : FOBBreachGuideState();
    const FBox2D Frame=FOBBreachGuideLayout::CameraRect(FVector2D(W,H),State.CameraSize);
    if (!BreachCharge->AttachToDoor(ActiveBreachDoor,PC,FOBBreachGuideLayout::MapPoint(C4Placement.GetTargetCenter(),Frame))) return false;
    C4Placement.bAttached=true;C4Placement.Progress=1;bBreachRequested=true;bBreachCompleted=false;
    bBreachPhysicsStarted=bBreachBlackHoldStarted=false;BreachBlackFrame=MAX_uint64;
    BreachFadeOutRemaining=FMath::Max(0.f,BreachFadeOutSeconds);BreachBlackRemaining=0;
    BreachCamera=PC->PlayerCameraManager;
    BreachCamera->StartCameraFade(0.f,1.f,BreachFadeOutRemaining,FLinearColor::Black,false,true);
    RefreshHUD();
    UE_LOG(LogTemp,Display,TEXT("C4 attached | Hand=%d | Door intact until a fully black frame"),C4Placement.Hand);
    return true;
}
FVector UOBStageFlowComponent::GetBreachDirection() const
{
    FVector Direction=ActiveBreachDoor->GetActorLocation()-Player->GetActorLocation();
    const auto* Rail=Player->GetPlayerSplineComponent()->GetRailPath();const auto* Spline=Rail ? Rail->GetSpline() : nullptr;
    if (Spline && Zone6EncounterPoint>=0 && Zone6BreachPoint>=0 && Zone6EncounterPoint<Spline->GetNumberOfSplinePoints() && Zone6BreachPoint<Spline->GetNumberOfSplinePoints())
        Direction=Spline->GetLocationAtSplinePoint(Zone6EncounterPoint,ESplineCoordinateSpace::World)-Spline->GetLocationAtSplinePoint(Zone6BreachPoint,ESplineCoordinateSpace::World);
    return Direction;
}
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
bool UOBStageFlowComponent::AttachBreachChargeForQA()
{
    StageRecord.bUsedTestInputs=true;return CompleteC4Placement();
}
#endif

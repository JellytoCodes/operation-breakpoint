#include "Training/OBRailTrainingTarget.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

AOBRailTrainingTarget::AOBRailTrainingTarget()
{
    RaiseSeconds = 0.f;
    LowerSeconds = 0.25f;
    RaisedRotation = FRotator::ZeroRotator;
    // The supplied board faces local +Y; +90 roll folds it backwards toward -Y.
    FoldRotation = FRotator(0,0,90);
    TargetPivot->SetRelativeLocation(CenterLocation);

    // Assign the stationary rail asset directly on the inherited BaseMesh root.
    // Never resize the root automatically: its scale also affects the moving target.
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (Cube.Succeeded())
    {
        ArmMesh->SetStaticMesh(Cube.Object);
    }
    ArmMesh->SetRelativeScale3D(FVector(0.4f, 0.4f, 0.12f));

    static ConstructorHelpers::FObjectFinder<UStaticMesh> FullTarget(TEXT(
        "/Game/Assets/MOUT_Fabrication/Meshes/MOUTFabrications/Props/SM_ShootingTarget/SM_ShootingTargetFull_01.SM_ShootingTargetFull_01"));
    BoardMesh->SetRelativeScale3D(FVector::OneVector);
    if (FullTarget.Succeeded())
    {
        BoardMesh->SetStaticMesh(FullTarget.Object);
        const FBox Bounds = FullTarget.Object->GetBoundingBox();
        const FVector BoardOffset(0, 0, 6.f - Bounds.Min.Z);
        BoardMesh->SetRelativeLocation(BoardOffset);
    }
}

FVector AOBRailTrainingTarget::GetTravelAxis() const
{
    const FVector Axis = MovementAxis.GetSafeNormal();
    return Axis.IsNearlyZero() ? FVector::ForwardVector : Axis;
}

void AOBRailTrainingTarget::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    if (!bMovementInitialized) CenterLocation = TargetPivot->GetRelativeLocation();
}

void AOBRailTrainingTarget::InitializeMovement()
{
    if (bMovementInitialized) return;
    CenterLocation = TargetPivot->GetRelativeLocation();
    bMovementInitialized = true;
    ResetMovement();
}

void AOBRailTrainingTarget::BeginPlay()
{
    InitializeMovement();
    Super::BeginPlay();
    if (bAutoStart) StartMoving();
    else RefreshTick();
}

bool AOBRailTrainingTarget::IsMoving() const
{
    return bMovementRequested && IsTargetRaised() && TravelDistance > KINDA_SMALL_NUMBER && MoveSpeed > 0.f;
}

void AOBRailTrainingTarget::RefreshTick()
{
    SetActorTickEnabled(IsPoseAnimating() || IsMoving());
}

void AOBRailTrainingTarget::StartMoving()
{
    InitializeMovement();
    bMovementRequested = true;
    RefreshTick();
}

void AOBRailTrainingTarget::StopMoving()
{
    bMovementRequested = false;
    RefreshTick();
}

void AOBRailTrainingTarget::ResetMovement()
{
    if (!bMovementInitialized)
    {
        CenterLocation = TargetPivot->GetRelativeLocation();
        bMovementInitialized = true;
    }
    const double Progress = FMath::Clamp(double(StartProgress), 0.0, 1.0);
    MovementPhase = bStartTowardPositive ? Progress : 2.0 - Progress;
    ApplyMovement();
    RefreshTick();
}

void AOBRailTrainingTarget::ApplyMovement()
{
    const double Progress = MovementPhase <= 1.0 ? MovementPhase : 2.0 - MovementPhase;
    TargetPivot->SetRelativeLocation(CenterLocation + GetTravelAxis() *
        ((Progress - 0.5) * FMath::Max(0.f, TravelDistance)));
}

void AOBRailTrainingTarget::SetTargetEnabled(bool bEnabled, bool bImmediate, bool bPlayLowerSound)
{
    if (bEnabled) bHitFolding=false;
    else if (bHitFolding && IsPoseAnimating()) bImmediate=false;
    Super::SetTargetEnabled(bEnabled, bImmediate, bPlayLowerSound);
    RefreshTick();
}
void AOBRailTrainingTarget::HandleScoringHit(const FVector& ImpactPoint)
{
    if (!IsTargetRaised()) return;
    StopMoving();
    bHitFolding=true;
    SetTargetEnabled(false, false, true);
    Super::HandleScoringHit(ImpactPoint);
}

void AOBRailTrainingTarget::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (IsMoving())
    {
        const double Distance = FMath::Max(double(TravelDistance), double(KINDA_SMALL_NUMBER));
        MovementPhase = FMath::Fmod(MovementPhase + FMath::Max(0.f, DeltaSeconds) * double(MoveSpeed) / Distance, 2.0);
        ApplyMovement();
    }
    RefreshTick();
}

#include "Training/OBTrainingTarget.h"
#include "Components/StaticMeshComponent.h"
#include "Core/OBCombatCollision.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "Components/DecalComponent.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
AOBTrainingTarget::AOBTrainingTarget()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
    TargetHitSound=TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Assets/FX/SFX/PlaySound/Target_hit.Target_hit")));
    TargetDownSound=TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Assets/FX/SFX/PlaySound/Target_down.Target_down")));
    BaseMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BaseMesh"));
    SetRootComponent(BaseMesh);
    BaseMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    // The base anchors the actor; only the pivot and its children fold.
    TargetPivot = CreateDefaultSubobject<USceneComponent>(TEXT("TargetPivot"));
    TargetPivot->SetupAttachment(BaseMesh);
    ArmMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ArmMesh"));
    ArmMesh->SetupAttachment(TargetPivot);
    ArmMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BoardMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BoardMesh"));
    BoardMesh->SetupAttachment(TargetPivot);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (Cube.Succeeded()) BoardMesh->SetStaticMesh(Cube.Object);
    BoardMesh->SetRelativeScale3D(FVector(0.1f, 1, 1));
    BoardMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    BoardMesh->SetCollisionObjectType(OBCombatCollision::PlayerShotTarget);
    BoardMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
    BoardMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    BoardMesh->SetCollisionResponseToChannel(OBCombatCollision::PlayerShotTarget, ECR_Block);
    BoardMesh->SetGenerateOverlapEvents(false);
    BulletHoleMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Blueprints/Training/Materials/MI_OBTrainingBulletHole.MI_OBTrainingBulletHole")));
}
void AOBTrainingTarget::BeginPlay()
{
    Super::BeginPlay();
    TargetHitSound.LoadSynchronous();
    TargetDownSound.LoadSynchronous();
}
void AOBTrainingTarget::PlayTargetSound(const TSoftObjectPtr<USoundBase>& Sound, const FVector& Location, const TCHAR* Event)
{
    if (auto* Asset=Sound.LoadSynchronous())
    {
        UGameplayStatics::PlaySoundAtLocation(this,Asset,Location,FMath::Max(0.f,SoundVolume));
        UE_LOG(LogTemp,Display,TEXT("TRAINING_AUDIO event=%s sound=%s target=%s"),Event,*Asset->GetName(),*GetName());
    }
}
void AOBTrainingTarget::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    ApplyPose();
}
bool AOBTrainingTarget::IsTargetRaised() const
{
    return bTargetEnabled && PoseProgress >= 1.f - KINDA_SMALL_NUMBER;
}
FVector AOBTrainingTarget::GetScoringPlaneNormal() const
{
    // Split boards face local Y; the native placeholder faces X. Use the board's thin axis.
    FVector Axis = FVector::ForwardVector;
    if (const UStaticMesh* Mesh = BoardMesh->GetStaticMesh())
    {
        const FVector Extent = Mesh->GetBoundingBox().GetExtent() * BoardMesh->GetComponentScale().GetAbs();
        if (Extent.Y < Extent.X && Extent.Y <= Extent.Z) Axis = FVector::RightVector;
        else if (Extent.Z < Extent.X && Extent.Z < Extent.Y) Axis = FVector::UpVector;
    }
    return BoardMesh->GetComponentTransform().TransformVectorNoScale(Axis).GetSafeNormal();
}
bool AOBTrainingTarget::IsScoringHit(const FHitResult& Hit) const
{
    return IsTargetRaised() && Hit.bBlockingHit && Hit.GetActor() == this && Hit.GetComponent() == BoardMesh;
}
void AOBTrainingTarget::HandleScoringHit(const FVector& ImpactPoint)
{
    PlayTargetSound(TargetHitSound,ImpactPoint,TEXT("TargetHit"));
    OnTargetHit(ImpactPoint);
}
UDecalComponent* AOBTrainingTarget::SpawnBulletHole(const FHitResult& Hit)
{
    if (!bShowBulletHoles || !Hit.bBlockingHit || Hit.GetActor()!=this || Hit.GetComponent()!=BoardMesh) return nullptr;
    auto* Material=BulletHoleMaterial.LoadSynchronous();
    if (!Material) return nullptr;
    // Store with the board, so rail motion and folding carry the existing shot group with them.
    BulletHoles.RemoveAll([](const auto& Hole) { return !IsValid(Hole.Get()); });
    while (BulletHoles.Num()>=FMath::Max(1,MaxBulletHoles))
    { BulletHoles[0]->DestroyComponent(); BulletHoles.RemoveAt(0); }
    FVector Normal=Hit.ImpactNormal.GetSafeNormal();
    if (Normal.IsNearlyZero()) Normal=GetScoringPlaneNormal();
    BoardMesh->SetReceivesDecals(true);
    auto* Decal=NewObject<UDecalComponent>(this);
    Decal->SetDecalMaterial(Material);
    Decal->SetFadeScreenSize(0.f); // Small bullet holes must remain visible at training distances.
    Decal->DecalSize=BulletHoleSize.GetAbs().ComponentMax(FVector(.01));
    Decal->SetUsingAbsoluteScale(true); // Board scale must not distort the bullet diameter/projection depth.
    Decal->SetupAttachment(BoardMesh);
    Decal->RegisterComponent();
    Decal->SetWorldLocationAndRotation(Hit.ImpactPoint+Normal*.05f,FRotationMatrix::MakeFromX(-Normal).Rotator());
    BulletHoles.Add(Decal);
    return Decal;
}
void AOBTrainingTarget::ClearBulletHoles()
{
    for (const auto& Hole : BulletHoles) if (IsValid(Hole.Get())) Hole->DestroyComponent();
    BulletHoles.Reset();
}
void AOBTrainingTarget::ApplyPose()
{
    BoardMesh->SetCollisionObjectType(OBCombatCollision::PlayerShotTarget);
    const float Fold = 1.f - FMath::SmoothStep(0.f, 1.f, PoseProgress);
    TargetPivot->SetRelativeRotation(RaisedRotation.Quaternion() * FQuat::Slerp(FQuat::Identity, FoldRotation.Quaternion(), Fold));
    const auto Collision = IsTargetRaised() ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision;
    BoardMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
    BoardMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    BoardMesh->SetCollisionResponseToChannel(OBCombatCollision::PlayerShotTarget, ECR_Block);
    BoardMesh->SetCollisionEnabled(Collision);
}
void AOBTrainingTarget::SetTargetEnabled(bool bEnabled, bool bImmediate, bool bPlayLowerSound)
{
    const bool bWasEnabled=bTargetEnabled;
    const bool bChanged = bTargetEnabled != bEnabled || !FMath::IsNearlyEqual(PoseProgress, bEnabled ? 1.f : 0.f);
    bTargetEnabled = bEnabled;
    if (!bEnabled && bWasEnabled && bPlayLowerSound)
        PlayTargetSound(TargetDownSound,BoardMesh->GetComponentLocation(),TEXT("TargetDown"));
    // A retracted stand remains visible, while its board cannot score or block a scoring ray.
    SetActorHiddenInGame(false);
    SetActorEnableCollision(true);
    const float Duration = bEnabled ? RaiseSeconds : LowerSeconds;
    if (bImmediate || Duration <= KINDA_SMALL_NUMBER)
    {
        PoseProgress = bEnabled ? 1.f : 0.f;
        ApplyPose();
        SetActorTickEnabled(false);
        if (bChanged) OnTargetPoseReached.Broadcast(this, bEnabled);
    }
    else
    {
        ApplyPose();
        SetActorTickEnabled(bChanged);
    }
}
void AOBTrainingTarget::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    // Travelling subclasses can keep ticking after a pose completes. Notify once.
    if (!IsPoseAnimating()) return;
    const float Desired = bTargetEnabled ? 1.f : 0.f;
    const float Duration = bTargetEnabled ? RaiseSeconds : LowerSeconds;
    PoseProgress = FMath::FInterpConstantTo(PoseProgress, Desired, FMath::Max(0.f, DeltaSeconds), 1.f / FMath::Max(0.001f, Duration));
    ApplyPose();
    if (!FMath::IsNearlyEqual(PoseProgress, Desired)) return;
    PoseProgress = Desired;
    ApplyPose();
    SetActorTickEnabled(false);
    OnTargetPoseReached.Broadcast(this, bTargetEnabled);
}

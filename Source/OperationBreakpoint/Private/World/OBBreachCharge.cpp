#include "World/OBBreachCharge.h"
#include "World/OBBreachDoor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "UObject/ConstructorHelpers.h"

AOBBreachCharge::AOBBreachCharge()
{
    PrimaryActorTick.bCanEverTick=false;
    Mesh=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("C4Mesh"));SetRootComponent(Mesh);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> C4(TEXT("/Game/Assets/Maps/MilitaryFiringRange/NEWitem/RPG_C4/bomb.bomb"));
    Mesh->SetStaticMesh(C4.Object);Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetGenerateOverlapEvents(false);Mesh->SetCanEverAffectNavigation(false);Mesh->SetCastShadow(false);
    SetActorHiddenInGame(true);
}
void AOBBreachCharge::PostInitializeComponents()
{
    Super::PostInitializeComponents();
    if (const UStaticMesh* Asset=Mesh->GetStaticMesh())
        SetActorScale3D(FVector(18.f/FMath::Max(1.f,Asset->GetBounds().BoxExtent.GetMax()*2.f)));
}
void AOBBreachCharge::PlaceCentered(FVector Center,FVector Normal,FVector Up)
{
    const UStaticMesh* Asset=Mesh->GetStaticMesh();if (!Asset) return;
    const FVector Extent=Asset->GetBounds().BoxExtent;
    const FRotator Rotation=Extent.X<=Extent.Y && Extent.X<=Extent.Z ? FRotationMatrix::MakeFromXZ(Normal,Up).Rotator()
        : Extent.Y<=Extent.Z ? FRotationMatrix::MakeFromYZ(Normal,Up).Rotator() : FRotationMatrix::MakeFromZY(Normal,Up).Rotator();
    // Imported mesh pivots need not be at the visual center.
    SetActorLocationAndRotation(Center-Rotation.RotateVector(Asset->GetBounds().Origin*Mesh->GetComponentScale()),Rotation);
}
bool AOBBreachCharge::FollowScreenPoint(APlayerController* PC,FVector2D Pixels)
{
    if (!PC || !PC->PlayerCameraManager || !Mesh->GetStaticMesh()) return false;
    FVector Origin,Ray;if (!PC->DeprojectScreenPositionToWorld(Pixels.X,Pixels.Y,Origin,Ray)) return false;
    const auto* Camera=PC->PlayerCameraManager.Get();const FVector Forward=Camera->GetCameraRotation().Vector();
    const float Dot=FVector::DotProduct(Ray,Forward);if (Dot<=.01f) return false;
    PlaceCentered(Camera->GetCameraLocation()+Ray*(90.f/Dot),-Forward,Camera->GetCameraRotation().RotateVector(FVector::UpVector));
    SetActorHiddenInGame(false);return true;
}
bool AOBBreachCharge::AttachToDoor(AOBBreachDoor* Door,APlayerController* PC,FVector2D TargetPixels)
{
    auto* Leaf=Door ? Door->GetIntactDoorMesh() : nullptr;
    if (!Leaf || !Leaf->GetStaticMesh() || !Mesh->GetStaticMesh() || !PC || !PC->PlayerCameraManager) return false;
    const FVector Camera=PC->PlayerCameraManager->GetCameraLocation();
    FVector Center=Leaf->Bounds.Origin,Normal=(Camera-Center).GetSafeNormal();
    FHitResult Hit;FCollisionQueryParams Params(SCENE_QUERY_STAT(OBC4Attach),true);
    FVector Origin,Ray;
    if (!PC->DeprojectScreenPositionToWorld(TargetPixels.X,TargetPixels.Y,Origin,Ray)) return false;
    if (Leaf->LineTraceComponent(Hit,Camera,Camera+Ray*2000.f,Params)) { Center=Hit.ImpactPoint;Normal=Hit.ImpactNormal; }
    else
    {
        // A window opening may have no triangle collision. Keep the target ray
        // on the door plane instead of snapping a raised target back to its bounds center.
        const float Dot=FVector::DotProduct(Ray,Normal);
        if (FMath::Abs(Dot)<.001f) return false;
        const float Distance=FVector::DotProduct(Center-Camera,Normal)/Dot;
        if (Distance<=0 || Distance>2000.f) return false;
        Center=Camera+Ray*Distance;
    }
    const float HalfThickness=Mesh->GetStaticMesh()->GetBounds().BoxExtent.GetMin()*Mesh->GetComponentScale().GetMax();
    PlaceCentered(Center+Normal*(HalfThickness+.2f),Normal,FVector::UpVector);
    AttachToComponent(Leaf,FAttachmentTransformRules::KeepWorldTransform);
    SetActorHiddenInGame(false);return true;
}

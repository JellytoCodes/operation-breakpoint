#include "World/OBBreachDoor.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/GeometryCollectionSimulationTypes.h"

DEFINE_LOG_CATEGORY_STATIC(LogOBBreachDoor, Log, All);

AOBBreachDoor::AOBBreachDoor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);
    FrameMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FrameMesh"));
    FrameMesh->SetupAttachment(SceneRoot);
    FrameMesh->SetCollisionProfileName(TEXT("BlockAll"));
    IntactDoorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("IntactDoorMesh"));
    IntactDoorMesh->SetupAttachment(SceneRoot);
    IntactDoorMesh->SetCollisionProfileName(TEXT("BlockAll"));
    FracturedDoor = CreateDefaultSubobject<UGeometryCollectionComponent>(TEXT("FracturedDoor"));
    FracturedDoor->SetupAttachment(SceneRoot);
    FracturedDoor->SetSimulatePhysics(false);
    FracturedDoor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    FracturedDoor->SetGenerateOverlapEvents(false);
    FracturedDoor->SetVisibility(false);
    FracturedDoor->SetHiddenInGame(true);
    FracturedDoor->SetCanEverAffectNavigation(false);
    ConfigureDebrisCollision(FracturedDoor);
    FracturedWall=CreateDefaultSubobject<UGeometryCollectionComponent>(TEXT("FracturedWall"));
    FracturedWall->SetupAttachment(SceneRoot);
    FracturedWall->SetSimulatePhysics(false);
    FracturedWall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    FracturedWall->SetGenerateOverlapEvents(false);
    FracturedWall->SetVisibility(false);FracturedWall->SetHiddenInGame(true);
    FracturedWall->SetCanEverAffectNavigation(false);
    ConfigureDebrisCollision(FracturedWall);
    FracturedWall->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
}

void AOBBreachDoor::ConfigureDebrisCollision(UGeometryCollectionComponent* Component)
{
    Component->SetCollisionObjectType(ECC_PhysicsBody);
    Component->SetCollisionResponseToAllChannels(ECR_Ignore);
    Component->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
    Component->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
    Component->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
    // Pawn, Camera and the player's Visibility shot trace pass through debris.
}

void AOBBreachDoor::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    if (!bBreachInProgress && !bBreached)
    {
        FracturedDoor->SetRelativeTransform(IntactDoorMesh->GetRelativeTransform());
        FracturedWall->SetRelativeTransform(FrameMesh->GetRelativeTransform());
    }
}

bool AOBBreachDoor::TryBreach(FVector BreachDirection)
{
    if (bBreached || bBreachInProgress || !IntactDoorMesh->GetStaticMesh() || !FracturedDoorAsset) return false;
    const auto Collection = FracturedDoorAsset->GetGeometryCollection();
    int32 RigidPieces = 0;
    if (Collection)
        for (int32 Index = 0; Index < Collection->SimulationType.Num(); ++Index)
            if (Collection->IsRigid(Index)) ++RigidPieces;
    const int32 RootIndex = FracturedDoorAsset->GetRootIndex();
    if (RigidPieces < 2 || !Collection || !Collection->SimulationType.IsValidIndex(RootIndex) || !Collection->IsClustered(RootIndex))
    {
        UE_LOG(LogOBBreachDoor, Warning, TEXT("%s: breach requires a clustered collection with at least two rigid pieces"), *GetName());
        return false;
    }
    if (FracturedWallAsset)
    {
        const auto Wall=FracturedWallAsset->GetGeometryCollection();
        const int32 WallRoot=FracturedWallAsset->GetRootIndex();
        if (!FrameMesh->GetStaticMesh() || !Wall || !Wall->SimulationType.IsValidIndex(WallRoot) || !Wall->IsClustered(WallRoot))
        { UE_LOG(LogOBBreachDoor,Warning,TEXT("%s: configured wall fracture is invalid"),*GetName());return false; }
    }
    FVector Direction = BreachDirection.ContainsNaN() ? FVector::ZeroVector : BreachDirection.GetSafeNormal();
    if (Direction.IsNearlyZero()) Direction = GetActorForwardVector();
    const float Speed = FMath::IsFinite(ForwardVelocity) ? FMath::Clamp(ForwardVelocity, 0.f, 2000.f) : 550.f;
    const float RadialSpeed = FMath::IsFinite(RadialVelocity) ? FMath::Clamp(RadialVelocity, 0.f, 1000.f) : 150.f;
    const float Radius = FMath::IsFinite(ImpulseRadius) ? FMath::Clamp(ImpulseRadius, 1.f, 1000.f) : 180.f;

    bBreachInProgress = true;
    BreakCollection(FracturedDoor,FracturedDoorAsset,IntactDoorMesh,Direction,Speed,RadialSpeed,Radius);
    if (FracturedWallAsset && FrameMesh->GetStaticMesh())
        BreakCollection(FracturedWall,FracturedWallAsset,FrameMesh,Direction,Speed*.6f,RadialSpeed,Radius);
    // Root-break multicast emission is disabled by default in the engine. Observe the actual
    // synchronized root state instead of changing that global cvar or reporting a queued command.
    SetActorTickEnabled(true);
    return true;
}

void AOBBreachDoor::BreakCollection(UGeometryCollectionComponent* Component,UGeometryCollection* Asset,
    UStaticMeshComponent* Intact,FVector Direction,float Speed,float RadialSpeed,float Radius)
{
    Component->SetRelativeTransform(Intact->GetRelativeTransform());
    Component->EnableClustering=true;Component->ObjectType=EObjectStateTypeEnum::Chaos_Object_Dynamic;
    Component->InitialVelocityType=EInitialVelocityTypeEnum::Chaos_Initial_Velocity_User_Defined;
    Component->InitialLinearVelocity=Direction*Speed;Component->InitialAngularVelocity=FVector::ZeroVector;
    Intact->SetCollisionEnabled(ECollisionEnabled::NoCollision);Intact->SetVisibility(false);Intact->SetHiddenInGame(true);
    Component->SetNotifyBreaks(true);Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Component->SetRestCollection(Asset,false);Component->SetEnableGravity(true);Component->SetSimulatePhysics(true);
    Component->SetVisibility(true);Component->SetHiddenInGame(false);
    // Keep the authored outer wall anchors while releasing the border next to the door.
    if (Component==FracturedDoor) Component->RemoveAllAnchors();
    Component->ApplyBreakingLinearVelocity(Component->GetRootIndex(),Direction*Speed);
    Component->CrumbleActiveClusters();
    Component->AddRadialImpulse(Component->Bounds.Origin-Direction*30.f,Radius,RadialSpeed,RIF_Linear,true);
}

void AOBBreachDoor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bBreachInProgress || !FracturedDoor->IsRootBroken()
        || (FracturedWallAsset && FrameMesh->GetStaticMesh() && !FracturedWall->IsRootBroken())) return;
    bBreachInProgress = false;
    bBreached = true;
    SetActorTickEnabled(false);
    OnBreachCompleted.Broadcast(this);
}
bool AOBBreachDoor::ClearPassageForEntry()
{
    if (!bBreached) return false;
    IntactDoorMesh->SetVisibility(false);IntactDoorMesh->SetHiddenInGame(true);
    IntactDoorMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    // A broken root does not imply all nested clusters have dispersed. The
    // explosion still runs in Chaos during blackout; no large leaf survives entry.
    FracturedDoor->SetVisibility(false);FracturedDoor->SetHiddenInGame(true);
    FracturedDoor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    FracturedDoor->SetSimulatePhysics(false);
    return IsPassageClear();
}
bool AOBBreachDoor::IsPassageClear() const
{
    return !IntactDoorMesh->IsVisible() && !FracturedDoor->IsVisible()
        && IntactDoorMesh->GetCollisionEnabled()==ECollisionEnabled::NoCollision
        && FracturedDoor->GetCollisionEnabled()==ECollisionEnabled::NoCollision;
}

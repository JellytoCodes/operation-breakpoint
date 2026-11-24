#include "Training/OBTrainingDoor.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

AOBTrainingDoor::AOBTrainingDoor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Hinge")));
    DoorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorMesh"));
    DoorPivot = CreateDefaultSubobject<USceneComponent>(TEXT("DoorPivot"));
    DoorPivot->SetupAttachment(RootComponent);
    FrameMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FrameMesh"));
    FrameMesh->SetupAttachment(RootComponent);
    FrameMesh->SetCollisionProfileName(TEXT("BlockAll"));
    DoorMesh->SetupAttachment(DoorPivot);
    HandleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HandleMesh"));
    HandleMesh->SetupAttachment(DoorMesh);
    HandleMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (Cube.Succeeded()) DoorMesh->SetStaticMesh(Cube.Object);
    DoorMesh->SetRelativeLocation(FVector(0.0f, 50.0f, 100.0f));
    DoorMesh->SetRelativeScale3D(FVector(0.1f, 1.0f, 2.0f));
    DoorMesh->SetCollisionProfileName(TEXT("BlockAll"));
}

void AOBTrainingDoor::BeginPlay()
{
    Super::BeginPlay();
    ClosedRotation = DoorPivot->GetRelativeRotation();
}

bool AOBTrainingDoor::TryBreach(int32 AttemptId)
{
    if (bBreached || ActiveAttempt != INDEX_NONE) return false;
    ActiveAttempt = AttemptId;
    Elapsed = 0.0f;
    SetActorTickEnabled(true);
    OnBreachStarted();
    return true;
}

void AOBTrainingDoor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (ActiveAttempt == INDEX_NONE) return;
    Elapsed += DeltaSeconds;
    const float Alpha = FMath::Clamp(Elapsed / FMath::Max(0.1f, BreachDuration), 0.0f, 1.0f);
    DoorPivot->SetRelativeRotation(ClosedRotation + FRotator(0, OpenYaw * Alpha, 0));
    if (Alpha < 1.0f) return;
    bBreached = true;
    const int32 CompletedAttempt = ActiveAttempt;
    ActiveAttempt = INDEX_NONE;
    SetActorTickEnabled(false);
    OnDoorBreached.Broadcast(this, CompletedAttempt);
}

void AOBTrainingDoor::ResetDoor()
{
    ActiveAttempt = INDEX_NONE;
    bBreached = false;
    SetActorTickEnabled(false);
    DoorPivot->SetRelativeRotation(ClosedRotation);
}

#include "World/OBEncounterSpawner.h"

#include "AI/OBAIController.h"
#include "Character/OBEnemyCharacter.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "NavigationSystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogOBEncounter, Log, All);

AOBEncounterSpawner::AOBEncounterSpawner()
{
    PrimaryActorTick.bCanEverTick = false;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);

    StagingPoint = CreateDefaultSubobject<USceneComponent>(TEXT("StagingPoint"));
    StagingPoint->SetupAttachment(SceneRoot);
    StagingPoint->SetRelativeLocation(FVector(300.0f, 0.0f, 0.0f));
    CoverPoint = CreateDefaultSubobject<USceneComponent>(TEXT("CoverPoint"));
    CoverPoint->SetupAttachment(SceneRoot);
    CoverPoint->SetRelativeLocation(FVector(300.f,150.f,0.f));
}

void AOBEncounterSpawner::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    ConfigureStagingMarkers();
}

void AOBEncounterSpawner::PostRegisterAllComponents()
{
    Super::PostRegisterAllComponents();
    ConfigureStagingMarkers();
}

void AOBEncounterSpawner::ConfigureStagingMarkers()
{
    // Staging meshes are authoring markers, not obstacles for navigation or moving pawns.
    TInlineComponentArray<UStaticMeshComponent*> Meshes(this);
    for (UStaticMeshComponent* Mesh : Meshes)
    {
        if (StagingPoint && Mesh->IsAttachedTo(StagingPoint))
        {
            Mesh->SetCanEverAffectNavigation(false);
            Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }
    }
}

void AOBEncounterSpawner::ResetSpawnForQA()
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    auto* Enemy=SpawnedEnemy.Get();
    SpawnedEnemy=nullptr;bPrepared=false;PreparedPointIndex=INDEX_NONE;
    if (!IsValid(Enemy)) return;
    if (auto* AI=Cast<AOBAIController>(Enemy->GetController())) AI->StopStateTree();
    if (auto* Controller=Enemy->GetController()) { Controller->UnPossess();Controller->Destroy(); }
    // Remove this QA spawn's in-flight bullets/owned actors as well, before restoring player damage.
    for (TActorIterator<AActor> It(GetWorld());It;++It)
        if (*It!=Enemy && IsValid(*It) && (It->GetOwner()==Enemy || It->GetInstigator()==Enemy)) It->Destroy();
    if (PlacedEnemy==Enemy) { EnemyClass=Enemy->GetClass();PlacedEnemy=nullptr; }
    Enemy->Destroy();
#endif
}

void AOBEncounterSpawner::PrepareEncounter(int32 InPointIndex)
{
    if (bPrepared) return;

    UWorld* World = GetWorld();
    if (IsValid(PlacedEnemy) && (PlacedEnemy->GetWorld()!=World || PlacedEnemy->GetCombatState()==EOBEnemyCombatState::Dead))
    {
        UE_LOG(LogOBEncounter,Warning,TEXT("Placed guard must be alive in the same world | Spawner=%s"),*GetName());
        return;
    }

    if (!World || (!IsValid(PlacedEnemy) && !EnemyClass))
    {
        UE_LOG(LogOBEncounter, Warning,
            TEXT("Enemy spawn failed | Point=%d | Spawner=%s | EnemyClass is missing"),
            InPointIndex, *GetName());
        return;
    }

    FActorSpawnParameters SpawnParameters;
    SpawnParameters.SpawnCollisionHandlingOverride =
        ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

    FTransform SpawnTransform=GetActorTransform();
    if (!IsValid(PlacedEnemy) && bParticipatesInCombat && bMoveToStagingPoint)
    {
        // Physics can lift a spawn onto the deck without putting its XY inside the eroded NavMesh.
        // Register crowd agents on a nearby navigable footprint, with physical capsule clearance.
        const auto* Defaults=EnemyClass.GetDefaultObject();
        const auto* Capsule=Defaults ? Defaults->GetCapsuleComponent() : nullptr;
        auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
        if (Capsule && Nav)
        {
            const float Scale=SpawnTransform.GetScale3D().GetAbsMin();
            const float Radius=Capsule->GetScaledCapsuleRadius()*Scale;
            const float HalfHeight=Capsule->GetScaledCapsuleHalfHeight()*Scale;
            FNavLocation Feet;
            if (Radius>0 && HalfHeight>=Radius && Nav->ProjectPointToNavigation(
                SpawnTransform.GetLocation()-FVector(0,0,HalfHeight),Feet,FVector(25,25,120)))
            {
                const FVector Center=Feet.Location+FVector(0,0,HalfHeight+2.f);
                FCollisionQueryParams Query(SCENE_QUERY_STAT(OBEncounterSpawnClearance),false,this);
                if (!World->OverlapBlockingTestByChannel(Center,FQuat::Identity,Capsule->GetCollisionObjectType(),
                    FCollisionShape::MakeCapsule(Radius,HalfHeight),Query,FCollisionResponseParams(Capsule->GetCollisionResponseToChannels())))
                    SpawnTransform.SetLocation(Center);
            }
        }
    }
    SpawnedEnemy = IsValid(PlacedEnemy) ? PlacedEnemy.Get() : World->SpawnActor<AOBEnemyCharacter>(
        EnemyClass,
        SpawnTransform,
        SpawnParameters);

    if (!IsValid(SpawnedEnemy))
    {
        UE_LOG(LogOBEncounter, Warning,
            TEXT("Enemy spawn failed | Point=%d | Spawner=%s"),
            InPointIndex, *GetName());
        return;
    }

    PreparedPointIndex = InPointIndex;
    bPrepared = true;
    if (bOverrideEnemyBehavior) SpawnedEnemy->EncounterBehavior = EnemyBehavior;
    SpawnedEnemy->EncounterBehavior.bFacePlayer = bFacePlayerAfterArrival;
    SpawnedEnemy->EncounterBehavior.EntryMode = bMoveToStagingPoint
        ? EOBEnemyEntryMode::RunToStaging : EOBEnemyEntryMode::WaitAtSpawn;
    SpawnedEnemy->SetEncounterCombatant(bParticipatesInCombat);
    if (!SpawnedEnemy->IsEncounterCombatant())
    {
        UE_LOG(LogOBEncounter,Display,TEXT("Noncombatant Spawned | Point=%d | Spawner=%s | Actor=%s"),
            PreparedPointIndex,*GetName(),*GetNameSafe(SpawnedEnemy));
        return;
    }

    AOBAIController* AIController =
        Cast<AOBAIController>(SpawnedEnemy->GetController());

    if (!AIController)
    {
        SpawnedEnemy->SpawnDefaultController();
        AIController = Cast<AOBAIController>(SpawnedEnemy->GetController());
    }

    if (AIController && CombatStateTree) AIController->SetEncounterStateTree(CombatStateTree);
    if (!AIController ||
        !AIController->PrepareEncounterEntry(GetStagingTransform(),CoverPoint->GetComponentTransform(),
            bMoveToStagingPoint ? FMath::Clamp(StagingMoveDelay,0.f,10.f) : 0.f))
    {
        UE_LOG(LogOBEncounter, Warning,
            TEXT("Staging move request failed | Enemy=%s"),
            *GetNameSafe(SpawnedEnemy));
    }

    UE_LOG(LogOBEncounter, Display,
        TEXT("Enemy Spawned | Point=%d | Spawner=%s | Enemy=%s"),
        PreparedPointIndex,
        *GetName(),
        *GetNameSafe(SpawnedEnemy));
}

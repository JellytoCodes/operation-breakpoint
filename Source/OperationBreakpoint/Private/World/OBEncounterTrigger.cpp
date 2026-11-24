#include "World/OBEncounterTrigger.h"

#include "Character/OBPlayerCharacter.h"
#include "Character/OBActionComponent.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Character/OBEnemyCharacter.h"
#include "Components/BoxComponent.h"
#include "World/OBEncounterSpawner.h"
#include "Stage/OBStageFlowComponent.h"

AOBEncounterTrigger::AOBEncounterTrigger()
{
    PrimaryActorTick.bCanEverTick = false;

    TriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox"));
    SetRootComponent(TriggerBox);

    TriggerBox->SetBoxExtent(FVector(150.0f, 300.0f, 150.0f));
    TriggerBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    TriggerBox->SetCollisionResponseToAllChannels(ECR_Ignore);
    TriggerBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
}

void AOBEncounterTrigger::BeginPlay()
{
    Super::BeginPlay();

    TriggerBox->OnComponentBeginOverlap.AddDynamic(this, &ThisClass::HandleBeginOverlap);
}

bool AOBEncounterTrigger::GetCombatFacingLocation(FVector& OutLocation) const
{
    FVector Total=FVector::ZeroVector;
    int32 Count=0;
    for (const AOBEncounterSpawner* Spawner : Spawners)
    {
        if (!IsValid(Spawner) || !Spawner->bParticipatesInCombat) continue;
        const FVector Location=Spawner->bMoveToStagingPoint ? Spawner->GetStagingTransform().GetLocation()
            : IsValid(Spawner->PlacedEnemy) ? Spawner->PlacedEnemy->GetActorLocation() : Spawner->GetActorLocation();
        if (Location.ContainsNaN()) continue;
        Total+=Location;
        ++Count;
    }
    if (Count==0) return false;
    OutLocation=Total/Count;
    return true;
}

int32 AOBEncounterTrigger::GetPlannedCombatantCount() const
{
    TSet<const AOBEncounterSpawner*> Unique;
    for (const AOBEncounterSpawner* S:Spawners) if (IsValid(S) && S->bParticipatesInCombat) Unique.Add(S);
    return Unique.Num();
}

void AOBEncounterTrigger::HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
    UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
    ActivateForPlayer(Cast<AOBPlayerCharacter>(OtherActor));
}

bool AOBEncounterTrigger::ActivateForPlayer(AOBPlayerCharacter* Player)
{
    if (bTriggered || !IsValid(Player) || Player->GetPlayerSplineComponent()->IsRoutePreview()) return false;
    if (!Player->GetStageFlowComponent()->CanActivateEncounter(PointIndex)) return false;

    bTriggered = true;
    TriggerBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    UOBActionComponent* ActionComponent = Player->GetActionComponent();
    ActionComponent->BeginEncounter(PointIndex);

    for (AOBEncounterSpawner* Spawner : Spawners)
    {
        if (!IsValid(Spawner)) continue;

        Spawner->PrepareEncounter(PointIndex);
        if (Spawner->bParticipatesInCombat) ActionComponent->RegisterEnemy(Spawner->GetSpawnedEnemy());
    }

    ActionComponent->FinalizeEncounterSetup();
    OnEncounterTriggered.Broadcast(PointIndex);
    return true;
}

void AOBEncounterTrigger::ResetForQA()
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    // Stay disarmed while the player is inside. QA Zone explicitly activates the next spawn.
    TriggerBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    bTriggered=false;
    for (AOBEncounterSpawner* Spawner : Spawners) if (IsValid(Spawner)) Spawner->ResetSpawnForQA();
#endif
}

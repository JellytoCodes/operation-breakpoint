#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OBEncounterTrigger.generated.h"

class AOBEncounterSpawner;
class UBoxComponent;
class UPrimitiveComponent;
class AOBPlayerCharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOBEncounterTriggeredSignature, int32, PointIndex);

UCLASS()
class OPERATIONBREAKPOINT_API AOBEncounterTrigger : public AActor
{
    GENERATED_BODY()

public:
    const TArray<TObjectPtr<AOBEncounterSpawner>>& GetSpawners() const { return Spawners; }
    AOBEncounterTrigger();

    int32 GetPointIndex() const { return PointIndex; }
    int32 GetPlannedCombatantCount() const;
    // Stable authored focus: runners' destinations and stationary combatants' spawn positions.
    bool GetCombatFacingLocation(FVector& OutLocation) const;
    bool ActivateForPlayer(AOBPlayerCharacter* Player);
    void ResetForQA();

    UPROPERTY(BlueprintAssignable, Category = "OB|Encounter")
    FOBEncounterTriggeredSignature OnEncounterTriggered;

protected:
    virtual void BeginPlay() override;

private:
    friend class FOBStageEncounterMapSmoke;
    friend class FOBStageFlowMapSmoke;
    friend class FOBNoncombatSpawnTest;
    friend class FOBRailCombatFacingTest;
    friend class FOBQACameraTuningTest;
    friend class FOBQACameraTuningMapSmoke;
    UFUNCTION()
    void HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
        UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OB|Encounter", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UBoxComponent> TriggerBox;

    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "OB|Encounter", meta = (AllowPrivateAccess = "true"))
    int32 PointIndex = 0;

    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "OB|Encounter", meta = (AllowPrivateAccess = "true"))
    TArray<TObjectPtr<AOBEncounterSpawner>> Spawners;

    bool bTriggered = false;
};

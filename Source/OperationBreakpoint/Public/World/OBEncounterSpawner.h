#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AI/OBEnemyBehaviorSettings.h"
#include "OBEncounterSpawner.generated.h"

class AOBEnemyCharacter;
class USceneComponent;
class UStateTree;

UCLASS()
class OPERATIONBREAKPOINT_API AOBEncounterSpawner : public AActor
{
    GENERATED_BODY()

public:
	AOBEncounterSpawner();

    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void PostRegisterAllComponents() override;

    void PrepareEncounter(int32 InPointIndex);
    void ResetSpawnForQA();

    AOBEnemyCharacter* GetSpawnedEnemy() const { return SpawnedEnemy; }
    FTransform GetSpawnTransform() const { return GetActorTransform(); }
    FTransform GetStagingTransform() const { return StagingPoint->GetComponentTransform(); }
    int32 GetPreparedPointIndex() const { return PreparedPointIndex; }
    bool IsPrepared() const { return bPrepared; }

    // Optional level-placed guard. Leaving this empty keeps the original spawn behavior.
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="OB|Encounter")
    TObjectPtr<AOBEnemyCharacter> PlacedEnemy;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Encounter|Spawn")
    bool bParticipatesInCombat = true;

    // This entry choice takes precedence over EnemyBehavior.EntryMode.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Encounter|Spawn", meta=(EditCondition="bParticipatesInCombat"))
    bool bMoveToStagingPoint = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Encounter|Spawn", meta=(EditCondition="bParticipatesInCombat && bMoveToStagingPoint", ClampMin="0.0", ClampMax="10.0", UIMin="0.0", UIMax="10.0", Units="s"))
    float StagingMoveDelay = 3.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Encounter|Behavior")
    bool bOverrideEnemyBehavior = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Encounter|Behavior", meta=(EditCondition="bOverrideEnemyBehavior"))
    FOBEnemyBehaviorSettings EnemyBehavior;

    // Empty preserves the AIController Blueprint's existing StateTree.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Encounter|Behavior", meta=(EditCondition="bParticipatesInCombat"))
    TObjectPtr<UStateTree> CombatStateTree;
    UPROPERTY(EditInstanceOnly,BlueprintReadOnly,Category="OB|Encounter|Zone6") TObjectPtr<AOBEncounterSpawner> GuardedHostageSpawner;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Encounter|Behavior", meta=(EditCondition="bParticipatesInCombat"))
    bool bFacePlayerAfterArrival = true;

private:
    void ConfigureStagingMarkers();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OB|Encounter", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<USceneComponent> SceneRoot;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OB|Encounter", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<USceneComponent> StagingPoint;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="OB|Encounter", meta=(AllowPrivateAccess="true"))
    TObjectPtr<USceneComponent> CoverPoint;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "OB|Encounter", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<AOBEnemyCharacter> EnemyClass;

	UPROPERTY(Transient)
	TObjectPtr<AOBEnemyCharacter> SpawnedEnemy;

    int32 PreparedPointIndex = INDEX_NONE;
    bool bPrepared = false;
};

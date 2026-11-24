#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OBBreachDoor.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UGeometryCollection;
class UGeometryCollectionComponent;
class AOBBreachDoor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOBBreachCompleted, AOBBreachDoor*, Door);

/** Scenario door: the intact leaf is replaced by a pre-fractured Chaos collection. */
UCLASS()
class OPERATIONBREAKPOINT_API AOBBreachDoor : public AActor
{
    GENERATED_BODY()
public:
    AOBBreachDoor();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void Tick(float DeltaSeconds) override;

    /** Accepts a breach once. Completion is reported after Chaos confirms the root is broken. */
    UFUNCTION(BlueprintCallable, Category="OB|Breach")
    bool TryBreach(FVector BreachDirection = FVector::ZeroVector);
    // Only after a confirmed Chaos break: retire leaf debris before the blackout ends.
    bool ClearPassageForEntry();
    bool IsPassageClear() const;
    UFUNCTION(BlueprintPure, Category="OB|Breach")
    bool IsBreached() const { return bBreached; }
    UFUNCTION(BlueprintPure, Category="OB|Breach")
    bool IsBreachInProgress() const { return bBreachInProgress; }
    UFUNCTION(BlueprintPure, Category="OB|Breach")
    UStaticMeshComponent* GetIntactDoorMesh() const { return IntactDoorMesh; }
    UFUNCTION(BlueprintPure, Category="OB|Breach")
    UStaticMeshComponent* GetFrameMesh() const { return FrameMesh; }
    UFUNCTION(BlueprintPure, Category="OB|Breach")
    UGeometryCollectionComponent* GetFracturedDoor() const { return FracturedDoor; }
    UFUNCTION(BlueprintPure, Category="OB|Breach")
    UGeometryCollectionComponent* GetFracturedWall() const { return FracturedWall; }

    UPROPERTY(BlueprintAssignable, Category="OB|Breach")
    FOBBreachCompleted OnBreachCompleted;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="OB|Breach")
    TObjectPtr<UGeometryCollection> FracturedDoorAsset;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="OB|Breach")
    TObjectPtr<UGeometryCollection> FracturedWallAsset;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Breach", meta=(ClampMin="0",ClampMax="2000",Units="cm/s"))
    float ForwardVelocity = 550.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Breach", meta=(ClampMin="0",ClampMax="1000",Units="cm/s"))
    float RadialVelocity = 150.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Breach", meta=(ClampMin="1",ClampMax="1000",Units="cm"))
    float ImpulseRadius = 180.f;

private:
    friend class FOBStageZone6GateTest;
    void ConfigureDebrisCollision(UGeometryCollectionComponent* Component);
    void BreakCollection(UGeometryCollectionComponent* Component,UGeometryCollection* Asset,
        UStaticMeshComponent* Intact,FVector Direction,float Speed,float RadialSpeed,float Radius);
    UPROPERTY(VisibleAnywhere, Category="OB|Breach")
    TObjectPtr<USceneComponent> SceneRoot;
    UPROPERTY(VisibleAnywhere, Category="OB|Breach")
    TObjectPtr<UStaticMeshComponent> FrameMesh;
    UPROPERTY(VisibleAnywhere, Category="OB|Breach")
    TObjectPtr<UStaticMeshComponent> IntactDoorMesh;
    UPROPERTY(VisibleAnywhere, Category="OB|Breach")
    TObjectPtr<UGeometryCollectionComponent> FracturedDoor;
    UPROPERTY(VisibleAnywhere, Category="OB|Breach")
    TObjectPtr<UGeometryCollectionComponent> FracturedWall;
    UPROPERTY(Transient)
    bool bBreachInProgress = false;
    UPROPERTY(Transient)
    bool bBreached = false;
};

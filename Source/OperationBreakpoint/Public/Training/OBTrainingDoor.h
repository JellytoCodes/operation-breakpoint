#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OBTrainingDoor.generated.h"

class UStaticMeshComponent;
class AOBTrainingDoor;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOBTrainingDoorOpened, AOBTrainingDoor*, Door, int32, AttemptId);

UCLASS()
class OPERATIONBREAKPOINT_API AOBTrainingDoor : public AActor
{
    GENERATED_BODY()
public:
    AOBTrainingDoor();
    bool TryBreach(int32 AttemptId);
    void ResetDoor();
    bool IsBreached() const { return bBreached; }

    UPROPERTY(BlueprintAssignable, Category = "OB|Training")
    FOBTrainingDoorOpened OnDoorBreached;

    UFUNCTION(BlueprintImplementableEvent, Category = "OB|Training")
    void OnBreachStarted();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OB|Training")
    TObjectPtr<USceneComponent> DoorPivot;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OB|Training")
    TObjectPtr<UStaticMeshComponent> FrameMesh;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OB|Training")
    TObjectPtr<UStaticMeshComponent> HandleMesh;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OB|Training")
    TObjectPtr<UStaticMeshComponent> DoorMesh;

    UPROPERTY(EditAnywhere, Category = "OB|Training", meta = (ClampMin = "0.1"))
    float BreachDuration = 0.75f;

    UPROPERTY(EditAnywhere, Category = "OB|Training")
    float OpenYaw = 100.0f;

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
private:
    friend class FOBTrainingFlowTest;
    friend class FOBTrainingPopUpTest;
    FRotator ClosedRotation;
    float Elapsed = 0.0f;
    int32 ActiveAttempt = INDEX_NONE;
    bool bBreached = false;
};

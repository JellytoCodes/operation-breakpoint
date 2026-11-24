#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OBBreachCharge.generated.h"
class UStaticMeshComponent;
class APlayerController;
class AOBBreachDoor;

UCLASS()
class OPERATIONBREAKPOINT_API AOBBreachCharge : public AActor
{
    GENERATED_BODY()
public:
    AOBBreachCharge();
    virtual void PostInitializeComponents() override;
    bool FollowScreenPoint(APlayerController* PC,FVector2D Pixels);
    bool AttachToDoor(AOBBreachDoor* Door,APlayerController* PC,FVector2D TargetPixels);
    UStaticMeshComponent* GetChargeMesh() const { return Mesh; }
private:
    void PlaceCentered(FVector Center,FVector Normal,FVector Up);
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Mesh;
};

#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "OBModularSoldierComponent.generated.h"

class USkeletalMesh;
class USkeletalMeshComponent;
class UAnimInstance;

UCLASS(ClassGroup=(OB), meta=(BlueprintSpawnableComponent))
class OPERATIONBREAKPOINT_API UOBModularSoldierComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UOBModularSoldierComponent();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Soldier") TObjectPtr<USkeletalMesh> BodyMesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Soldier") TSubclassOf<UAnimInstance> AnimationClass;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Soldier") TArray<TObjectPtr<USkeletalMesh>> OutfitMeshes;
    const TArray<TObjectPtr<USkeletalMeshComponent>>& GetVisualParts() const { return VisualParts; }
    void UpdateFirstPersonVisibility();
protected:
    virtual void OnRegister() override;
    virtual void OnUnregister() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
private:
    UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category="OB|Soldier", meta=(AllowPrivateAccess="true"))
    TArray<TObjectPtr<USkeletalMeshComponent>> VisualParts;
};

#pragma once
#include "CoreMinimal.h"
#include "Components/ProgressBar.h"
#include "OBRadialProgressBar.generated.h"
class UMaterialInstanceDynamic;

/** A normal editable ProgressBar whose UI material reveals a clockwise texture arc. */
UCLASS(meta=(DisplayName="OB Radial Progress Bar"))
class OPERATIONBREAKPOINT_API UOBRadialProgressBar : public UProgressBar
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable,Category="Progress") void SetRadialPercent(float Value);
    virtual void SynchronizeProperties() override;
private:
    void UpdateRadialMaterial();
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> RadialMaterial;
};

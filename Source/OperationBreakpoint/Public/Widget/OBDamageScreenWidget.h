#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "OBDamageScreenWidget.generated.h"

class UImage;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/** Local, non-interactive damage feedback, rendered underneath the HUD. */
UCLASS()
class OPERATIONBREAKPOINT_API UOBDamageScreenWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    UOBDamageScreenWidget(const FObjectInitializer& ObjectInitializer);
    void PlayHit();
    void SetInjuredActive(bool bActive);
    void ResetEffects();
    bool IsHitActive() const { return HitElapsed<.5f; }
    bool IsInjuredActive() const { return bInjuredActive; }
protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeTick(const FGeometry& Geometry,float DeltaTime) override;
    virtual void NativeDestruct() override;
    UPROPERTY(EditDefaultsOnly,Category="OB|Damage") TSoftObjectPtr<UMaterialInterface> HitMaterial;
    UPROPERTY(EditDefaultsOnly,Category="OB|Damage") TSoftObjectPtr<UMaterialInterface> InjuredMaterial;
private:
    friend class FOBDamageScreenTest;
    void UpdateForTime(double Now,float Aspect);
    void UpdateVisuals(float Aspect);
    UPROPERTY(Transient) TObjectPtr<UImage> HitImage;
    UPROPERTY(Transient) TObjectPtr<UImage> InjuredImage;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> HitMID;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> InjuredMID;
    float HitElapsed=.5f;
    float InjuredElapsed=0.f;
    double HitStartedAt=-1;
    double InjuredStartedAt=0;
    bool bInjuredActive=false;
};

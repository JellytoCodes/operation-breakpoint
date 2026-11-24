#pragma once
#include "CoreMinimal.h"
#include "Character/OBCharacter.h"
#include "OBAllyCharacter.generated.h"
class UOBAllySplineComponent;
class AOBEnemyCharacter;
class UOBAllyNavigationComponent;

// Cinematic squad member: no enemy AI, hit boxes, score contribution or player input components.
UCLASS()
class OPERATIONBREAKPOINT_API AOBAllyCharacter : public AOBCharacter
{
    GENERATED_BODY()
public:
    AOBAllyCharacter();
    virtual FVector GetVelocity() const override;
    virtual USkeletalMeshComponent* GetWeaponMesh() const override { return WeaponMesh; }
    UFUNCTION(BlueprintPure, Category="OB|Ally") UOBAllySplineComponent* GetRouteComponent() const { return RouteComponent; }
    UFUNCTION(BlueprintPure, Category="OB|Ally") UOBAllyNavigationComponent* GetNavigationComponent() const { return NavigationComponent; }
    // Retained for old assets; weapon visibility no longer depends on action state.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Ally|Weapon", meta=(DeprecatedProperty, DeprecationMessage="Allies always keep weapons visible")) bool bHideWeaponDuringPlayerAction=false;
    void FireSupportAt(AOBEnemyCharacter* Target);
    UFUNCTION(BlueprintImplementableEvent,Category="OB|Ally|Support") void OnSupportShot(AOBEnemyCharacter* Target);
protected:
    virtual void Tick(float DeltaSeconds) override;
private:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="OB|Navigation", meta=(AllowPrivateAccess="true")) TObjectPtr<UOBAllyNavigationComponent> NavigationComponent;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="OB|Ally", meta=(AllowPrivateAccess="true")) TObjectPtr<UOBAllySplineComponent> RouteComponent;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="OB|Weapon", meta=(AllowPrivateAccess="true")) TObjectPtr<USkeletalMeshComponent> WeaponMesh;
};

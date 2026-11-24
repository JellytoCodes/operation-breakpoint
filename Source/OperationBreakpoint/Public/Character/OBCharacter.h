// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "OBCharacter.generated.h"

class UOBModularSoldierComponent;
class UOBWeaponGripComponent;
class UMeshComponent;

UCLASS()
class OPERATIONBREAKPOINT_API AOBCharacter : public ACharacter
{
	GENERATED_BODY()

public:
    AOBCharacter();

    // Uses the mesh's native Custom Depth / Stencil settings; restores authored values on exit.
    UFUNCTION(BlueprintCallable, Category="OB|Appearance|Outline")
    void SetAimOutline(bool bEnabled);
    bool IsAimOutlined() const { return bAimOutlined; }
    static constexpr int32 AimOutlineStencil=253;
    virtual int32 GetAimOutlineStencilValue() const { return AimOutlineStencil; }

    UFUNCTION(BlueprintPure, Category="OB|Soldier")
    UOBModularSoldierComponent* GetSoldierAppearanceComponent() const { return SoldierAppearanceComponent; }

    UFUNCTION(BlueprintPure, Category="OB|Weapon")
    virtual USkeletalMeshComponent* GetWeaponMesh() const { return nullptr; }

    UFUNCTION(BlueprintPure, Category="OB|Weapon|Grip")
    UOBWeaponGripComponent* GetWeaponGripComponent() const { return WeaponGripComponent; }

protected:
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void PostInitializeComponents() override;

private:
    struct FOutlineMeshState
    {
        TWeakObjectPtr<UMeshComponent> Mesh;
        bool bCustomDepth=false;
        int32 Stencil=0;
        ERendererStencilMask WriteMask=ERendererStencilMask::ERSM_Default;
    };
    TArray<FOutlineMeshState> OutlineMeshStates;
    bool bAimOutlined=false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="OB|Soldier", meta=(AllowPrivateAccess="true"))
    TObjectPtr<UOBModularSoldierComponent> SoldierAppearanceComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="OB|Weapon|Grip", meta=(AllowPrivateAccess="true"))
    TObjectPtr<UOBWeaponGripComponent> WeaponGripComponent;
};

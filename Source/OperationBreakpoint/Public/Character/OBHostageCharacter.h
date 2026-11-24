#pragma once

#include "CoreMinimal.h"
#include "Character/OBEnemyCharacter.h"
#include "OBHostageCharacter.generated.h"

class UAnimSequence;

// Reuses the existing shot hit boxes and damage/death integration, but never participates in combat.
UCLASS(Blueprintable)
class OPERATIONBREAKPOINT_API AOBHostageCharacter : public AOBEnemyCharacter
{
    GENERATED_BODY()
public:
    AOBHostageCharacter();
    static constexpr int32 HostageAimOutlineStencil=252;
    virtual int32 GetAimOutlineStencilValue() const override { return HostageAimOutlineStencil; }

    virtual USkeletalMeshComponent* GetWeaponMesh() const override { return nullptr; }
    virtual void SetEncounterCombatant(bool bCombatant) override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Hostage")
    TObjectPtr<UAnimSequence> IdleAnimation;

protected:
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;

private:
    void ConfigureHostage(bool bApplyPose);
    void SettleOnFloor();
};

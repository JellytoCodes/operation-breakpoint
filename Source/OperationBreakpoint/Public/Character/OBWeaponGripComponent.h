#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "OBWeaponGripComponent.generated.h"

class USkeletalMeshComponent;

// One grip profile on the body leader; clothing receives the corrected pose through Leader Pose.
UCLASS(ClassGroup=(OB), meta=(BlueprintSpawnableComponent))
class OPERATIONBREAKPOINT_API UOBWeaponGripComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UOBWeaponGripComponent();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Weapon|Grip")
    bool bEnabled = false;

    // Rifle component transform relative to hand_r (cm). Does not modify the vendor skeleton/socket.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Weapon|Grip")
    FTransform WeaponInRightHand = FTransform::Identity;

    // Desired left wrist transform in rifle component space, including palm orientation.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Weapon|Grip")
    FTransform LeftHandInWeapon = FTransform::Identity;

    // Parent-local offsets from the Soldier reference pose. The right index has its own trigger pose.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Weapon|Grip")
    TMap<FName, FRotator> FingerPoseOffsets;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Weapon|Grip|RPG")
    bool bRPGEnabled = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Weapon|Grip|RPG")
    FTransform RPGWeaponInRightHand = FTransform::Identity;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Weapon|Grip|RPG")
    FTransform RPGLeftHandInWeapon = FTransform::Identity;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Weapon|Grip|RPG")
    TMap<FName, FRotator> RPGFingerPoseOffsets;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Weapon|Grip", meta=(ClampMin="0.01", Units="s"))
    float BlendSeconds = 0.15f;

    // Animation notifies/Blueprint actions can release the support hand for a future reload montage.
    UFUNCTION(BlueprintCallable, Category="OB|Weapon|Grip")
    void SetGripReleased(bool bReleased) { bGripReleased = bReleased; }

    bool ShouldApplyGrip() const;
    void ApplyWeaponAttachment();
    USkeletalMeshComponent* GetWeapon() const;
    bool IsUsingRPGGrip() const;
    const FTransform& GetWeaponAttachment() const;
    const FTransform& GetSupportHand() const;
    const TMap<FName, FRotator>& GetFingerOffsets() const;

private:
    bool bGripReleased = false;
};

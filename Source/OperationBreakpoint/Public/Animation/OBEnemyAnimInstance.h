#pragma once

#include "CoreMinimal.h"
#include "Animation/OBWeaponGripAnimInstance.h"
#include "Character/OBEnemyCharacter.h"
#include "OBEnemyAnimInstance.generated.h"

class UAnimMontage;

struct FOBEnemyPatternPose
{
    UAnimSequence* Base=nullptr;
    UAnimSequence* Action=nullptr;
    float BaseTime=0.f,ActionTime=0.f,BlendAlpha=1.f;
    bool bArmsOnly=false;
};

// Copied by the native proxy on the game thread. Contains no UObject references for pose evaluation.
struct OPERATIONBREAKPOINT_API FOBEnemyAimContext
{
    bool bActive = false;
    FVector OriginWorld = FVector::ZeroVector;
    FVector RawTargetWorld = FVector::ZeroVector;
    FVector TargetWorld = FVector::ZeroVector;
    FVector TargetComponentSpace = FVector::ZeroVector;
    FTransform MeshToWorld = FTransform::Identity;
    float AimPitch = 0.f;
    float AimYaw = 0.f;
    float Alpha = 0.f;
    float MaxPitch = 70.f;
    float MaxYaw = 60.f;
};

UCLASS(Blueprintable)
class OPERATIONBREAKPOINT_API UOBEnemyAnimInstance : public UOBWeaponGripAnimInstance
{
    GENERATED_BODY()

public:
    virtual void NativeInitializeAnimation() override;
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;

    float PlayAttackMontage();
    float PlayDeadMontage();

    void StopAttackMontage(float BlendOutTime);

    // Called once per animation update by the proxy's game-thread PreUpdate, never by worker evaluation.
    void UpdateEnemyAimContext(float DeltaSeconds);
    const FOBEnemyAimContext& GetEnemyAimContext() const { return EnemyAimContext; }
    UAnimSequence* GetRPGPoseSequence() const;
    float GetRPGPoseTime() const { return RPGShotElapsed; }
    void BeginPatternAnimation() { PatternAnimationElapsed=0.f; }
    FOBEnemyPatternPose GetPatternPose() const;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Animation|Aim")
    bool bEnableUpperBodyAim = true;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="OB|Animation|Aim", meta=(ClampMin="0",ClampMax="89"))
    float MaxAimPitch = 70.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="OB|Animation|Aim", meta=(ClampMin="0",ClampMax="89"))
    float MaxAimYaw = 60.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="OB|Animation|Aim", meta=(ClampMin="0"))
    float AimInterpSpeed = 8.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="OB|Animation|Aim", meta=(ClampMin="0.01",Units="s"))
    float AimBlendSeconds = .15f;
    UPROPERTY(Transient, BlueprintReadOnly, Category="OB|Animation|Aim")
    float AimPitch = 0.f;
    UPROPERTY(Transient, BlueprintReadOnly, Category="OB|Animation|Aim")
    float AimYaw = 0.f;
    UPROPERTY(Transient, BlueprintReadOnly, Category="OB|Animation|Aim")
    float AimAlpha = 0.f;

protected:
    UPROPERTY(Transient, BlueprintReadOnly, Category = "OB|Animation")
    TObjectPtr<AOBEnemyCharacter> EnemyCharacter;

    UPROPERTY(BlueprintReadOnly, Category = "OB|Animation")
    float GroundSpeed = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "OB|Animation")
    bool bIsMoving = false;

    UPROPERTY(BlueprintReadOnly, Category = "OB|Animation")
    bool bIsInAir = false;

    UPROPERTY(BlueprintReadOnly, Category = "OB|Animation")
    EOBEnemyCombatState CombatState = EOBEnemyCombatState::Idle;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "OB|Animation|Montage")
    TObjectPtr<UAnimMontage> AttackMontage;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "OB|Animation|Montage")
	TObjectPtr<UAnimMontage> DeadMontage;

private:
    void ResetEnemyAimContext();
    FOBEnemyAimContext EnemyAimContext;
    float RPGShotElapsed=0.f;
    bool bPlayingRPGShot=false;
    float PatternAnimationElapsed=0.f;
};

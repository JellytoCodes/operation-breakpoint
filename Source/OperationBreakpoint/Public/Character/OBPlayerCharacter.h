// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Character/OBCharacter.h"
#include "OBPlayerCharacter.generated.h"

class UOBPlayerSplineComponent;
class UCameraComponent;
class UCapsuleComponent;
class UOBMediaPipeInputComponent;
class UOBActionComponent;
class UOBStageFlowComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
struct FDamageEvent;
struct FTimerHandle;

UCLASS()
class OPERATIONBREAKPOINT_API AOBPlayerCharacter : public AOBCharacter
{
	GENERATED_BODY()

public :
    AOBPlayerCharacter();

    virtual USkeletalMeshComponent* GetWeaponMesh() const override { return WeaponMesh; }

    UCameraComponent* GetPlayerCamera() const { return PlayerCamera; }
    UOBActionComponent* GetActionComponent() const { return ActionComponent; }
    UOBStageFlowComponent* GetStageFlowComponent() const { return StageFlowComponent; }

    bool IsCollisionCameraNearBody() const { return bCollisionCameraNearBody; }
	UOBPlayerSplineComponent* GetPlayerSplineComponent() const { return PlayerSplineComponent; }

    virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;
    virtual FVector GetVelocity() const override;

	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

	virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
    virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;

private:
    void ConfigureEnemyBulletHitBox();

    // Separate from the Pawn capsule so movement and trigger collision remain unchanged.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="OB|HitBoxes", meta=(AllowPrivateAccess="true"))
    TObjectPtr<UCapsuleComponent> EnemyBulletHitBox;

    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="OB|Stage",meta=(AllowPrivateAccess="true"))
    TObjectPtr<UOBStageFlowComponent> StageFlowComponent;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="OB|Weapon", meta=(AllowPrivateAccess="true"))
    TObjectPtr<USkeletalMeshComponent> WeaponMesh;

    friend class FOBTrainingImpactSmoke;
    friend class FOBPlayerEdgeLookTest;
    friend class FOBPlayerCameraCollisionTest;
    friend class FOBCombatCollisionTest;

    void ApplyCameraCollision(float DeltaTime, FMinimalViewInfo& View);

    UPROPERTY(EditAnywhere, Category="OB|Camera|Collision")
    bool bEnableCameraCollision = true;

    // Objects must block this trace channel and have query collision enabled.
    UPROPERTY(EditAnywhere, Category="OB|Camera|Collision", meta=(EditCondition="bEnableCameraCollision"))
    TEnumAsByte<ECollisionChannel> CameraCollisionChannel = ECC_Camera;

    // Minimum sphere radius; increased as needed to protect the near-plane corners.
    UPROPERTY(EditAnywhere, Category="OB|Camera|Collision", meta=(EditCondition="bEnableCameraCollision", ClampMin="1.0", Units="cm"))
    float CameraCollisionProbeRadius = 16.f;

    UPROPERTY(EditAnywhere, Category="OB|Camera|Collision", meta=(EditCondition="bEnableCameraCollision", ClampMin="0.0", Units="cm"))
    float CameraCollisionPadding = 3.f;

    // Collision retracts immediately. Only the return to the authored distance is smoothed.
    UPROPERTY(EditAnywhere, Category="OB|Camera|Collision", meta=(EditCondition="bEnableCameraCollision", ClampMin="0.1"))
    float CameraCollisionReturnSpeed = 6.f;

    float CollisionCameraDistance = 0.f;
    FVector PreviousCameraAnchor = FVector::ZeroVector;
    FVector LastSafeCameraLocation = FVector::ZeroVector;
    bool bCameraCollisionInitialized = false;
    bool bHasSafeCameraLocation = false;
    bool bCollisionCameraNearBody = false;
    bool bCameraCollisionUnresolved = false;

    void ApplyCombatCamera(float DeltaTime, bool bCombatActive, bool bHasCursor,
        float NormalizedCursorX, FMinimalViewInfo& View);

    UPROPERTY(EditDefaultsOnly, Category = "OB|Camera|EdgeLook")
    bool bEnableEdgeLook = true;

    // Each outer edge occupies this fraction of the viewport. 0.15 leaves a 70% central dead zone.
    UPROPERTY(EditDefaultsOnly, Category = "OB|Camera|EdgeLook", meta = (ClampMin = "0.01", ClampMax = "0.49"))
    float EdgeLookRegionFraction = 0.15f;

    UPROPERTY(EditDefaultsOnly, Category = "OB|Camera|EdgeLook", meta = (ClampMin = "0.0", ClampMax = "15.0", Units = "deg"))
    float EdgeLookMaxYaw = 10.0f;

    UPROPERTY(EditDefaultsOnly, Category = "OB|Camera|EdgeLook", meta = (ClampMin = "0.1", ClampMax = "30.0"))
    float EdgeLookInterpSpeed = 12.0f;

    float CurrentEdgeLookYaw = 0.0f;

	void PlayHitScreenEffect();
	void UpdateHitScreenEffect();

	UPROPERTY(EditDefaultsOnly, Category = "OB|Damage", meta = (ClampMin = "0.05"))
	float HitScreenEffectDuration = 0.25f;

	float HitScreenEffectElapsed = 0.0f;
	FTimerHandle HitScreenEffectTimer;
	
	UPROPERTY(EditDefaultsOnly, Category = "OB|Damage")
	TObjectPtr<UMaterialInterface> ScreenDamageMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ScreenDamageMID;

	UPROPERTY(EditDefaultsOnly, Category = "OB|Damage", meta = (ClampMin = "1.0"))
	float MaxHealth = 100.0f;

	UPROPERTY(VisibleInstanceOnly, Category = "OB|Damage")
	float CurrentHealth = 100.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OB|Camera", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> PlayerCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OB|MediaPipe", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UOBMediaPipeInputComponent> MediaPipeInputComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OB|Rail", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UOBPlayerSplineComponent> PlayerSplineComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OB|Action", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UOBActionComponent> ActionComponent;
};

#pragma once

#include "CoreMinimal.h"
#include "Character/OBCharacter.h"
#include "AI/OBEnemyBehaviorSettings.h"
#include "AI/OBEnemyArchetype.h"
#include "Core/OBCombatCollision.h"
#include "OBEnemyCharacter.generated.h"

class AOBEnemyCharacter;
class AOBBulletActor;
class UBoxComponent;
class UNiagaraSystem;
class UParticleSystem;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UMeshComponent;
class UAnimSequence;
class UPhysicalMaterial;
class UAudioComponent;
class USceneComponent;
class USoundBase;
struct FBodyInstance;
struct FPointDamageEvent;

DECLARE_MULTICAST_DELEGATE_OneParam(FOBOnEnemyKilled, AOBEnemyCharacter*);

USTRUCT(BlueprintType)
struct OPERATIONBREAKPOINT_API FOBEnemyHitFeedback
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "OB|HitFeedback")
    FName HitBoxName = NAME_None;

    UPROPERTY(BlueprintReadOnly, Category = "OB|HitFeedback")
    FName HitZoneTag = NAME_None;

    // World-space contact on the hit box, not on the rendered character surface.
    UPROPERTY(BlueprintReadOnly, Category = "OB|HitFeedback")
    FVector ImpactPoint = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "OB|HitFeedback")
    FVector ImpactNormal = FVector::UpVector;

    UPROPERTY(BlueprintReadOnly, Category = "OB|HitFeedback")
    float Damage = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "OB|HitFeedback")
    float RemainingHealth = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "OB|HitFeedback")
    bool bFatal = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOBEnemyHitConfirmedSignature, const FOBEnemyHitFeedback&, Feedback);

UENUM(BlueprintType)
enum class EOBEnemyCombatState : uint8
{
    Idle,
    PreparingAttack,
    Attacking,
    MovingToCover,
    InCover,
    Reloading,
    Dead
};

UCLASS()
class OPERATIONBREAKPOINT_API AOBEnemyCharacter : public AOBCharacter
{
    GENERATED_BODY()

public:
    AOBEnemyCharacter();

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="OB|Appearance", meta=(ClampMin="0.1"))
    float CombatVisualScale=1.2f;

    virtual USkeletalMeshComponent* GetWeaponMesh() const override { return WeaponMesh; }
    FVector GetDirectAimTarget(const ACharacter& Target) const;
    FName GetMuzzleSocketName() const { return MuzzleSocketName; }
    UFUNCTION(BlueprintPure, Category="OB|Weapon|Aim")
    int32 GetLastShotAimChancePercent() const { return LastShotAimChancePercent; }

    virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

    UFUNCTION(BlueprintCallable, Category = "OB|Death")
	void DropWeapon();

	UFUNCTION(BlueprintCallable, Category = "OB|Death")
	void BeginDeathPhysics();

    UFUNCTION(BlueprintCallable, Category = "OB|AI")
    void SetCombatState(EOBEnemyCombatState NewState);

    UFUNCTION(BlueprintPure, Category = "OB|AI")
    EOBEnemyCombatState GetCombatState() const { return CombatState; }

    FOBOnEnemyKilled OnKilled;

    virtual void SetEncounterCombatant(bool bCombatant);
    UFUNCTION(BlueprintPure, Category="OB|AI|Encounter")
    bool IsEncounterCombatant() const { return bEncounterCombatant; }

    UFUNCTION(BlueprintCallable, Category = "OB|Weapon")
	void FireBullet();

    // Both sounds follow the animated character socket. Assign Sound and attenuation on each component.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="OB|Audio")
    TObjectPtr<USceneComponent> EnemyAudioOrigin;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="OB|Audio")
    TObjectPtr<UAudioComponent> EnemyFireAudio;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="OB|Audio")
    TObjectPtr<UAudioComponent> EnemyVoiceAudio;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="OB|Audio") TObjectPtr<UAudioComponent> EnemyReloadAudio;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="OB|Audio") TObjectPtr<USoundBase> EnemyRifleFireSound;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="OB|Audio") TObjectPtr<USoundBase> EnemyRPGReloadSound;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="OB|Audio") TObjectPtr<USoundBase> EnemyRPGFireSound;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="OB|Audio") TObjectPtr<USoundBase> EnemyRPGEntryVoice;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="OB|Audio") TArray<TObjectPtr<USoundBase>> EnemyEntryVoiceLines;
    static int32 PlayEncounterEntryVoices(const TArray<TObjectPtr<AOBEnemyCharacter>>& Enemies);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Audio")
    FName EnemyAudioSocketName=TEXT("EnemyAudioSocket");

    // A null argument plays the Sound configured on EnemyVoiceAudio. An explicit line replaces it.
    UFUNCTION(BlueprintCallable, Category="OB|Audio")
    bool PlayEnemyVoice(USoundBase* VoiceLine=nullptr);
    UFUNCTION(BlueprintCallable, Category="OB|Audio")
    void StopEnemyVoice();
    UFUNCTION(BlueprintCallable, Category="OB|Audio")
    void StopEnemyAudio();

    void StartStateTree();

    UFUNCTION(BlueprintCallable, Category="OB|AI|Encounter")
    void StartEncounterBehavior();

    UFUNCTION(BlueprintCallable, Category="OB|AI|Archetype")
    void ConfigureEnemyArchetype(EOBEnemyArchetype InArchetype);
    UFUNCTION(BlueprintPure, Category="OB|AI|Archetype")
    EOBEnemyArchetype GetEnemyArchetype() const { return EnemyArchetype; }
    UFUNCTION(BlueprintPure, Category="OB|AI|Archetype")
    bool IsRPGTrooper() const { return EnemyArchetype == EOBEnemyArchetype::RPGTrooper; }
    UFUNCTION(BlueprintPure, Category="OB|AI|Archetype")
    bool IsRPGAttackActive() const { return bRPGAttackActive || (bPatternCombatActive && IsRPGTrooper()); }
    UFUNCTION(BlueprintPure, Category="OB|AI|Archetype")
    int32 GetRPGShotsFired() const { return RPGShotsFired; }
    UFUNCTION(BlueprintCallable, Category="OB|AI|Archetype")
    void StopArchetypeAttack();
    bool StartArchetypeAttack();

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="OB|AI|Patterns")
    TObjectPtr<UOBEnemyCombatPatternSet> CombatPatterns;
    EOBEnemyCombatPattern ResolveCombatPattern() const;
    bool CanRunCombatPattern(bool bAllowArrivalFrame=true) const;
    void BeginCombatPattern();
    void StopCombatPattern();
    bool IsCombatPatternActive() const { return bPatternCombatActive; }
    EOBEnemyPosture GetCombatPosture() const { return CombatPosture; }
    EOBEnemyPatternAction GetPatternAction() const { return PatternAction; }
    int32 GetPatternShotsFired() const { return PatternShotsFired; }
    float BeginPatternAction(EOBEnemyPatternAction Action,EOBEnemyPosture Posture);
    bool FirePatternRound();
    UAnimSequence* GetPostureSequence() const;
    UAnimSequence* GetPostureTransitionSequence() const;
    UAnimSequence* GetPatternActionSequence() const;
    bool IsPostureTransitioning() const { return bPostureTransitioning; }

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|AI|Archetype")
    EOBEnemyArchetype EnemyArchetype = EOBEnemyArchetype::Rifleman;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="OB|Weapon|RPG")
    TObjectPtr<UStaticMeshComponent> RPGLauncherMesh;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="OB|Weapon|RPG")
    TObjectPtr<USkeletalMeshComponent> RPGWeaponMesh;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="OB|Weapon|RPG")
    TObjectPtr<UAnimSequence> RPGShotReloadAnimation;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="OB|Weapon|RPG")
    TObjectPtr<UAnimSequence> RPGWeaponShotReloadAnimation;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="OB|Weapon|RPG")
    FVector RPGBarrelAxis=FVector::UpVector;
    UMeshComponent* GetRPGVisualMesh() const;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="OB|Weapon|RPG", meta=(ClampMin="0.01", Units="s"))
    float RPGFirstAttackDelay = 2.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="OB|Weapon|RPG", meta=(ClampMin="0.01", Units="s"))
    float RPGAttackInterval = 4.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="OB|Weapon|RPG")
    TSubclassOf<AOBBulletActor> RPGRocketClass;
    // Launcher-local offset: the native placeholder tube's barrel points along local +Z.
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="OB|Weapon|RPG")
    FVector RPGMuzzleOffset = FVector(0, 0, 55.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|AI|Encounter")
    FOBEnemyBehaviorSettings EncounterBehavior;

    UFUNCTION(BlueprintPure, Category = "OB|Health")
    float GetCurrentHealth() const { return CurrentHealth; }

    UFUNCTION(BlueprintPure, Category = "OB|Health")
    float GetMaxHealth() const { return MaxHealth; }

    UFUNCTION(BlueprintPure, Category = "OB|HitFeedback")
    FOBEnemyHitFeedback GetLastHitFeedback() const { return LastHitFeedback; }

    // Fires only after positive point damage has been applied, including the lethal shot.
    UPROPERTY(BlueprintAssignable, Category = "OB|HitFeedback")
    FOBEnemyHitConfirmedSignature OnHitConfirmed;

protected:
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void PostInitializeComponents() override;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    void InitializeEnemyAudio();
    void AttachEnemyAudioOrigin();
    void PlayEnemyFireSound();
    void PlayEnemyReloadSound();
    UPROPERTY(Transient) TObjectPtr<USoundBase> PendingEntryVoice;
    FTimerHandle EntryVoiceTimer;
    FTimerHandle RPGLegacyReloadTimer;
    bool bEntryVoiceAssigned=false;
    bool bRPGEntryVoiceAssigned=false;
    friend class FOBEnemyEngagementAudioTest;
    friend class AOBAIController;
    friend class FOBEnemyCombatPatternTest;
    void SetCombatPosture(EOBEnemyPosture Posture);
    bool bPatternCombatActive=false;
    bool bPatternShotAuthorized=false;
    bool bPostureTransitioning=false;
    EOBEnemyPosture CombatPosture=EOBEnemyPosture::Stand;
    EOBEnemyPosture PreviousCombatPosture=EOBEnemyPosture::Stand;
    EOBEnemyPatternAction PatternAction=EOBEnemyPatternAction::Posture;
    int32 PatternShotsFired=0;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="OB|AI|Encounter", meta=(AllowPrivateAccess="true"))
    bool bEncounterCombatant = true;
    friend class FOBEnemyShotAimTest;
    friend class FOBRPGCombatTest;
    friend class FOBStageNarrationRPGMapSmoke;
    friend class FOBRagdollStabilityTest;
    friend class FOBRagdollInitialOverlapTest;

    void UpdateArchetypeVisuals();
    void StartRPGAttack();
    bool CanRPGAttack(bool bRequireCombatPhase = true) const;
    void FireRPGRound();
    void RecoverRPGAim();
    bool bRPGAttackActive = false;
    bool bRPGLaunchAuthorized = false;
    int32 RPGShotsFired = 0;
    FTimerHandle RPGAttackTimer;
    FTimerHandle RPGRecoveryTimer;
    bool bSavedRifleDefaults = false;
    bool bArchetypeConfigured = false;
    bool bRPGVisualsApplied = false;
    bool bSavedRifleVisibility = true;
    float SavedRifleMaxHealth = 100.f;
    FVector SelectShotAimLocation(const ACharacter& Player, int32 ProbabilityRoll, int32 SurroundingIndex);

    // Probability of aiming directly, not a guarantee that the projectile hits.
    UPROPERTY(EditAnywhere, Category = "OB|Weapon|Aim", meta = (ClampMin = "0", ClampMax = "100", Units="%"))
    int32 DirectAimMinPercent = 2;
    UPROPERTY(EditAnywhere, Category = "OB|Weapon|Aim", meta = (ClampMin = "0", ClampMax = "100", Units="%"))
    int32 DirectAimMaxPercent = 2;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="OB|Weapon|Aim", meta=(AllowPrivateAccess="true",Units="%"))
    int32 LastShotAimChancePercent = 0;

    // Six offsets from the normal player aim point, in player yaw-local coordinates (cm).
    // X = forward, Y = right, Z = up. Fixed size in the Details panel.
    UPROPERTY(EditDefaultsOnly, EditFixedSize, Category = "OB|Weapon|Aim")
    TArray<FVector> SurroundingAimOffsets;

    // World positions refreshed at each shot so they follow rail movement and facing.
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "OB|Weapon|Aim",
        meta = (AllowPrivateAccess = "true"))
    TArray<FVector> SurroundingAimPoints;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "OB|Weapon|Aim",
        meta = (AllowPrivateAccess = "true"))
    bool bLastShotAimedAtPlayer = false;

    UPROPERTY(EditDefaultsOnly, Category = "OB|Weapon|Aim|Debug")
    bool bDrawShotAimDebug = false;

    void Die();
    void ConfigureHitBoxes();
    UBoxComponent* CreateHitBox(FName Name, FName BoneName, FName ZoneTag,
        const FVector& Extent, const FVector& Offset = FVector::ZeroVector);
    float GetHitBoxDamageMultiplier(const UBoxComponent* HitBox) const;
    void PlayHitFeedback(const FPointDamageEvent& PointDamage, const UBoxComponent* HitBox, float AppliedDamage);

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "OB|HitFeedback",
        meta = (AllowPrivateAccess = "true"))
    FOBEnemyHitFeedback LastHitFeedback;

    // Use a one-shot effect. Niagara takes precedence when both effect fields are assigned.
    UPROPERTY(EditDefaultsOnly, Category = "OB|HitFeedback")
    TObjectPtr<UNiagaraSystem> HitNiagaraEffect;

    UPROPERTY(EditDefaultsOnly, Category = "OB|HitFeedback")
    TObjectPtr<UParticleSystem> HitParticleEffect;

    UPROPERTY(EditDefaultsOnly, Category = "OB|HitFeedback", meta = (ClampMin = "0.001"))
    FVector HitEffectScale = FVector::OneVector;

    // The effect's local +Z points along the outward impact normal, then this local offset is applied.
    UPROPERTY(EditDefaultsOnly, Category = "OB|HitFeedback")
    FRotator HitEffectRotationOffset = FRotator::ZeroRotator;

    UPROPERTY(EditDefaultsOnly, Category = "OB|HitFeedback", meta = (ClampMin = "0.0", Units = "cm"))
    float HitEffectSurfaceOffset = 1.0f;

    UPROPERTY(EditDefaultsOnly, Category = "OB|HitFeedback|Debug")
    bool bShowHitDebug = false;

    UPROPERTY(EditDefaultsOnly, Category = "OB|HitFeedback|Debug",
        meta = (ClampMin = "0.1", Units = "s", EditCondition = "bShowHitDebug"))
    float HitDebugDuration = 1.5f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "OB|Health",
        meta = (AllowPrivateAccess = "true", ClampMin = "1.0"))
    float MaxHealth = 100.0f;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "OB|Health",
        meta = (AllowPrivateAccess = "true"))
    float CurrentHealth = 100.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OB|HitBoxes",
        meta = (AllowPrivateAccess = "true"))
    TArray<TObjectPtr<UBoxComponent>> HitBoxes;

    // Keys are native hit box component names. Adjust bones here and box transforms in the BP viewport.
    UPROPERTY(EditDefaultsOnly, Category = "OB|HitBoxes")
    TMap<FName, FName> HitBoxBones;

    // Must match the player's ShotTraceChannel. Other collision channels are ignored by the boxes.
    UPROPERTY(VisibleDefaultsOnly, Category = "OB|HitBoxes")
    TEnumAsByte<ECollisionChannel> HitTraceChannel = OBCombatCollision::PlayerShotTarget;

    UPROPERTY(EditDefaultsOnly, Category = "OB|Damage", meta = (ClampMin = "0.0"))
    float HeadDamageMultiplier = 2.5f;

    UPROPERTY(EditDefaultsOnly, Category = "OB|Damage", meta = (ClampMin = "0.0"))
    float BodyDamageMultiplier = 1.0f;

    UPROPERTY(EditDefaultsOnly, Category = "OB|Damage", meta = (ClampMin = "0.0"))
    float ArmDamageMultiplier = 0.5f;

    UPROPERTY(EditDefaultsOnly, Category = "OB|Damage", meta = (ClampMin = "0.0"))
    float LegDamageMultiplier = 0.5f;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "OB|AI",
        meta = (AllowPrivateAccess = "true"))
    EOBEnemyCombatState CombatState = EOBEnemyCombatState::Idle;

    bool bDead = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OB|Weapon", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USkeletalMeshComponent> WeaponMesh;

	UPROPERTY(EditDefaultsOnly, Category = "OB|Weapon")
	TSubclassOf<AOBBulletActor> BulletClass;

	UPROPERTY(EditDefaultsOnly, Category = "OB|Weapon")
	FName MuzzleSocketName = TEXT("b_gun_muzzleflash");

	UPROPERTY(EditDefaultsOnly, Category = "OB|Weapon", meta = (ClampMin = "0.0"))
	float AimHeightOffset = 50.0f;

    void UpdateDeathPhysicsBlend();
    UPhysicalMaterial* GetOrCreateRagdollContactMaterial();
    void ConfigureRagdollBody(FBodyInstance* BodyInstance);
    FVector ResolveInitialRagdollPenetration();

	bool bWeaponDropped = false;
	bool bDeathPhysicsStarted = false;
	float DeathPhysicsElapsed = 0.0f;
	FTimerHandle DeathPhysicsBlendTimer;

	UPROPERTY(EditDefaultsOnly, Category = "OB|Death")
	FName RagdollRootBoneName = TEXT("pelvis");

	UPROPERTY(EditDefaultsOnly, Category = "OB|Death", meta = (ClampMin = "0.05"))
	float DeathPhysicsBlendDuration = 0.65f;

	UPROPERTY(EditDefaultsOnly, Category = "OB|Death|Physics", meta = (ClampMin = "1.0"))
	float RagdollMassScale = 5.0f;

	UPROPERTY(EditDefaultsOnly, Category = "OB|Death|Physics", meta = (ClampMin = "0.0"))
	float RagdollLinearDamping = 6.0f;

	UPROPERTY(EditDefaultsOnly, Category = "OB|Death|Physics", meta = (ClampMin = "0.0"))
	float RagdollAngularDamping = 10.0f;

	UPROPERTY(EditDefaultsOnly, Category = "OB|Death|Physics", meta = (ClampMin = "0.0"))
	float RagdollMaxDepenetrationVelocity = 25.0f;

    // Kept only to deserialize existing Blueprint defaults. World-axis locking is never applied to ragdolls.
    UPROPERTY()
    bool bLockRagdollHorizontalMovement = false;

    UPROPERTY(EditDefaultsOnly, Category="OB|Death|Physics", meta=(ClampMin="1",ClampMax="64"))
    int32 RagdollPositionSolverIterations = 16;
    UPROPERTY(EditDefaultsOnly, Category="OB|Death|Physics", meta=(ClampMin="1",ClampMax="64"))
    int32 RagdollVelocitySolverIterations = 8;
    UPROPERTY(EditDefaultsOnly, Category="OB|Death|Physics", meta=(ClampMin="0",ClampMax="8"))
    int32 RagdollProjectionSolverIterations = 1;
    UPROPERTY(EditDefaultsOnly, Category="OB|Death|Physics", meta=(ClampMin="0.1",ClampMax="100",Units="rad/s"))
    float RagdollMaxAngularVelocity = 8.f;
    UPROPERTY(EditDefaultsOnly, Category="OB|Death|Physics", meta=(ClampMin="0",ClampMax="1"))
    float RagdollFriction = .4f;
    UPROPERTY(EditDefaultsOnly, Category="OB|Death|Physics", meta=(ClampMin="0",ClampMax="1"))
    float RagdollStaticFriction = .4f;
    UPROPERTY(EditDefaultsOnly, Category="OB|Death|Physics", meta=(ClampMin="0",ClampMax="1"))
    float RagdollRestitution = 0.f;
    UPROPERTY(Transient)
    TObjectPtr<UPhysicalMaterial> RagdollContactMaterial;
};

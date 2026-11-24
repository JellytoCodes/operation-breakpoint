#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Core/OBCombatCollision.h"
#include "UObject/StrongObjectPtr.h"
#include "OBPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class UUserWidget;
class AOBHUD;
class USoundBase;
struct FOBOpenFireEvents;
class AOBEnemyCharacter;
class UPostProcessComponent;
class UMaterialInterface;

UENUM(BlueprintType)
enum class EOBGunInputMode : uint8
{
    Disabled,
    Interaction,
    Shooting,
    Travel
};

DECLARE_MULTICAST_DELEGATE(FOBPlayerTrainingSignal);
DECLARE_MULTICAST_DELEGATE_OneParam(FOBPlayerShotResolved, const FHitResult&);

UCLASS()
class OPERATIONBREAKPOINT_API AOBPlayerController : public APlayerController
{
    GENERATED_BODY()

public:
    static constexpr int32 StandardMagazineCapacity = 20;
    AOBPlayerController();
    virtual bool InputKey(const FInputKeyEventArgs& Params) override;
    virtual void PlayerTick(float DeltaTime) override;
    bool IsTriggerHeld() const { return bTriggerHeld; }
    UFUNCTION(BlueprintCallable, Category="OB|Navigation") void ReturnToMainMenu();
    UFUNCTION(BlueprintCallable, Category="OB|Navigation") void RestartCurrentStage();
    UFUNCTION(BlueprintCallable, Category="OB|Navigation") void RestartZone6Demo();
    bool HandleOperatorShortcut(const FKey& Key, EInputEvent Event);
    void NavigateToMap(FName Map,const FString& Options=FString());

    // Called by the opt-in OpenFire input component, on the game thread.
    void ApplyOpenFireInput(bool bHeld, bool bPressed, bool bReload, bool bCalibration);
    void ApplyOpenFireInput(const FOBOpenFireEvents& Events);
    void CancelOpenFireInput();

    UFUNCTION(BlueprintPure, Category="OB|Input|Gun") EOBGunInputMode GetGunInputMode() const;
    UFUNCTION(BlueprintPure, Category="OB|Input|Gun") bool CanReloadInCurrentMode() const;
    // Default preserves the current policy; enabling this allows reload on a
    // live spline journey, without enabling firing or cinematic input.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Input|Gun")
    bool bAllowReloadWhileMoving=false;

    UFUNCTION(BlueprintCallable, Category = "OB|Shooting")
    void SetCrouchMode(bool bEnabled) { bIsCrouchMode = bEnabled; }

    UFUNCTION(BlueprintCallable, Category = "OB|UI")
    void ShowResultWidget();

    UFUNCTION(BlueprintPure, Category = "OB|UI")
    AOBHUD* GetOBHUD() const;

    UFUNCTION(BlueprintCallable, Category = "OB|Weapon")
    void CompleteReload();

    UFUNCTION(BlueprintCallable, Category = "OB|Weapon")
    void NotifyAimCalibrationCompleted();

    UFUNCTION(BlueprintPure, Category = "OB|Weapon")
    int32 GetCurrentAmmo() const { return CurrentAmmo; }
    int32 GetMagazineCapacity() const { return MagazineCapacity; }

    void SetTrainingControls(bool bEnabled, bool bCanShoot = false, bool bCanReload = false, bool bRequireShootingPosture = true);
    // ReloadAmount replaces the magazine contents; it never adds to the remaining rounds.
    void SetTrainingAmmo(int32 Ammo, int32 Capacity, int32 ReloadAmount=INDEX_NONE);
    void ResetQAInputState(bool bResetPosture=true);

    FOBPlayerTrainingSignal OnTriggerPressed;
    FOBPlayerTrainingSignal OnReloadCompleted;
    FOBPlayerTrainingSignal OnAimCalibrationCompleted;
    FOBPlayerShotResolved OnShotResolved;

protected:
    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void OnUnPossess() override;

private:
    friend class FOBEnemyAimOutlineTest;
    friend class FOBEnemyAimOutlineMapSmoke;
    void SetOutlinedEnemy(AOBEnemyCharacter* Enemy);
    void UpdateAimOutlineAlongRay(const FVector& Start,const FVector& End);
    bool TraceShotTarget(const FVector& Start,const FVector& End,FHitResult& Hit) const;
    TWeakObjectPtr<AOBEnemyCharacter> OutlinedEnemy;
    UPROPERTY(Transient) TObjectPtr<UPostProcessComponent> AimOutlinePostProcess;
    UPROPERTY(EditDefaultsOnly, Category="OB|Appearance|Outline")
    TSoftObjectPtr<UMaterialInterface> AimOutlineMaterial;
    friend class FOBStageAmmoTest;
    friend class FOBTrainingAmmoTest;
    friend class FOBTrainingAmmoMapSmoke;
    friend class FOBStageAmmoMapSmoke;
    friend class FOBOpenFireInputModesTest;
    friend class FOBFullFlowMapSmoke;
    friend class FOBPackagePerformance;
    friend class FOBTrainingImpactSmoke;
    friend class FOBTrainingFlowTest;
    friend class FOBTrainingPopUpTest;
    friend class FOBTrainingVisualSmoke;
    friend class FOBTrainingKeyboardSmoke;
    friend class FOBTrainingSplinePrototypeTest;
    friend class FOBTrainingZeroingMapSmoke;
    friend class FOBTrainingAccuracyToleranceTest;
    friend class FOBWeaponAudioTest;
    friend class FOBTrainingWeaponViewSmoke;
    friend class FOBGuideAnimationSmoke;
    friend class FOBHostageShotIsolationTest;
    friend class FOBCombatCollisionTest;
    friend class FOBStageZone6MapSmoke;
    bool IsActionInputAllowed() const;
    bool IsShootingPostureAllowed() const;
    bool bTrainingControls = false;
    bool bTriggerHeld = false;
    bool bApplyingOpenFireTrigger = false;
    bool bNavigationPending=false;
    void HandleTriggerState(bool bHeld);
    void CancelGunTrigger();
    EOBGunInputMode LastGunInputMode=EOBGunInputMode::Disabled;
    bool bGunModeInitialized=false, bGunTriggerNeedsRelease=false;
    bool bTrainingCanShoot = false;
    bool bTrainingCanReload = false;
    bool bTrainingRequireShootingPosture = true;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "OB|Weapon", meta = (AllowPrivateAccess = "true"))
    int32 CurrentAmmo = StandardMagazineCapacity;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "OB|Weapon", meta = (AllowPrivateAccess = "true"))
    int32 MagazineCapacity = StandardMagazineCapacity;
    int32 ReloadAmmo = StandardMagazineCapacity;

    void FireAtCursor();
    void FireAlongRay(const FVector& TraceStart, const FVector& TraceEnd);
    bool BuildCursorTraceRay(FVector& OutTraceStart, FVector& OutTraceEnd) const;
    bool CheckWeaponFireAllowed();
    void PlayWeaponFireSound(bool bEmpty);

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "OB|Weapon|Audio", meta = (AllowPrivateAccess = "true"))
    TSoftObjectPtr<USoundBase> FireSound;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "OB|Weapon|Audio", meta = (AllowPrivateAccess = "true"))
    TSoftObjectPtr<USoundBase> EmptyFireSound;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "OB|Weapon|Audio", meta = (AllowPrivateAccess = "true"))
    TSoftObjectPtr<USoundBase> ReloadSound;

    // Keep the primed sounds alive across garbage collection before first input.
    TArray<TStrongObjectPtr<USoundBase>> LoadedWeaponSounds;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "OB|Input", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UInputAction> FireAction;

    // Retained for existing Blueprint data; reload uses right-click or the IR bridge.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "OB|Input", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UInputAction> ReloadCompletedAction;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "OB|Input", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UInputAction> CalibrationCompletedAction;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "OB|Input", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UInputMappingContext> CommonMappingContext;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "OB|UI", meta = (AllowPrivateAccess = "true"))
    TSubclassOf<UUserWidget> ResultWidgetClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OB|Shooting", meta = (AllowPrivateAccess = "true"))
    bool bIsCrouchMode = false;

    UPROPERTY(EditDefaultsOnly, Category = "OB|Shooting")
    float TraceDistance = 100000.0f;

    UPROPERTY(EditDefaultsOnly, Category = "OB|Shooting", meta = (ClampMin = "0.0"))
    float ShotDamage = 40.0f;

    UPROPERTY(VisibleDefaultsOnly, Category = "OB|Shooting")
    TEnumAsByte<ECollisionChannel> ShotTraceChannel = OBCombatCollision::PlayerShotTarget;

    UPROPERTY(EditDefaultsOnly, Category = "OB|Shooting")
    bool bTraceComplex = true;

    UPROPERTY(EditDefaultsOnly, Category = "OB|Debug")
    bool bDrawDebugTrace = false;

    UPROPERTY(EditDefaultsOnly, Category = "OB|Debug")
    float DebugDrawTime = 1.0f;
};

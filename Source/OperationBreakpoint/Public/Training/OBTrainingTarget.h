#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OBTrainingTarget.generated.h"

class UStaticMeshComponent;
class UDecalComponent;
class UMaterialInterface;
class USoundBase;
class AOBTrainingTarget;
DECLARE_MULTICAST_DELEGATE_TwoParams(FOBTargetPoseReached, AOBTrainingTarget*, bool);

/** A scoring board on a folding pivot; a hit counts only while fully raised. */
UCLASS()
class OPERATIONBREAKPOINT_API AOBTrainingTarget : public AActor
{
    GENERATED_BODY()

public:
    AOBTrainingTarget();

    virtual void Tick(float DeltaSeconds) override;
    virtual void OnConstruction(const FTransform& Transform) override;

    bool IsScoringHit(const FHitResult& Hit) const;
    virtual void HandleScoringHit(const FVector& ImpactPoint);
    // Only gameplay retractions request audio; setup, teardown and QA resets stay silent.
    virtual void SetTargetEnabled(bool bEnabled, bool bImmediate = false, bool bPlayLowerSound = false);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Training|Audio") TSoftObjectPtr<USoundBase> TargetHitSound;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Training|Audio") TSoftObjectPtr<USoundBase> TargetDownSound;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Training|Audio", meta=(ClampMin="0", ClampMax="1")) float SoundVolume=.8f;
    UDecalComponent* SpawnBulletHole(const FHitResult& Hit);
    UFUNCTION(BlueprintCallable, Category="OB|Training|Impact") void ClearBulletHoles();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Training|Impact") bool bShowBulletHoles=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Training|Impact") TSoftObjectPtr<UMaterialInterface> BulletHoleMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Training|Impact") FVector BulletHoleSize=FVector(.5f,4.f,4.f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Training|Impact",meta=(ClampMin="1")) int32 MaxBulletHoles=100;

    UFUNCTION(BlueprintPure, Category = "OB|Training") 
    bool IsTargetRaised() const;

    UFUNCTION(BlueprintPure, Category = "OB|Training") 
    float GetPoseProgress() const { return PoseProgress; }
    FVector GetScoringPlaneNormal() const;

    FOBTargetPoseReached OnTargetPoseReached;

    UFUNCTION(BlueprintImplementableEvent, Category = "OB|Training") 
    void OnTargetHit(const FVector& ImpactPoint);

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OB|Training") 
    TObjectPtr<UStaticMeshComponent> BaseMesh;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OB|Training") 
    TObjectPtr<USceneComponent> TargetPivot;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OB|Training") 
    TObjectPtr<UStaticMeshComponent> ArmMesh;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OB|Training") 
    TObjectPtr<UStaticMeshComponent> BoardMesh;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OB|Training|Motion", meta = (ClampMin = "0")) 
    float RaiseSeconds = 0.25f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OB|Training|Motion", meta = (ClampMin = "0")) 
    float LowerSeconds = 0.25f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OB|Training|Motion") 
    FRotator RaisedRotation;

    // Rotation in the raised pivot's local axes.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OB|Training|Motion") 
    FRotator FoldRotation = FRotator(-90, 0, 0);

protected:
    virtual void BeginPlay() override;
    bool IsPoseAnimating() const { return !FMath::IsNearlyEqual(PoseProgress, bTargetEnabled ? 1.f : 0.f); }

private:
    void PlayTargetSound(const TSoftObjectPtr<USoundBase>& Sound, const FVector& Location, const TCHAR* Event);
    void ApplyPose();
    bool bTargetEnabled = true;
    float PoseProgress = 1;
    UPROPERTY(Transient) TArray<TObjectPtr<UDecalComponent>> BulletHoles;
};

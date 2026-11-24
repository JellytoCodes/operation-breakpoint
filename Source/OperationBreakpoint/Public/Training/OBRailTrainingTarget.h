#pragma once

#include "CoreMinimal.h"
#include "Training/OBTrainingTarget.h"
#include "OBRailTrainingTarget.generated.h"

/** A full-height scoring board travelling back and forth on a stationary straight rail. */
UCLASS()
class OPERATIONBREAKPOINT_API AOBRailTrainingTarget : public AOBTrainingTarget
{
    GENERATED_BODY()

public:
    AOBRailTrainingTarget();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void SetTargetEnabled(bool bEnabled, bool bImmediate = false, bool bPlayLowerSound = false) override;
    virtual void HandleScoringHit(const FVector& ImpactPoint) override;

    UFUNCTION(BlueprintCallable, Category = "OB|Training|Rail")
    void StartMoving();

    UFUNCTION(BlueprintCallable, Category = "OB|Training|Rail")
    void StopMoving();

    UFUNCTION(BlueprintCallable, Category = "OB|Training|Rail")
    void ResetMovement();

    UFUNCTION(BlueprintPure, Category = "OB|Training|Rail")
    bool IsMoving() const;

    // Total end-to-end travel, in the base's local centimetres.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OB|Training|Rail", meta = (ClampMin = "0", Units = "cm"))
    float TravelDistance = 400.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OB|Training|Rail", meta = (ClampMin = "0", Units = "cm/s"))
    float MoveSpeed = 100.f;

    // Default local X runs left/right across the supplied mesh's +Y-facing front.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OB|Training|Rail")
    FVector MovementAxis = FVector::ForwardVector;

    // 0 = negative end, 0.5 = centre, 1 = positive end.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OB|Training|Rail", meta = (ClampMin = "0", ClampMax = "1"))
    float StartProgress = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OB|Training|Rail")
    bool bStartTowardPositive = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OB|Training|Rail")
    bool bAutoStart = true;

protected:
    virtual void BeginPlay() override;

private:
    void InitializeMovement();
    void ApplyMovement();
    void RefreshTick();
    FVector GetTravelAxis() const;

    FVector CenterLocation = FVector(0, 0, 14);
    double MovementPhase = 0.5;
    bool bMovementInitialized = false;
    bool bMovementRequested = false;
    bool bHitFolding = false;
};

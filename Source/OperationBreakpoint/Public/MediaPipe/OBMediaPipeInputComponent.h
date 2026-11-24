#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "OBMediaPipeInputComponent.generated.h"

class ACharacter;
class UOBMediaPipeOSCSubsystem;

UCLASS(ClassGroup = (OB), meta = (BlueprintSpawnableComponent))
class OPERATIONBREAKPOINT_API UOBMediaPipeInputComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    void SetPoseInputEnabled(bool bEnabled) { bPoseInputEnabled = bEnabled; }
    bool IsPoseInputEnabled() const { return bPoseInputEnabled; }
    // Reapply a held state when training returns from Prepare to an active exercise.
    void RefreshPoseState();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    friend class FOBTrainingMediaPipePoseTest;
    void HandleCrouchStateChanged(bool bIsCrouching);
    bool bPoseInputEnabled = true;

    UPROPERTY(Transient)
    TObjectPtr<ACharacter> OwnerCharacter;

    UPROPERTY(Transient)
    TObjectPtr<UOBMediaPipeOSCSubsystem> MediaPipeSubsystem;
};

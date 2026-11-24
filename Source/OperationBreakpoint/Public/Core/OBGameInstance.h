#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "OBGameInstance.generated.h"

/** Applies borderless fullscreen on the selected display and the Medium preset on every launch. */
UCLASS()
class OPERATIONBREAKPOINT_API UOBGameInstance : public UGameInstance
{
    GENERATED_BODY()
public:
    virtual void Init() override;
    virtual void OnStart() override;
    UFUNCTION(BlueprintPure, Category="OB|Learner")
    bool HasLearnerProfile() const { return !LearnerName.IsEmpty() && !LearnerId.IsEmpty(); }
    UFUNCTION(BlueprintPure, Category="OB|Learner")
    FString GetLearnerName() const { return LearnerName; }
    UFUNCTION(BlueprintPure, Category="OB|Learner")
    FString GetLearnerId() const { return LearnerId; }
    bool IsStageShortcutEnabled() const { return bStageShortcutEnabled; }
    void ToggleStageShortcut() { bStageShortcutEnabled = !bStageShortcutEnabled; }
    bool IsCrosshairEnabled() const { return bCrosshairEnabled; }
    void ToggleCrosshair() { bCrosshairEnabled = !bCrosshairEnabled; }
    static bool ReadLearnerProfileArgument(const TCHAR* CommandLine, FString& Path);
private:
    friend class FOBLauncherIdentityTest;
    bool LoadLearnerProfile(const FString& Path);
    UPROPERTY(Transient) FString LearnerName;
    UPROPERTY(Transient) FString LearnerId;
    bool bStageShortcutEnabled = true;
    bool bCrosshairEnabled = true;
    void ApplyFixedStartupSettings(const TCHAR* Phase);
};

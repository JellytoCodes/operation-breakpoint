#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CheatManager.h"
#include "OBQACheatManager.generated.h"

class AOBTrainingDirector;
class UOBPlayerOverlayWidget;
class UAudioComponent;
class UOBPlayerSplineComponent;
struct FKey;

UCLASS()
class OPERATIONBREAKPOINT_API UOBQACheatManager : public UCheatManager
{
    GENERATED_BODY()
public:
    static constexpr bool IsEnabledBuild() { return !UE_BUILD_SHIPPING && !UE_BUILD_TEST; }
    UFUNCTION(Exec)
    void QA(const FString& CommandLine = TEXT("Help"));
    static bool IsTrainingMap(const UWorld* World);
    bool IsUIPreviewActive() const { return UIPreview!=nullptr; }
    bool IsCameraTuningActive() const;
    bool HandleCameraTuningKey(const FKey& Key,bool bAdjust);
private:
    friend class FOBTitleGuideMediaSmoke;
    friend class FOBQACheatManagerTest;
    friend class FOBQAUIMapSmoke;
    friend class FOBQACameraTuningTest;
    friend class FOBQACameraTuningMapSmoke;
    bool ExecuteCommand(const FString& CommandLine, FString& Reply);
    AOBTrainingDirector* RequireTrainingMap(const FString& Command, FString& Reply) const;
    bool ShowUIPreview(int32 UIId, FString& Reply);
    void CloseUIPreview();
    UOBPlayerSplineComponent* GetCameraTuningSpline() const;
    bool ExecuteCameraCommand(const TArray<FString>& Args,FString& Reply);
    bool ExecuteBreachCommand(const TArray<FString>& Args,FString& Reply);
    UFUNCTION() void HandlePreviewOwnerEndPlay(AActor* Actor, EEndPlayReason::Type Reason);
    UPROPERTY(Transient) TObjectPtr<UOBPlayerOverlayWidget> UIPreview;
    UPROPERTY(Transient) TWeakObjectPtr<UOBPlayerOverlayWidget> HiddenOverlay;
    UPROPERTY(Transient) TArray<TWeakObjectPtr<UAudioComponent>> PausedAudio;
    bool bPreviewOwnsPause=false;
    uint8 PreviousOverlayVisibility=0;
};

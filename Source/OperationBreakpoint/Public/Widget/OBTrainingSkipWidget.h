#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "OBTrainingSkipWidget.generated.h"
class UImage;
class UTextBlock;
class UProgressBar;
class UMediaSource;
class UOBUIVideo;
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOBTrainingSkipRequested);

UCLASS(Blueprintable)
class OPERATIONBREAKPOINT_API UOBTrainingSkipWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void SetAvailable(bool bEnabled,bool bAlreadyHeld=false);
    bool HandleTrigger(bool bPressed);
    void AdvanceHold(float DeltaSeconds);
    bool IsAvailable() const { return bAvailable; }
    float GetHoldProgress() const { return FMath::Clamp(HeldSeconds/FMath::Max(.1f,HoldSeconds),0.f,1.f); }
    static constexpr float HoldSeconds=2.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="OB|UI|Skip") TSoftObjectPtr<UMediaSource> TriggerSource=TSoftObjectPtr<UMediaSource>(FSoftObjectPath(TEXT("/Game/Assets/UI/TriggerPress.TriggerPress")));
    UPROPERTY(BlueprintAssignable) FOBTrainingSkipRequested OnSkipRequested;
protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> TitleText;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional)) TObjectPtr<UImage> TriggerLoopVideo;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional)) TObjectPtr<UProgressBar> ProgressBar;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> BodyText;
private:
    friend class FOBTitleSkipTest;
    void ResetHold();
    UPROPERTY(Transient) TObjectPtr<UOBUIVideo> Video;
    float HeldSeconds=0;
    bool bAvailable=false,bHolding=false,bWaitingForRelease=false,bTriggered=false;
    bool bVideoBound=false;
};

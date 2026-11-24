#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Training/OBTrainingTypes.h"
#include "OBTrainingResultWidget.generated.h"
class UTextBlock;
class UImage;
class UTexture2D;
class UWorld;
class UBorder;
class UDataTable;
class AOBTrainingDirector;
class UProgressBar;
class UCanvasPanel;
class UScaleBox;

UENUM(BlueprintType)
enum class EOBTrainingResultAction : uint8 { None, NextStage, Retry };
UENUM(BlueprintType)
enum class EOBTrainingWarningAction : uint8 { None, Continue, ExitToMenu };

// A held input present when the result opens must be released before a new selection.
struct OPERATIONBREAKPOINT_API FOBTrainingResultTrigger
{
    void Reset(bool bAlreadyHeld);
    EOBTrainingResultAction Change(bool bPressed, float LongPressSeconds);
    EOBTrainingResultAction Advance(float DeltaSeconds, float LongPressSeconds);
    void Cancel();
    float HeldSeconds = 0;
    bool bWaitingForRelease = false, bHolding = false, bConsumed = false;
};
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOBTrainingResultSelected, EOBTrainingResultAction, Action);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOBTrainingWarningSelected, EOBTrainingWarningAction, Action);

UCLASS(Blueprintable)
class OPERATIONBREAKPOINT_API UOBTrainingResultWidget : public UUserWidget
{
    GENERATED_BODY()
public:
#if WITH_EDITOR
    virtual void OnSelectedByDesigner() override;
    virtual void OnDeselectedByDesigner() override;
#endif
    virtual void SynchronizeProperties() override;
    void BuildDefaultLayout();
    void SetResult(const FOBTrainingResultData& Data);
    void SetStageCompletion(bool bPassed, bool bReportSaved=true);
    void SetWarning(const FOBTrainingResultData& Data);
    void PrepareWarningInteraction(bool bAlreadyHeld);
    void SetTrainingDirector(AOBTrainingDirector* InDirector);
    void AdvanceInteraction(float DeltaSeconds);
    bool IsWarningInteraction() const { return bWarningInteraction; }
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Training|UI") int32 CurrentUIId=0;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Training|UI")
    TSoftObjectPtr<UDataTable> UIDataTable=TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Blueprints/Training/Data/DT_TrainingUI.DT_TrainingUI")));
    UFUNCTION(BlueprintCallable, Category="Training|Result") void HandleTrigger(bool bPressed);
    UFUNCTION(BlueprintCallable, Category="Training|Result") void CancelTrigger();
    UFUNCTION(BlueprintPure, Category="Training|Result") bool IsScenarioPassed() const;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Training|Interaction", meta=(ClampMin="0.25")) float LongPressSeconds = 2;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Training|Interaction") TSoftObjectPtr<UWorld> NextStage = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Maps/Stage_01.Stage_01")));
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Training|Result") TSoftObjectPtr<UTexture2D> PassStamp = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/Blueprints/Widget/PlayerOverlay/Art/T_TrainingPassStamp.T_TrainingPassStamp")));
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Training|Result") TSoftObjectPtr<UTexture2D> FailStamp = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/Blueprints/Widget/PlayerOverlay/Art/T_TrainingFailStamp.T_TrainingFailStamp")));
    UPROPERTY(BlueprintAssignable, Category="Training|Interaction") FOBTrainingResultSelected OnResultActionSelected;
    UPROPERTY(BlueprintAssignable, Category="Training|Interaction") FOBTrainingWarningSelected OnWarningActionSelected;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Training|Interaction") EOBTrainingWarningAction LastWarningAction=EOBTrainingWarningAction::None;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Training|Interaction") TSoftObjectPtr<UWorld> MainMenu=TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Maps/TitleMap.TitleMap")));
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Training|Interaction") EOBTrainingResultAction LastAction = EOBTrainingResultAction::None;
    // Allows projects to replace map navigation with the event above.
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Training|Interaction") bool bAutomaticNavigation = true;
protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;
private:
    void Refresh();
    void Select(EOBTrainingResultAction Action);
    UPROPERTY(Transient) FOBTrainingResultData Result;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> TitleText;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> BodyText;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UBorder> ResultBorder;
    UPROPERTY(Transient) TObjectPtr<UDataTable> LoadedUIData;
    UPROPERTY(Transient) TWeakObjectPtr<AOBTrainingDirector> Director;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> InteractionText;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> NextActionText;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> RetryActionText;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> NextActionHint;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> RetryActionHint;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UImage> StampImage;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UCanvasPanel> ResultActions;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UScaleBox> StageResultPanel;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UProgressBar> RetryHoldProgress;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UProgressBar> StageMenuHoldProgress;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> StageAccuracyText;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> StageScoreText;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> StageRankText;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> StageSubtitleText;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> StageTimeText;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> StageMenuHint;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> StageReportText;
    FOBTrainingResultTrigger Trigger;
    bool bWarningInteraction=false;
    bool bStageCompletion=false;
    bool bStagePassed=false;
    bool bStageReportSaved=false;
};

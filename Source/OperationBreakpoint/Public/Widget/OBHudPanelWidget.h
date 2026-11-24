#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Training/OBTrainingResultWidget.h"
#include "OBHudPanelWidget.generated.h"
class AOBTrainingDirector;
class UTextBlock;
class UImage;
class UProgressBar;
class UDataTable;
class UTexture2D;
class UMediaSource;
class UOBUIVideo;
class UWidgetAnimation;
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOBGuideHidden);

UENUM(BlueprintType)
enum class EOBHudPanelRole : uint8 { Guide, Pause, Subtitle, Mission, Timer, Life };

USTRUCT(BlueprintType)
struct OPERATIONBREAKPOINT_API FOBMissionObjective
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mission") FText Text;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mission") bool bCompleted=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mission") EOBTrainingOutcome Outcome=EOBTrainingOutcome::NotRun;
};

UENUM(BlueprintType)
enum class EOBTrainingPauseAction : uint8 { None, Continue, ExitToMenu };
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOBTrainingPauseSelected, EOBTrainingPauseAction, Action);

/** Each child owns its display data; PlayerOverlay owns placement and visibility. */
UCLASS(Blueprintable)
class OPERATIONBREAKPOINT_API UOBHudPanelWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category="HUD") EOBHudPanelRole Role = EOBHudPanelRole::Subtitle;
    virtual EOBHudPanelRole GetPanelRole() const { return Role; }
    void BuildDefaultLayout(EOBHudPanelRole InRole);
    void SetTrainingDirector(AOBTrainingDirector* InDirector);
    UFUNCTION(BlueprintCallable, Category="HUD") void SetMessage(const FText& Message);
    UFUNCTION(BlueprintCallable, Category="HUD") void SetDialogue(const FText& Speaker, const FText& Dialogue);
    UFUNCTION(BlueprintCallable, Category="HUD") void SetRemainingSeconds(float Seconds);
    UFUNCTION(BlueprintCallable, Category="HUD") void StartCountdown(float Seconds);
    UFUNCTION(BlueprintCallable, Category="HUD") void StopCountdown();
    UFUNCTION(BlueprintCallable, Category="HUD") void SetZoneProgress(int32 Current, int32 Total);
    UFUNCTION(BlueprintCallable, Category="HUD") void SetHealth(float Current, float Maximum);
protected:
    AOBTrainingDirector* GetTrainingDirector() const;
    void SetInteractionPrompt(const FText& Text);
    void SetPanelTitle(const FText& Text);
    virtual void NativeOnInitialized() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;
private:
    void UpdateTrainingData();
    UPROPERTY(Transient) TWeakObjectPtr<AOBTrainingDirector> Director;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> TitleText;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> BodyText;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> PromptText;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> ValueText;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> Name;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> Script;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UProgressBar> HealthProgress;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UImage> LIFE_01;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UImage> LIFE_02;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UImage> LIFE_03;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UImage> LIFE_04;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UImage> LIFE_05;
    float RemainingSeconds = 0;
    bool bCountdownRunning = false;
};

UCLASS(Blueprintable)
class OPERATIONBREAKPOINT_API UOBGuideWidget : public UOBHudPanelWidget
{
    GENERATED_BODY()
public:
    UOBGuideWidget() { Role=EOBHudPanelRole::Guide; }
    virtual EOBHudPanelRole GetPanelRole() const override { return EOBHudPanelRole::Guide; }
    UFUNCTION(BlueprintCallable, Category="Training|Guide") void SetGuidePage(int32 UIId, bool bCanAdvance=false, bool bAutoShow=true);
    void PlayGuideFadeIn();
    void DismissGuide();
    void CancelGuideTransitions();
    UPROPERTY(BlueprintAssignable,Category="Training|Guide") FOBGuideHidden OnGuideHidden;
    UPROPERTY(Transient,meta=(BindWidgetAnimOptional)) TObjectPtr<UWidgetAnimation> GuideFadeIn;
    UPROPERTY(Transient,meta=(BindWidgetAnimOptional)) TObjectPtr<UWidgetAnimation> GuideFadeOut;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Training|Guide")
    TSoftObjectPtr<UDataTable> UIDataTable=TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Blueprints/Training/Data/DT_TrainingUI.DT_TrainingUI")));
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Training|Guide")
    TSoftObjectPtr<UMediaSource> TriggerSource=TSoftObjectPtr<UMediaSource>(FSoftObjectPath(TEXT("/Game/Assets/UI/TriggerPress.TriggerPress")));
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Training|Guide") int32 CurrentUIId=0;
protected:
    virtual void NativeOnInitialized() override;
    virtual void NativePreConstruct() override;
    virtual void NativeDestruct() override;
private:
    friend class FOBTrainingUIPagesTest;
    friend class FOBTitleGuideMediaSmoke;
    friend class FOBTrainingDialogueAnimationTest;
    UFUNCTION() void HandleGuideFadeOutFinished();
    void SetTriggerReady(bool bReady);
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UImage> BodyImage;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UImage> TriggerImage;
    UPROPERTY(Transient) TObjectPtr<UDataTable> LoadedUIData;
    UPROPERTY(Transient) TObjectPtr<UOBUIVideo> TriggerVideo;
    bool bTriggerReady=false;
    bool bGuideShown=false, bGuideHiding=false;
    int32 GuideAnimationRequest=0;
    FName GuideAnimationTag;
};
UCLASS(Blueprintable)
class OPERATIONBREAKPOINT_API UOBPauseWidget : public UOBHudPanelWidget
{
    GENERATED_BODY()
public:
    UOBPauseWidget() { Role=EOBHudPanelRole::Pause; }
    virtual EOBHudPanelRole GetPanelRole() const override { return EOBHudPanelRole::Pause; }
    void PrepareInteraction(bool bAlreadyHeld);
    UFUNCTION(BlueprintCallable, Category="Training|Pause") void HandleTrigger(bool bPressed);
    void AdvanceInteraction(float DeltaSeconds);
    void CancelInteraction();
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Training|Pause", meta=(ClampMin="0.25")) float LongPressSeconds=2;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Training|Pause") TSoftObjectPtr<UWorld> MainMenu=TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Maps/TitleMap.TitleMap")));
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Training|Pause") bool bAutomaticActions=true;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Training|Pause") EOBTrainingPauseAction LastAction=EOBTrainingPauseAction::None;
    UPROPERTY(BlueprintAssignable, Category="Training|Pause") FOBTrainingPauseSelected OnActionSelected;
protected:
    virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;
    virtual void NativePreConstruct() override;
private:
    void Select(EOBTrainingResultAction Gesture);
    void RefreshPrompt();
    // Share the tested press/release and hold evaluator; map short/long to continue/exit.
    FOBTrainingResultTrigger Trigger;
};
UCLASS(Blueprintable)
class OPERATIONBREAKPOINT_API UOBSubtitleWidget : public UOBHudPanelWidget
{
    GENERATED_BODY()
public:
    UOBSubtitleWidget() { Role=EOBHudPanelRole::Subtitle; }
    virtual EOBHudPanelRole GetPanelRole() const override { return EOBHudPanelRole::Subtitle; }
};
UCLASS(Blueprintable)
class OPERATIONBREAKPOINT_API UOBMissionWidget : public UOBHudPanelWidget
{
    GENERATED_BODY()
public:
    UOBMissionWidget();
    virtual EOBHudPanelRole GetPanelRole() const override { return EOBHudPanelRole::Mission; }
    /** Replaces all five rows, clearing strikes and collapsing unused rows. */
    UFUNCTION(BlueprintCallable, Category="HUD|Mission") void SetMissionObjectives(const FText& Heading, const TArray<FOBMissionObjective>& Objectives);
    void UpdateTrainingObjectives(const FOBTrainingResultData& Result, bool bBasic);
protected:
    virtual void NativeOnInitialized() override;
private:
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> MissionTitleText;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> BodyText_1;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> BodyText_2;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> BodyText_3;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> BodyText_4;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> BodyText_5;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UImage> Strike_1;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UImage> Strike_2;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UImage> Strike_3;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UImage> Strike_4;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UImage> Strike_5;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> OutcomeText_1;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> OutcomeText_2;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> OutcomeText_3;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> OutcomeText_4;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> OutcomeText_5;
    UPROPERTY(Transient) TArray<FSlateColor> PendingTextColors;
};
UCLASS(Blueprintable)
class OPERATIONBREAKPOINT_API UOBTimerWidget : public UOBHudPanelWidget
{
    GENERATED_BODY()
public:
    UOBTimerWidget() { Role=EOBHudPanelRole::Timer; }
    virtual EOBHudPanelRole GetPanelRole() const override { return EOBHudPanelRole::Timer; }
};
UCLASS(Blueprintable)
class OPERATIONBREAKPOINT_API UOBLifeWidget : public UOBHudPanelWidget
{
    GENERATED_BODY()
public:
    UOBLifeWidget() { Role=EOBHudPanelRole::Life; }
    virtual EOBHudPanelRole GetPanelRole() const override { return EOBHudPanelRole::Life; }
};

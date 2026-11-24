#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Training/OBTrainingTypes.h"
#include "OBPlayerOverlayWidget.generated.h"

class UOBSystemMessageWidget;
class UWidgetAnimation;
class AOBTrainingDirector;
class UOBHudPanelWidget;
class UOBTrainingStatusWidget;
class UOBTrainingResultWidget;
class UOBTrainingSkipWidget;
class UOBGuideWidget;
class UBackgroundBlur;

UENUM(BlueprintType, meta=(Bitflags, UseEnumValuesAsMaskValuesInEditor="true"))
enum class EOBOverlayElement : uint8
{
    None=0 UMETA(Hidden),
    Guide=1, Pause=2 UMETA(Hidden), Subtitle=4, Mission=8, Timer=16, Life=32, Markers=64, Result=128
};

UENUM(BlueprintType)
enum class EOBOverlayPreviewMode : uint8
{
    Layout,
    Stage,
    TrainingGuide,
    TrainingPractice,
    Pause,
    ResultPass,
    ResultFail,
    MarkersAim,
    MarkersBreach,
    SystemMessage
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOBSystemMessageFinished);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOBSubtitleFinished);

/** Display data is pushed by gameplay; the widget does not poll actors every frame. */
UCLASS(Abstract, Blueprintable)
class OPERATIONBREAKPOINT_API UOBPlayerOverlayWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|UI|Designer", meta=(ToolTip="Designer-only preview. Gameplay selects panels from the current training or stage state."))
    EOBOverlayPreviewMode DesignerPreview = EOBOverlayPreviewMode::Layout;
    virtual void SynchronizeProperties() override;
    void SetTrainingDirector(AOBTrainingDirector* Director);
    void ShowTrainingResult(const FOBTrainingResultData& Result);
    void RefreshTrainingGuide();
    UOBGuideWidget* GetTrainingGuideWidget() const;
    void RefreshInitialTrainingSkip();
    bool HandleInitialSkipTrigger(bool bPressed);
    bool IsInitialTrainingSkipVisible() const;
    bool SetQAUIPreview(int32 UIId);
    bool IsTrainingResultVisible() const;
    bool IsTrainingPauseVisible() const;
    void HandleTrainingTrigger(bool bPressed);
    void HandleResultTrigger(bool bPressed);
    void CancelResultTrigger();
#if WITH_EDITOR
    void PreviewSelectedPanel(EOBOverlayElement Panel, bool bSelected);
#endif
    UOBTrainingStatusWidget* GetTrainingMarkers() const { return TrainingMarkers; }
    UOBTrainingResultWidget* GetTrainingResultWidget() const { return TrainingResult; }
    UFUNCTION(BlueprintCallable, Category="OB|UI|Overlay") void SetStageMode();
    UFUNCTION(BlueprintCallable, Category="OB|UI|Overlay") void SetPanelEnabled(EOBOverlayElement Panel, bool bEnabled);
    UFUNCTION(BlueprintCallable, Category="OB|UI|Overlay") void SetSystemMessageEnabled(bool bEnabled);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|UI|Visibility") bool bSystemMessageEnabled=true;
    UFUNCTION(BlueprintCallable, Category="OB|UI|Overlay") void SetStageTime(float Seconds, bool bStartCountdown=false);
    UFUNCTION(BlueprintCallable, Category="OB|UI|Overlay") void SetZoneProgress(int32 Current, int32 Total);
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="OB|UI|Visibility", meta=(Bitmask,BitmaskEnum="/Script/OperationBreakpoint.EOBOverlayElement")) int32 StageElements = 16 | 32;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="OB|UI|Visibility", meta=(Bitmask,BitmaskEnum="/Script/OperationBreakpoint.EOBOverlayElement")) int32 TrainingElements = 1 | 4 | 8 | 16 | 64;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="OB|UI|Visibility") int32 VisibleElements = 0;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="OB|UI|Background",meta=(ClampMin="0",ClampMax="100")) float TrainingBlurStrength=3.f;
    /** Legacy message/fade API; uses the SystemMessage instance placed in the overlay's Designer. */
    UFUNCTION(BlueprintCallable, Category = "OB|UI|SystemMessage")
    void SetSystemMessage(UOBSystemMessageWidget* InSystemMessageWidget);

    /** Fade in, hold after fade-in finishes, then fade out. Replaces any current message. */
    UFUNCTION(BlueprintCallable, Category = "OB|UI|SystemMessage")
    void ShowSystemMessage(const FText& Message, float HoldDuration = 3.0f);

    UFUNCTION(BlueprintCallable, Category = "OB|UI|SystemMessage")
    void ShowPersistentSystemMessage(const FText& Message);

    UFUNCTION(BlueprintCallable, Category = "OB|UI|SystemMessage")
    void DismissSystemMessage();

    UFUNCTION(BlueprintPure, Category = "OB|UI|SystemMessage")
    bool IsSystemMessageReady() const;

    UPROPERTY(BlueprintAssignable, Category = "OB|UI|SystemMessage")
    FOBSystemMessageFinished OnSystemMessageShown;

    /** Broadcast after fade-out finishes. Replacement/destruction does not broadcast. */
    UPROPERTY(BlueprintAssignable, Category = "OB|UI|SystemMessage")
    FOBSystemMessageFinished OnSystemMessageFinished;

    // Shown/Finished are emitted only after the respective authored animation completes.
    UFUNCTION(BlueprintCallable, Category="OB|UI|Subtitle") void SetSubtitle(UWidget* InSubtitleWidget);
    UFUNCTION(BlueprintCallable, Category="OB|UI|Subtitle") void ShowSubtitle(const FText& Speaker, const FText& Script, float HoldDuration=3.f);
    UFUNCTION(BlueprintCallable, Category="OB|UI|Subtitle") void ShowPersistentSubtitle(const FText& Speaker, const FText& Script);
    UFUNCTION(BlueprintCallable, Category="OB|UI|Subtitle") void DismissSubtitle();
    UFUNCTION(BlueprintPure, Category="OB|UI|Subtitle") bool IsSubtitleReady() const;
    void SetCinematicSubtitles(bool bEnabled);
    bool IsCinematicSubtitleMode() const { return bCinematicSubtitleMode; }
    bool IsSubtitleActive() const { return bSubtitleMessageActive; }
    UWidget* GetSubtitleWidget() const { return SubtitleWidget; }
    FText GetSubtitleText() const;
    UPROPERTY(BlueprintAssignable, Category="OB|UI|Subtitle") FOBSubtitleFinished OnSubtitleShown;
    UPROPERTY(BlueprintAssignable, Category="OB|UI|Subtitle") FOBSubtitleFinished OnSubtitleFinished;
    void SetTrainingDialogueAnimationControl(bool bEnabled);
    void CancelTrainingDialogue();

    UFUNCTION(BlueprintCallable, Category = "OB|UI|Overlay")
    void SetHealth(float InCurrentHealth, float InMaxHealth);

    UFUNCTION(BlueprintCallable, Category = "OB|UI|Overlay")
    void SetAmmo(int32 InCurrentAmmo, int32 InMaxAmmo);

    UFUNCTION(BlueprintCallable, Category = "OB|UI|Overlay")
    void SetScore(int32 InScore);

    UPROPERTY(BlueprintReadOnly, Category = "OB|UI|Overlay")
    float CurrentHealth = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "OB|UI|Overlay")
    float MaxHealth = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "OB|UI|Overlay")
    int32 CurrentAmmo = 0;

    UPROPERTY(BlueprintReadOnly, Category = "OB|UI|Overlay")
    int32 MaxAmmo = 0;

    UPROPERTY(BlueprintReadOnly, Category = "OB|UI|Overlay")
    int32 Score = 0;

protected:
    virtual void NativePreConstruct() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

    UPROPERTY(Transient, BlueprintReadOnly, Category = "OB|UI|SystemMessage")
    TObjectPtr<UOBSystemMessageWidget> SystemMessageWidget;
    UPROPERTY(Transient, BlueprintReadOnly, Category="OB|UI|Subtitle") TObjectPtr<UWidget> SubtitleWidget;

    // These names match the animations authored in WBP_PlayerOverlay.
    // An omitted animation skips that fade while preserving the hold/completion flow.
    UPROPERTY(Transient, meta = (BindWidgetAnimOptional))
    TObjectPtr<UWidgetAnimation> SystemMessageFadeIn;

    UPROPERTY(Transient, meta = (BindWidgetAnimOptional))
    TObjectPtr<UWidgetAnimation> SystemMessageFadeOut;
    UPROPERTY(Transient, meta=(BindWidgetAnimOptional)) TObjectPtr<UWidgetAnimation> SubtitleMessageFadeIn;
    UPROPERTY(Transient, meta=(BindWidgetAnimOptional)) TObjectPtr<UWidgetAnimation> SubtitleMessageFadeOut;

    /** Read the cached properties here to update the Blueprint's visual controls. */
    UFUNCTION(BlueprintImplementableEvent, Category = "OB|UI|Overlay")
    void OnOverlayUpdated();
    UPROPERTY(Transient, BlueprintReadOnly, meta=(BindWidgetOptional), Category="OB|UI|Panels") TObjectPtr<UOBHudPanelWidget> Guide;
    UPROPERTY(Transient, BlueprintReadOnly, meta=(BindWidgetOptional), Category="OB|UI|Panels") TObjectPtr<UWidget> Subtitle;
    UPROPERTY(Transient, BlueprintReadOnly, meta=(BindWidgetOptional), Category="OB|UI|Panels") TObjectPtr<UOBSystemMessageWidget> SystemMessage;
    UPROPERTY(Transient, BlueprintReadOnly, meta=(BindWidgetOptional), Category="OB|UI|Panels") TObjectPtr<UOBHudPanelWidget> Mission;
    UPROPERTY(Transient, BlueprintReadOnly, meta=(BindWidgetOptional), Category="OB|UI|Panels") TObjectPtr<UOBHudPanelWidget> Timer;
    UPROPERTY(Transient, BlueprintReadOnly, meta=(BindWidgetOptional), Category="OB|UI|Panels") TObjectPtr<UOBHudPanelWidget> Life;
    UPROPERTY(Transient, BlueprintReadOnly, meta=(BindWidgetOptional), Category="OB|UI|Panels") TObjectPtr<UOBTrainingStatusWidget> TrainingMarkers;
    UPROPERTY(Transient, BlueprintReadOnly, meta=(BindWidgetOptional), Category="OB|UI|Panels") TObjectPtr<UOBTrainingResultWidget> TrainingResult;
    UPROPERTY(Transient, BlueprintReadOnly, meta=(BindWidgetOptional), Category="OB|UI|Panels") TObjectPtr<UOBTrainingSkipWidget> TrainingSkip;

private:
    // Compatibility binding for loading pre-migration assets. The migrated tree
    // contains no Pause widget and runtime input is owned entirely by Result.
    UPROPERTY(Transient,meta=(BindWidgetOptional)) TObjectPtr<UOBHudPanelWidget> Pause;
    UPROPERTY(Transient,meta=(BindWidgetOptional)) TObjectPtr<UBackgroundBlur> TrainingBackgroundBlur;
    UFUNCTION() void HandleInitialTrainingSkip();
    int32 QAUIPreviewId=0;
    void RefreshPanelVisibility();
    void ApplyPanelVisibility(int32 Mask);
    void ApplyDesignerPreview();
    void RefreshSystemMessage();
    UPROPERTY(Transient) TWeakObjectPtr<AOBTrainingDirector> TrainingDirector;
    bool bTrainingResultShown = false;
    bool bWasTrainingPaused = false;
    bool bPauseInteractionActive = false;
    bool bSystemMessageManaged = false;
    bool bTrainingDialogueAnimationControl = false;
#if WITH_EDITOR
    EOBOverlayElement SelectedDesignerPanel = EOBOverlayElement::None;
#endif
    friend class FOBOpenFireInputModesTest;
    friend class FOBSystemMessageTest;
    friend class FOBTrainingFlowTest;
    friend class FOBTrainingSystemMessageTest;
    friend class FOBSubtitleMessageTest;
    friend class FOBStageZone6MapSmoke;
    friend class FOBTrainingDialogueAnimationTest;
    friend class FOBTrainingDialogueMapSmoke;

    enum class ESystemMessagePhase : uint8 { Idle, FadingIn, Holding, FadingOut };

    void ResetSystemMessage();
    void BeginSystemMessageFadeOut();

    UFUNCTION()
    void HandleSystemMessageFadeInFinished();

    UFUNCTION()
    void HandleSystemMessageFadeOutFinished();

    FTimerHandle SystemMessageHoldTimer;
    bool bPersistentSystemMessage = false;
    bool bDismissSystemMessageRequested = false;
    ESystemMessagePhase SystemMessagePhase = ESystemMessagePhase::Idle;
    float SystemMessageHoldDuration = 0.0f;
    int32 SystemMessageRequestNumber = 0;
    FName SystemMessageAnimationTag;
    void ResetSubtitle();
    void SetSubtitleDialogue(const FText& Speaker,const FText& Script);
    void BeginSubtitleFadeOut();
    UFUNCTION() void HandleSubtitleFadeInFinished();
    UFUNCTION() void HandleSubtitleFadeOutFinished();
    ESystemMessagePhase SubtitlePhase=ESystemMessagePhase::Idle;
    float SubtitleHoldDuration=0;
    bool bPersistentSubtitle=false, bDismissSubtitleRequested=false;
    FName SubtitleAnimationTag;
    FTimerHandle SubtitleHoldTimer;
    bool bSubtitleMessageActive=false;
    bool bSubtitleVisibilityOwned=false;
    bool bCinematicSubtitleMode=false;
    TMap<TWeakObjectPtr<UWidget>,ESlateVisibility> CinematicHiddenWidgets;
    int32 SubtitleRequestNumber=0;
};

#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Training/OBTrainingTypes.h"
#include "OBTrainingDirector.generated.h"
class AOBPlayerController;
class UOBActionComponent;
class UOBPlayerSplineComponent;
class UOBPlayerOverlayWidget;
class UOBMediaPipeOSCSubsystem;
class UOBMediaPipeInputComponent;
class UOBTrainingStatusWidget;
class AOBTrainingTarget;
class AOBTrainingDoor;
class UAudioComponent;
class USoundBase;
class USoundCue;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOBTrainingStepChanged, EOBTrainingStep, Step);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOBTrainingProgress, int32, Count, int32, Required);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOBTrainingRetry, FText, Reason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOBTrainingCompleted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOBTrainingFinished, FOBTrainingResultData, Result);
/** Owns ordered zone exercises, not the shared player or hardware implementation. */
UCLASS()
class OPERATIONBREAKPOINT_API AOBTrainingDirector : public AActor
{
    GENERATED_BODY()
  public:
    AOBTrainingDirector();
    bool EnterQACheckpoint(FName Checkpoint, FString& Error);
    TArray<FName> GetQACheckpoints() const;
    FName GetQACheckpoint() const;
    FString GetQAStatus() const;
    bool IsReadyForQA() const { return bInitialized; }
    virtual void Tick(float DeltaSeconds) override;
    UFUNCTION(BlueprintCallable, Category = "OB|Training") void StartTraining();
    UFUNCTION(BlueprintCallable, Category = "OB|Training") void AbortTraining(const FString &Reason);
    UFUNCTION(BlueprintCallable, Category = "OB|Training") void NotifyBreachState(bool bRecognized);
    UFUNCTION(BlueprintPure, Category = "OB|Training") float GetRemainingSeconds() const;
    UFUNCTION(BlueprintPure, Category = "OB|Training") FOBTrainingResultData GetTrainingResult() const
    {
        return Result;
    }
    UFUNCTION(BlueprintPure, Category = "OB|Training") TArray<FOBTrainingZoneConfig> GetZoneConfiguration() const
    {
        return Zones;
    }
    UFUNCTION(BlueprintPure, Category = "OB|Training") FVector2D GetAimTargetPosition() const;
    UFUNCTION(BlueprintPure, Category = "OB|Training") float GetAimHoldProgress() const;
    UFUNCTION(BlueprintPure, Category = "OB|Training") FText GetStatusText() const;
    UFUNCTION(BlueprintPure, Category = "OB|Training") int32 GetCurrentZoneIndex() const { return ZoneIndex; }
    UFUNCTION(BlueprintPure, Category = "OB|Training") FText GetSubtitleText() const;
    UFUNCTION(BlueprintPure, Category = "OB|Training") FText GetSubtitleSpeaker() const;
    UFUNCTION(BlueprintPure, Category = "OB|Training") FText GetSystemMessageText() const;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OB|Training|Prototype") bool bUseSplinePrototype = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OB|Training|Prototype") TArray<int32> PrototypeRoutePoints = {0, 2, 4, 7, 12, 17};
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OB|Training|Prototype", meta = (ClampMin = "0.01")) float PrototypeNarrationSeconds = 2.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OB|Training|Dialogue") TSoftObjectPtr<UDataTable> DialogueTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Blueprints/Training/Data/DT_TrainingDialogue.DT_TrainingDialogue")));
    // Scene brightness: zero is black, one is fully visible.
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "OB|Training|Timing") float IntroBrightness = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OB|Training|Prototype", meta = (ClampMin = "1")) float PrototypeZoneSeconds = 120.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OB|Training|Prototype", meta = (ClampMin = "1")) float PrototypeReminderSeconds = 20.f;
    // TimedTargets[0..2] belong to point 7, [3..5] to point 12.
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "OB|Training|Prototype") int32 CurrentDialogueId = 0;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "OB|Training|Prototype") bool bGuideAdvanceReady = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Training|Guide", meta=(ClampMin="0.0", Units="s")) float GuideTriggerDelaySeconds=1.f;
    UFUNCTION(BlueprintPure, Category="OB|Training|Guide") int32 GetGuideUIId() const;
    UFUNCTION(BlueprintPure, Category="OB|Training|Skip") bool IsInitialSkipAvailable() const;
    UFUNCTION(BlueprintCallable, Category="OB|Training|Skip") bool RequestInitialSkip();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Training|Skip") TSoftObjectPtr<UWorld> SkipDestination=TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Maps/Stage_01.Stage_01")));
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "OB|Training|Prototype") bool bPrototypePaused = false;
    UFUNCTION(BlueprintPure, Category = "OB|Training|Prototype") bool IsPrototypeGuideVisible() const;
    /** Guide A (dialogues 3-7) and guide B (17-18), including the gaps between their pages. */
    UFUNCTION(BlueprintPure, Category="OB|Training|Guide") bool IsGuideSectionActive() const;
    UFUNCTION(BlueprintPure, Category = "OB|Training|Prototype") FText GetPrototypeDialogueText() const;
    UFUNCTION(BlueprintPure, Category = "OB|Training|Prototype") int32 GetPrototypeRouteIndex() const { return PrototypeRouteIndex; }
    UFUNCTION(BlueprintCallable, Category = "OB|Training|Prototype") void ResumePrototype();
    UFUNCTION(BlueprintCallable, Category = "OB|Training|Prototype") void FinishPrototypeNarration();
    bool HasAcceptedBreach() const { return bBreachAccepted; }
    bool NeedsBreachRelease() const { return bLastBreachState && !bBreachAccepted; }
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "OB|Training")
    EOBTrainingStep CurrentStep = EOBTrainingStep::Idle;
    UPROPERTY(BlueprintAssignable, Category = "OB|Training") FOBTrainingStepChanged OnStepChanged;
    UPROPERTY(BlueprintAssignable, Category = "OB|Training") FOBTrainingProgress OnProgressChanged;
    UPROPERTY(BlueprintAssignable, Category = "OB|Training") FOBTrainingRetry OnRetry;
    // Qualification only. Finished also fires for failed/aborted assessments.
    UPROPERTY(BlueprintAssignable, Category = "OB|Training") FOBTrainingCompleted OnTrainingCompleted;
    UPROPERTY(BlueprintAssignable, Category = "OB|Training") FOBTrainingFinished OnTrainingFinished;
    UPROPERTY(EditAnywhere, Category = "OB|Training|Setup") bool bAutoStart = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OB|Training|Setup") FString LearnerName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OB|Training|Setup") FString LearnerId;
    UPROPERTY(EditAnywhere, Category = "OB|Training|Setup") bool bAutoExportPdf = true;
    UPROPERTY(EditAnywhere, Category = "OB|Training|Setup") TArray<FOBTrainingZoneConfig> Zones;
    UPROPERTY(EditInstanceOnly, Category = "OB|Training|Setup") TObjectPtr<AOBTrainingTarget> AccuracyTarget;
    UPROPERTY(EditInstanceOnly, Category = "OB|Training|Setup") TArray<TObjectPtr<AOBTrainingTarget>> TimedTargets;
    UPROPERTY(EditInstanceOnly, Category = "OB|Training|Setup") TObjectPtr<AOBTrainingDoor> BreachDoor;
    // Legacy non-default values override default zone point indices for old assets.
    UPROPERTY(EditAnywhere, Category = "OB|Training|Legacy") int32 ZoneAPoint = 0;
    UPROPERTY(EditAnywhere, Category = "OB|Training|Legacy") int32 ZoneBPoint = 1;
    UPROPERTY(EditAnywhere, Category = "OB|Training|Legacy") int32 ZoneCPoint = 2;
    UPROPERTY(EditAnywhere, Category = "OB|Training|Timing", meta = (ClampMin = "0.01")) float IntroFadeSeconds = 2;
    UPROPERTY(EditAnywhere, Category = "OB|Training|Timing", meta = (ClampMin = "0.01")) float WelcomeSeconds = 3;
    UPROPERTY(EditAnywhere, Category = "OB|Training|Timing", meta = (ClampMin = "0.01")) float StepDelaySeconds = 1;
    UPROPERTY(EditAnywhere, Category = "OB|Training|Timing", meta = (ClampMin = "0.1")) float WeaponCheckSeconds = 5;
    UPROPERTY(EditAnywhere, Category = "OB|Training|Timing", meta = (ClampMin = "0.1")) float AimAttemptSeconds = 10;
    UPROPERTY(EditAnywhere, Category = "OB|Training|Timing", meta = (ClampMin = "0.1")) float ReloadSeconds = 15;
    UPROPERTY(EditAnywhere, Category = "OB|Training|Timing", meta = (ClampMin = "0.1")) float CoverSeconds = 10;
    UPROPERTY(EditAnywhere, Category = "OB|Training|Timing", meta = (ClampMin = "0.1")) float CountdownSeconds = 10;
    UPROPERTY(EditAnywhere, Category="OB|Training|Prototype", meta=(ClampMin="0.01")) float MockStartDelaySeconds=2.f;
    UPROPERTY(EditAnywhere, Category="OB|Training|Prototype") TSoftObjectPtr<USoundBase> MockStartSiren;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Training|Audio") TSoftObjectPtr<USoundBase> TrainingMusic;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Training|Audio") TSoftObjectPtr<USoundBase> WalkingSound;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Training|Audio") TSoftObjectPtr<USoundBase> MissionEndSiren;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Training|Audio", meta=(ClampMin="0", ClampMax="1")) float MusicVolume=.16f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Training|Audio", meta=(ClampMin="0", ClampMax="1")) float MusicVoiceVolume=.05f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Training|Audio", meta=(ClampMin="0", ClampMax="1")) float FootstepVolume=.65f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="OB|Training|Audio", meta=(ClampMin="0.1", Units="s")) float FootstepIntervalSeconds=.55f;
    UPROPERTY(EditAnywhere, Category="OB|Training|Camera") bool bFocusWeaponCheckCamera = true;
    UPROPERTY(EditAnywhere, Category="OB|Training|Camera", meta=(ClampMin="-80", ClampMax="0", Units="deg")) float WeaponCheckCameraPitch = -17.f;
    UPROPERTY(EditAnywhere, Category="OB|Training|Camera", meta=(ClampMin="-80", ClampMax="80", Units="deg")) float WeaponCheckCameraYaw = -5.5f;
    // 2*atan(tan(50deg/2)/3): approximately three times the previous inspection magnification.
    UPROPERTY(EditAnywhere, Category="OB|Training|Camera", meta=(ClampMin="5", ClampMax="170", Units="deg")) float WeaponCheckCameraFOV = 17.7f;
    UPROPERTY(EditAnywhere, Category="OB|Training|Camera", meta=(ClampMin="0.01")) float WeaponCheckCameraBlendSeconds = .65f;
    UPROPERTY(EditAnywhere, Category = "OB|Training|Timing", meta = (ClampMin = "0.1")) float TimedTargetSeconds = 30;
    UPROPERTY(EditAnywhere, Category = "OB|Training|Timing", meta = (ClampMin = "0.1")) float BreachSeconds = 10;
    UPROPERTY(EditAnywhere, Category = "OB|Training|Rules", meta = (ClampMin = "1")) int32 MaxPrerequisiteAttempts = 3;
    UPROPERTY(EditAnywhere, Category = "OB|Training|Rules", meta = (ClampMin = "1")) int32 MaxAccuracyAttempts = 2;
    UPROPERTY(EditAnywhere, Category = "OB|Training|Rules", meta = (ClampMin = "0.1")) float AimHoldSeconds = 2;
    // Pixels; physical 1.5 cm requires display calibration, not guessed DPI.
    UPROPERTY(EditAnywhere, Category = "OB|Training|Rules", meta = (ClampMin = "1")) float AimRadiusPixels = 72;
    UPROPERTY(EditAnywhere, Category = "OB|Training|Rules") TArray<FVector2D> AimPositions;
    UPROPERTY(EditAnywhere, Category = "OB|Training|Rules", meta = (ClampMin = "0.1")) float GroupRadiusCm = 20;
    UPROPERTY(EditAnywhere, Category = "OB|Training|Rules", meta = (ClampMin = "1")) int32 RequiredTargetCount = 10;
    UPROPERTY(EditAnywhere, Category = "OB|Training|Rules", meta = (ClampMin = "1")) int32 PassingTargetCount = 6;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OB|Training|Pop Up") bool bUsePopUpTargets = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OB|Training|Pop Up", meta = (ClampMin = "0.1")) float TargetExposureSeconds = 2.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OB|Training|Pop Up", meta = (ClampMin = "0")) float TargetGapSeconds = 0.35f;
    UFUNCTION(BlueprintPure, Category = "OB|Training") AOBTrainingTarget* GetActiveTimedTarget() const;

    UPROPERTY(EditAnywhere, Category = "OB|Training|Messages") TMap<EOBTrainingStep, FText> Instructions;
    // Debug inputs invalidate qualification: Space=trigger, R=reload, C=calibration, V=cover toggle, B=breach.
    UPROPERTY(EditAnywhere, Category = "OB|Training|Debug") bool bEnableKeyboardTestInputs = false;

  protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

  private:
    friend class FOBTrainingAmmoTest;
    void ApplyLearnerProfile();
    friend class FOBUICaptureSmoke;
    friend class FOBTrainingSoundTest;
    void StartTrainingAudio();
    void UpdateTrainingAudio(float DeltaSeconds);
    void StopTrainingAudio(bool bFadeMusic);
    void StopTrainingFootsteps();
    void PlayTrainingSignal(const TSoftObjectPtr<USoundBase>& Sound, const TCHAR* Event);
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> TrainingMusicAudio;
    UPROPERTY(Transient) TObjectPtr<USoundCue> TrainingMusicLoop;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> FootstepAudio;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> TrainingSignalAudio;
    UPROPERTY(Transient) TObjectPtr<USoundBase> LoadedWalkingSound;
    float FootstepRemaining=0.f;
    float CurrentMusicVolume=0.f;
    friend class FOBPackagePerformance;
    bool bInitialSkipOpen=false;
    friend class FOBTitleSkipTest;
    friend class FOBTitleSkipMapSmoke;
    friend class FOBFullFlowMapSmoke;
    friend class FOBMarkerWarningSmoke;
    friend class FOBTrainingFlowTest;
    friend class FOBTrainingUIPagesTest;
    friend class FOBQATrainingCheckpointTest;
    friend class FOBTrainingPopUpTest;
    friend class FOBTrainingVisualSmoke;
    friend class FOBTrainingKeyboardSmoke;
    friend class FOBTrainingSplinePrototypeTest;
    friend class FOBTrainingReminderVoiceTest;
    friend class FOBTrainingSystemMessageTest;
    friend class FOBTrainingAccuracyToleranceTest;
    friend class FOBTrainingZeroingMapSmoke;
    friend class FOBTrainingWeaponViewSmoke;
    bool ValidatePrototype(FString& Error) const;
    FText PrototypeLineText(int32 Id) const;
    int32 GetPrototypeDisplayDialogueId() const;
    void BeginPrototype();
    void FinishPrototypeIntro();
    void StartPrototypeDialogueIfReady();
    const FOBTrainingDialogueRow* FindPrototypeDialogue(int32 Id) const;
    void StopPrototypeNarration();
    void PlayPrototypeHint(int32 Id);
    void StopPrototypeHint();
    void PlayPrototypeLine(int32 Id);
    void BeginPrototypeNarration(int32 Id);
    float GetPrototypeIntroDuration() const { return FMath::Max(0.01f, IntroFadeSeconds); }
    void PresentPrototypeDialogue(int32 Id, bool bHint=false, bool bTerminal=false);
    void DismissPrototypeDialogue();
    UFUNCTION() void HandlePrototypeDialogueShown();
    UFUNCTION() void HandlePrototypeDialogueHidden();
    UFUNCTION() void HandlePrototypeGuideHidden();
    void TryFinishPrototypeDialogueFade();
    bool bPrototypeDialogueLayerHidden=false, bPrototypeGuideHidden=true;
    UFUNCTION() void FinishPrototypeHint();
    void AdvancePrototypeDialogue();
    enum class EPrototypeMessage : uint8 { None, Narration, Hint, Terminal };
    EPrototypeMessage PrototypeMessage=EPrototypeMessage::None;
    int32 PrototypePresentedId=0;
    bool bPrototypeMessageFadingOut=false, bPrototypeMessageShown=false, bPrototypeMessageUsesSystem=false;
    FTimerHandle PrototypeHintTimer;
    friend class FOBTrainingDialogueAnimationTest;
    friend class FOBTrainingBackgroundBlurTest;
    friend class FOBTrainingDialogueMapSmoke;
    friend class FOBGuideAnimationSmoke;
    friend class FOBTrainingMediaPipePoseTest;
    friend class FOBTrainingMediaPipePoseSmoke;
    friend class FOBTrainingImpactSmoke;
    void MovePrototypeTo(int32 RouteIndex, int32 DialogueId = 0);
    void ArrivePrototype(int32 RouteIndex);
    void HandlePrototypeTurnCompleted();
    void TurnPrototypeAtBasicStop(bool bReturnToRoute);
    void TurnPrototypeAtShootingStop(float YawOffset);
    void TurnPrototypeForDialogue(float WorldYaw, int32 NextDialogueId);
    void BeginPrototypeExercise(EOBTrainingStep Step);
    void BeginPrototypeMockCountdown();
    void StartPrototypeMockTraining();
    FTimerHandle PrototypeMockStartTimer;
    bool bPrototypeMockStartPending=false;
    bool bPrototypeWeaponViewReturning=false;
    void CompletePrototypeExercise(EOBTrainingOutcome Outcome);
    void TickPrototype();
    void StopPrototypeTargets();
    bool IsPrototypeGroupTarget(AOBTrainingTarget* Target) const;
    bool IsPrototypePopUpCandidate(AOBTrainingTarget* Target) const;
    int32 GetPrototypeGroupHits() const;
    int32 PrototypeRouteIndex = 0;
    float PrototypeBasicArrivalYaw = 0;
    float PrototypeSignArrivalYaw = 0;
    int32 PrototypeAfterTurnDialogueId = 0;
    bool bPrototypeIntroPending = false, bPrototypeNarrationActive = false;
    double PrototypeIntroStartedAt = 0;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> PrototypeNarrationAudio;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> PrototypeHintAudio;
    bool bPrototypeTurning = false, bPrototypeReturnTurn = false;
    double PrototypeDeadline = 0, PrototypeReminderAt = 0, PrototypeHintUntil = 0;
    int32 PrototypeHintId = 0;
    FTimerHandle PrototypeNarrationTimer;
    FTimerHandle GuideReadyTimer;
    FTimerHandle PrototypeIntroTimer;
    void TryInitialize();
    bool ValidateConfiguration(FString &Error) const;
    void BeginZone(int32 Index);
    void BeginStep(EOBTrainingStep Step);
    void CompleteStep(EOBTrainingOutcome Outcome);
    void RetryPrerequisite();
    void FinishTraining(bool bAborted, const FString &Reason = FString(), bool bShowResult = true);
    void TransitionTo(EOBTrainingStep Step, bool bRetry = false);
    void AdvancePendingStep();
    void HandlePointReached(int32 Point);
    void HandleTriggerPressed();
    void HandleReloadCompleted();
    void HandleCalibrationCompleted();
    void HandleShotResolved(const FHitResult &Hit);
    void ResetAccuracyMeasurement();
    FString GetAccuracyMeasurementText() const;
    void HandleCoverState(bool bCrouching);
    void UpdateAimHold(float Seconds, const FVector2D &Cursor, const FVector2D &ViewportSize);
    void EvaluateTimedTargets();
    void StartPopUpTargets();
    void StopPopUpTargets(bool bImmediate = true);
    void RaiseNextTarget();
    void LowerActiveTarget();
    void HandleTargetPoseReached(AOBTrainingTarget* Target, bool bRaised);

    void HandleTimeout();
    bool AcceptsInput(EOBTrainingStep Step) const;
    float GetTimeLimit() const;
    FText GetInstructionText(EOBTrainingStep Step) const;
    FOBTrainingStepResult &StepResult(EOBTrainingStep Step);
    void ConfigurationError(const FString &Reason);
    void TestReload();
    void TestCalibration();
    void TestBreach();
    void TestCover();
    UFUNCTION() void HandleMessageShown();
    UFUNCTION() void HandleMessageFinished();
    UFUNCTION() void HandleDoorBreached(AOBTrainingDoor *Door, int32 AttemptId);
    UPROPERTY(Transient) TObjectPtr<AOBPlayerController> Controller;
    UPROPERTY(Transient) TObjectPtr<UOBActionComponent> Action;
    UPROPERTY(Transient) TObjectPtr<UOBPlayerSplineComponent> Spline;
    UPROPERTY(Transient) TObjectPtr<UOBPlayerOverlayWidget> Overlay;
    UPROPERTY(Transient) TObjectPtr<UOBMediaPipeOSCSubsystem> Motion;
    UPROPERTY(Transient) TObjectPtr<UOBMediaPipeInputComponent> PoseInput;
    bool bSavedPoseInputEnabled = true;
    UPROPERTY(Transient) TObjectPtr<UOBTrainingStatusWidget> StatusWidget;
    UPROPERTY(Transient) TSet<TObjectPtr<AOBTrainingTarget>> HitTargets;
    UPROPERTY(Transient) FOBTrainingResultData Result;
    bool bQASession=false;
    FTimerHandle StartupTimer, StepTimer, TransitionTimer;
    FTimerHandle PopUpTimer;
    UPROPERTY(Transient) TObjectPtr<AOBTrainingTarget> ActiveTimedTarget;
    TArray<int32> PopUpOrder;
    int32 PopUpCursor = 0, LastPopUpIndex = INDEX_NONE;
    bool bPopUpRunning = false;

    EOBTrainingStep PendingStep = EOBTrainingStep::Idle;
    bool bTransitionPending = false, bStepActive = false, bRetryMessage = false;
    bool bUseOverlayMessages = true;
    bool bLastBreachState = false, bBreachAccepted = false, bSavedAutoAdvance = true, bInitialized = false;
    int32 StartupAttempts = 0, Shots = 0, Hits = 0, Attempt = 0, ZoneIndex = 0, StageIndex = 0, AimIndex = 0;
    float HoldSeconds = 0;
    FVector FirstImpact = FVector::ZeroVector;
    double LatestAccuracyErrorCm = -1, MaxAccuracyErrorCm = 0;
    bool bAccuracyHasReference = false, bAccuracyLastShotHit = false;
    FString AccuracyRetryDetails;
    double Deadline = 0, StepStartedAt = 0, SessionStartedAt = 0;
};

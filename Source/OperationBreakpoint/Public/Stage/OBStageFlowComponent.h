#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Stage/OBStageScript.h"
#include "Training/OBTrainingTypes.h"
#include "Stage/OBC4Placement.h"
#include "OBStageFlowComponent.generated.h"
class AOBPlayerCharacter;
class AOBEnemyCharacter;
class UOBStageCinematicWidget;
class UOBPlayerOverlayWidget;
class UAudioComponent;
class UMediaSource;
class UDamageType;
class AOBBreachDoor;
class UOBMediaPipeOSCSubsystem;
class APlayerCameraManager;
class AOBPlayerController;
class UOBDamageScreenWidget;
class AOBBreachCharge;
class AOBAIController;

UENUM(BlueprintType)
enum class EOBStagePhase : uint8 { Inactive, Opening, Travel, Combat, SupportingFire, Ending, Complete, QA, AwaitingDeparture, Zone6Dialogue, Zone6Breach, Zone6Intro, ShortcutTransition };

UCLASS(ClassGroup=(OB),meta=(BlueprintSpawnableComponent))
class OPERATIONBREAKPOINT_API UOBStageFlowComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    void ExcludeDeviceSettingsTime(double Seconds) { if (ResultStartSeconds>=0 && !StageRecord.bFinished) ResultStartSeconds+=FMath::Max(0.,Seconds); }
    UOBStageFlowComponent();
    bool IsStageActive() const { return bActive; }
    float GetTravelMoveSpeed(float BaseSpeed) const;
    bool IsInputBlocked() const { return (bZone6EntrySceneStarted && Phase==EOBStagePhase::Travel) || Phase==EOBStagePhase::Opening || Phase==EOBStagePhase::Ending || Phase==EOBStagePhase::Complete || Phase==EOBStagePhase::SupportingFire || Phase==EOBStagePhase::AwaitingDeparture || Phase==EOBStagePhase::Zone6Dialogue || Phase==EOBStagePhase::Zone6Breach || Phase==EOBStagePhase::Zone6Intro || Phase==EOBStagePhase::ShortcutTransition; }
    bool ApplyPlayerHit(int32 DamageHearts=1);
    UOBDamageScreenWidget* GetDamageScreenWidget() const { return DamageScreen; }
    void RecordEnemyDamage(AOBEnemyCharacter* Enemy,FName HitZone,float ActualDamage,bool bKilled,AController* Instigator);
    void AbortForNavigation();
    const FOBTrainingResultData& GetStageRecord() const { return StageRecord; }
    bool HandleEncounterCleared();
    bool DeferTravelUntilNarrationDone();
    bool DeferInitialTravelUntilNarrationDone();
    int32 GetNextStageStop(int32 AfterPoint,int32 AuthoredNext) const;
    bool CanActivateEncounter(int32 Point) const;
    void PrepareEncounterAtStop(int32 Point);
    void HandleApproachToStop(int32 Point,float EstimatedSecondsRemaining);
    bool IsWaitingForBreach() const { return Phase==EOBStagePhase::Zone6Breach && !bBreachCompleted; }
    bool HasRequestedBreach() const { return bBreachRequested; }
    const FOBC4PlacementTracker& GetC4Placement() const { return C4Placement; }
    AOBBreachCharge* GetBreachCharge() const { return BreachCharge; }
    // Tutorial still consumes the two-hand gesture. Stage_01 accepts only C4 placement.
    void HandleBreachStateChanged(bool bRecognized);
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    bool AttachBreachChargeForQA();
#endif
    void SuspendForQA();
    EOBStagePhase GetPhase() const { return Phase; }
    int32 GetCurrentZone() const { return CurrentZone; }
    float GetRemainingSeconds() const { return RemainingSeconds; }
    int32 GetRemainingLives() const { return RemainingLives; }
    int32 GetLastSupportScript() const { return LastSupportScript; }
    bool DidSucceed() const { return bSucceeded; }
    UFUNCTION(BlueprintCallable,Category="OB|Stage") void PlayScript(int32 Id);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="OB|Stage") bool bEnabled=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="OB|Stage|Opening") bool bPlayOpening=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="OB|Stage|Dialogue") TSoftObjectPtr<UDataTable> ScriptTable;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="OB|Stage|Dialogue",meta=(ClampMin="0")) float VoiceVolumeMultiplier=3.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="OB|Stage|Dialogue",meta=(ClampMin="0",Units="s")) float ArrivalVoiceLeadSeconds=2.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="OB|Stage|Opening") TSoftObjectPtr<UMediaSource> OpeningVideo;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="OB|Stage|Opening") TSoftObjectPtr<USoundBase> OpeningVoice;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="OB|Stage|Ending") TSoftObjectPtr<UMediaSource> ClearVideo;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="OB|Stage|Ending") TSoftObjectPtr<UMediaSource> FailVideo;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="OB|Stage|Zones",meta=(ClampMin="1",Units="s")) float ZoneTimeLimit=60.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="OB|Stage|Zones",meta=(ClampMin="1",ClampMax="5")) int32 LivesPerZone=5;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="OB|Stage|Zone6",meta=(ClampMin="1",Units="s")) float HostageTimeLimit=10.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="OB|Stage|Zone6",meta=(ClampMin="0.05",ClampMax="1")) float EnemySlowMotionRate=.35f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="OB|Stage|Zone6",meta=(ClampMin="1",Units="cm/s")) float Zone6EntryMoveSpeed=300.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="OB|Stage|Zone6|Breach") bool bUseZone6Breach=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="OB|Stage|Zone6|Breach",meta=(ClampMin="1")) int32 Zone6DialoguePoint=83;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="OB|Stage|Zone6|Breach",meta=(ClampMin="1")) int32 Zone6BreachPoint=85;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="OB|Stage|Zone6|Breach",meta=(ClampMin="1")) int32 Zone6EncounterPoint=86;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="OB|Stage|Zone6|Breach",meta=(ClampMin="0",Units="s")) float Zone6DialogueGapSeconds=.6f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="OB|Stage|Zone6|Breach",meta=(ClampMin="0",Units="s")) float BreachFadeOutSeconds=.4f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="OB|Stage|Zone6|Breach",meta=(ClampMin="0",Units="s")) float BreachBlackHoldSeconds=2.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="OB|Stage|Zone6|Breach",meta=(ClampMin="0",Units="s")) float BreachFadeInSeconds=.4f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="OB|Stage|Zone6|Breach") TSoftObjectPtr<USoundBase> BreachExplosionSound;
    // Optional explicit level actor. Otherwise the nearest dedicated breach door is resolved at the breach stop.
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="OB|Stage|Zone6|Breach") TSoftObjectPtr<AOBBreachDoor> Zone6Door;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="OB|Stage|Support",meta=(ClampMin="0.05",Units="s")) float SupportShotInterval=.3f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="OB|Stage|Support") TArray<int32> SupportScriptIds={2019,2020,2021};
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="OB|Stage|Support") TSoftObjectPtr<USoundBase> SupportShotSound;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* Tick) override;
private:
    friend class FOBUICaptureSmoke;
    friend class FOBEnemyCombatPatternTest;
    friend class FOBEnemyCombatPatternMapSmoke;
    friend class FOBStageFlowTest;
    friend class FOBOpenFireInputModesTest;
    friend class FOBStageAmmoTest;
    friend class FOBStageAmmoMapSmoke;
    friend class FOBRPGCombatTest;
    friend class FOBStageFlowMapSmoke;
    friend class FOBStageNarrationRPGMapSmoke;
    friend class FOBStageDepartureMapSmoke;
    friend class FOBArrivalAudioMapSmoke;
    friend class FOBStageSquadMapSmoke;
    friend class FOBNavSquadMapSmoke;
    friend class FOBEnemyAimContextTest;
    friend class FOBEnemyAimMapSmoke;
    friend class FOBStageZone6GateTest;
    friend class FOBStageZone6MapSmoke;
    friend class FOBC4BreachMapSmoke;
    friend class FOBC4BreachTest;
    friend class FOBZone6DemoMapSmoke;
    friend class FOBStageShortcutMapSmoke;
    friend class FOBHostageShotIsolationTest;
    friend class FOBCombatCollisionTest;
    friend class FOBStageResultTest;
    friend class FOBStagePlanMapSmoke;
    friend class FOBFullFlowMapSmoke;
    void BindResultTracking();
    void StartResultTracking();
    void RecordPlayerShot(const FHitResult& Hit);
    void StartZoneRecord(int32 Zone,int32 Point);
    void FinalizeZoneRecord(bool bRescued=false);
    FOBStageZoneResult* ActiveZoneRecord();
    bool SaveStageRecord() const;
    void HandleActionPoint(int32 Point);
    void HandleTravel();
    void BeginDeparture(bool bViaAction);
    void ReleaseDeparture();
    void HoldPlayerMovement();
    void BeginZone6Dialogue();
    void PrepareZone6Scene();
    void StartZone6EntryScene();
    void BeginZone6Breach();
    void BeginC4Placement();
    void TickC4Placement();
    bool CompleteC4Placement();
    FVector GetBreachDirection() const;
    void ReleaseZone6Travel();
    void TickZone6Gate(float DeltaTime);
    void CancelZone6Gate();
    void StartZone6Demo();
    void BeginShortcutTransition();
    void TickShortcutTransition(float DeltaTime);
    bool MoveShortcutToZone5Exit();
    UFUNCTION() void HandleBreachCompleted(AOBBreachDoor* Door);
    void StartOpening();
    void HandleVideoOpened();
    void TickOpeningVoiceClock(float DeltaTime);
    void HandleVideoFinished();
    UFUNCTION() void HandleOpeningVoiceFinished();
    UFUNCTION() void HandleNarrationFinished();
    UFUNCTION() void HandleVoicePlayback(const USoundWave* Wave,float Percent);
    UFUNCTION() void HandleEncounterTriggered(int32 Point);
    UFUNCTION() void HandleHostageDamage(AActor* Victim,float Damage,const UDamageType* Type,AController* Instigator,AActor* Causer);
    void FinishOpeningIfReady();
    void FinishOpening();
    void StartSupport();
    void FireSupportShot();
    void FinishStage(bool bSuccess);
    void ShowCompletion();
    void FinishEndingVideo();
    void QueueScript(int32 Id,int32 Zone);
    void PlayAutomaticScript(int32 Id,int32 Zone);
    void StartNextNarration();
    void StartNarration();
    void TickNarration(float DeltaTime);
    void CancelNarration();
    void DiscardOldNarration(int32 Zone);
    void RestoreSlowMotion();
    void RefreshHUD();
    void ClearPresentation();
    void RestoreOpeningMovement();
    void UpdateOpeningSubtitle();
    void BeginPresentationSubtitles();
    void ShowPresentationSubtitle(int32 Id);
    UFUNCTION() void HandlePresentationSubtitleFinished();
    void UpdateEndingSubtitle();
    const FOBStageScriptRow* FindScript(int32 Id) const;
    UOBPlayerOverlayWidget* GetOverlay() const;
    UPROPERTY(Transient) TObjectPtr<AOBPlayerCharacter> Player;
    UPROPERTY(Transient) TObjectPtr<UDataTable> LoadedScripts;
    void EnsureDamageScreen();
    void ClearDamageScreen();
    friend class FOBDamageScreenTest;
    UPROPERTY(Transient) TObjectPtr<UOBDamageScreenWidget> DamageScreen;
    UPROPERTY(Transient) TObjectPtr<UOBStageCinematicWidget> Cinematic;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> Voice;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> NarrationVoice;
    UPROPERTY(Transient) TObjectPtr<AOBBreachDoor> ActiveBreachDoor;
    UPROPERTY(Transient) TObjectPtr<AOBBreachCharge> BreachCharge;
    FOBC4PlacementTracker C4Placement;
    TWeakObjectPtr<UOBMediaPipeOSCSubsystem> MotionInput;
    struct FQueuedScript { int32 Id=0;int32 Zone=0; };
    TArray<FQueuedScript> NarrationQueue;
    TSet<int32> AutomaticScriptIds;
    int32 ActiveScriptId=0,ActiveScriptZone=0;
    float NarrationDelay=0,NarrationElapsed=0,NarrationDuration=0;
    bool bNarrationStarted=false,bEndingNarration=false;
    bool bDeparturePending=false,bDepartureViaAction=false,bReleasingDeparture=false;
    bool bZone6DialogueStarted=false,bZone6DialogueFinished=false,bBreachSawNeutral=false,bBreachRequested=false,bBreachCompleted=false;
    float Zone6GapRemaining=0;
    float BreachFadeOutRemaining=0,BreachBlackRemaining=0;
    bool bBreachBlackHoldStarted=false;
    bool bBreachPhysicsStarted=false;
    uint64 BreachBlackFrame=MAX_uint64;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> BreachExplosionAudio;
    TWeakObjectPtr<APlayerCameraManager> BreachCamera;
    bool bEndingVideoOpened=false;
    bool bWaitingForOutcomeSubtitle=false;
    int32 PresentationSubtitleId=0;
    bool bPendingZone6Demo=false;
    bool bStageShortcutDemo=false,bStageShortcutTransferred=false;
    int32 ShortcutFadeStep=0;
    float ShortcutFadeRemaining=0;
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    int32 PendingQABreachPoint=INDEX_NONE;
#endif
    EOBStagePhase Phase=EOBStagePhase::Inactive;
    bool bActive=false,bSucceeded=false,bVideoFinished=false,bVoiceFinished=false,bVoiceStarted=false;
    int32 CurrentZone=0,RemainingLives=5,LastSupportScript=0,OpeningSubtitleId=0;
    float RemainingSeconds=0,SupportElapsed=0,OpeningElapsed=0,VoiceElapsed=0,PresentationElapsed=0;
    float VoiceWatchdogElapsed=0,VoicePlaybackCallbackAge=0;
    uint8 OpeningMovementMode=0,OpeningCustomMovementMode=0;
    bool bOpeningMovementSaved=false;
    TMap<TWeakObjectPtr<AActor>,float> OriginalDilations;
    TWeakObjectPtr<AOBPlayerController> ShotController;
    FOBTrainingResultData StageRecord;
    double ResultStartSeconds=-1;
    TMap<TWeakObjectPtr<AOBEnemyCharacter>,double> EnemyDamageAssessment;
    TSet<TWeakObjectPtr<AOBEnemyCharacter>> CreditedKills;
    double ZoneElapsedSeconds=0;
    float Zone6SlowMotionSeconds=0;
    TArray<TWeakObjectPtr<AOBAIController>> Zone6SceneGuards;
    bool bZone6EntrySceneStarted=false;
};

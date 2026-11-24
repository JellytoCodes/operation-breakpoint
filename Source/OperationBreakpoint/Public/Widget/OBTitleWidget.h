#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "OBTitleWidget.generated.h"
class UImage;
class UTextBlock;
class UMediaSource;
class USoundBase;
class UAudioComponent;
class UOBUIVideo;
class UWorld;
class UWidgetAnimation;
struct FOBOpenFireState;
struct FOBOpenFireEvents;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOBTitleWidgetAction);

/** Full idle loop and separate entrance movie; narration completion controls travel. */
UCLASS(Abstract, Blueprintable)
class OPERATIONBREAKPOINT_API UOBTitleWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="OB|UI|Title|Video") TSoftObjectPtr<UMediaSource> TitleSource=TSoftObjectPtr<UMediaSource>(FSoftObjectPath(TEXT("/Game/Assets/UI/TitleLoop.TitleLoop")));
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="OB|UI|Title|Video") TSoftObjectPtr<UMediaSource> EntranceSource=TSoftObjectPtr<UMediaSource>(FSoftObjectPath(TEXT("/Game/Assets/UI/TravleToTraining.TravleToTraining")));
    // Retained only for old Blueprint serialization; TitleSource now supplies idle playback.
    UPROPERTY(meta=(DeprecatedProperty)) TSoftObjectPtr<UMediaSource> IdleSource;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="OB|UI|Title|Video") float IdleStartSeconds=0;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="OB|UI|Title|Video") float IdleEndSeconds=0;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="OB|UI|Title|Video") float EntranceStartSeconds=0;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="OB|UI|Title|Video") float EntranceEndSeconds=0;
    UPROPERTY(EditDefaultsOnly,BlueprintReadWrite,Category="OB|UI|Title",meta=(ClampMin="0.0",Units="s")) float TrainingTravelDelaySeconds=1.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="OB|UI|Title|Video", meta=(ClampMin="0.0",ClampMax="0.25")) float LoopBlendSeconds=.1f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="OB|UI|Title") TSoftObjectPtr<USoundBase> EntranceVoice=TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Assets/FX/SFX/Title/TrainingEntrance.TrainingEntrance")));
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="OB|UI|Title") TSoftObjectPtr<UWorld> TrainingMap=TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Maps/TutorialMap.TutorialMap")));
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="OB|UI|Title") bool bStartRequested=false;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="OB|UI|Title") int32 IdleLoopCount=0;
    UPROPERTY(BlueprintAssignable, Category = "OB|UI|Title")
    FOBTitleWidgetAction OnStartRequested;

    UPROPERTY(BlueprintAssignable, Category = "OB|UI|Title")
    FOBTitleWidgetAction OnQuitRequested;
    UPROPERTY(BlueprintAssignable, Category = "OB|UI|Title")
    FOBTitleWidgetAction OnMissionRequested;
    UPROPERTY(BlueprintAssignable, Category = "OB|UI|Title")
    FOBTitleWidgetAction OnTrainingRequested;
    UFUNCTION(BlueprintPure, Category = "OB|UI|Title") bool CanStartMission() const;
    UFUNCTION(BlueprintCallable, Category = "OB|UI|Title") void RequestMission();
    UFUNCTION(BlueprintCallable, Category = "OB|UI|Title") void RequestTraining();

    UFUNCTION(BlueprintCallable, Category = "OB|UI|Title")
    void RequestStart();

    UFUNCTION(BlueprintCallable, Category = "OB|UI|Title")
    void RequestQuit();
protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    virtual FReply NativeOnPreviewMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event) override;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional)) TObjectPtr<UImage> BackgroundVideo;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional)) TObjectPtr<UImage> LogoImage;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional)) TObjectPtr<UImage> TitleImage;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> PressText;
    UPROPERTY(Transient, meta=(BindWidgetAnimOptional)) TObjectPtr<UWidgetAnimation> PressTextFadeInOut;
private:
    friend class FOBOpenFireTitleInputTest;
    friend class FOBDeviceSettingsSmoke;
    double DeviceSettingsPausedAt=0;
    void HandleOpenFireInput(const FOBOpenFireEvents& Events);
    void HandleOpenFireStart(const FOBOpenFireState& State,bool bFocused);
    bool bOpenFireStartArmed=false;
    int32 LastOpenFireSequence=-1;
    FString OpenFireSession;
    friend class FOBTitleGuideMediaSmoke;
    friend class FOBTitleSkipMapSmoke;
    friend class FOBTitleLoopMapSmoke;
    friend class FOBTitlePackageSmoke;
    friend class FOBTitleEntranceTimingTest;
    friend class FOBTitleEntranceTimingSmoke;
    friend class FOBFullFlowMapSmoke;
    friend class FOBStageShortcutMapSmoke;
    void OpenVideo();
    UMediaSource* ResolvePortableMovie(UMediaSource* Source, const TCHAR* BuiltinAsset, const TCHAR* MovieName);
    void PrepareClip(int32 Index);
    void ClipOpened(int32 Index);
    void ClipSeekCompleted(int32 Index);
    void BeginBlend(int32 Index);
    void FinishBlend();
    void FinishEntrance();
    bool IsEntranceTravelReady(double Now) const;
    UFUNCTION() void HandleEntranceVoiceFinished();
    UFUNCTION() void HandleMediaOpened(FString URL);
    UFUNCTION() void HandleSeekCompleted();
    UFUNCTION() void HandleMediaEnded();
    UFUNCTION() void HandleStandbyOpened(FString URL);
    UFUNCTION() void HandleStandbySeekCompleted();
    UFUNCTION() void HandleEntranceOpened(FString URL);
    UFUNCTION() void HandleEntranceSeekCompleted();
    UFUNCTION() void HandleMediaFailed(FString URL);
    UPROPERTY(Transient) TObjectPtr<UOBUIVideo> Video;
    UPROPERTY(Transient) TArray<TObjectPtr<UOBUIVideo>> Clips;
    UPROPERTY(Transient) TArray<TObjectPtr<UMediaSource>> PlaybackSources;
    UPROPERTY(Transient) TObjectPtr<UImage> BlendImage;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ShortcutStateText;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> Voice;
    bool bMediaOpened=false,bSeeking=false,bEntrancePlaying=false,bNavigationRequested=false,bPlaybackFailed=false;
    bool bEntranceVideoFinished=false,bEntranceVoiceFinished=false;
    double EntranceVoiceFinishedAt=0,TrainingTravelAt=0;
    enum class EClipState : uint8 { Opening, Seeking, AwaitingSeekClock, Warming, Settling, Ready, Playing };
    EClipState ClipStates[3]={EClipState::Opening,EClipState::Opening,EClipState::Opening};
    uint64 ReadyAfterFrame[3]={0,0,0};
    double PrepareDeadline[3]={0,0,0};
    int32 ActiveClip=INDEX_NONE,BlendClip=INDEX_NONE;
    double BlendStartedAt=0;
};

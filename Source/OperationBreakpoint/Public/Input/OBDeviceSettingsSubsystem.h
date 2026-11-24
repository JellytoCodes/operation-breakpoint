#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "HAL/PlatformProcess.h"
#include "Stage/OBC4Placement.h"
#include "OBDeviceSettingsSubsystem.generated.h"

class IInputProcessor;
class SWidget;
class UTexture2D;
class UMediaPlayer;
class UAudioComponent;
class USoundBase;
struct FOBOpenFireEvents;

// Runtime UI, deliberately independent of cheats/editor/test map cooking.
UCLASS()
class OPERATIONBREAKPOINT_API UOBDeviceSettingsSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual bool IsTickable() const override;
    virtual bool IsTickableWhenPaused() const override { return true; }
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UOBDeviceSettingsSubsystem, STATGROUP_Tickables); }
    void Toggle(int32 Device);
    void Close();
    bool IsOpen() const { return Mode!=0; }
    int32 GetMode() const { return Mode; }
    static constexpr int32 MagazineCapacity=20;
    static constexpr int32 MaxBulletMarks=60;
    void Shoot();
    void Reload();
    int32 GetAmmo() const { return Ammo; }
    const TArray<FVector2D>& GetMarks() const { return Marks; }
    FVector2D GetTargetCenter(FVector2D CanvasSize) const;
    static float GetTargetRadius(FVector2D CanvasSize) { return FMath::Min(CanvasSize.X,CanvasSize.Y)*.035f; }
    bool GetAim(FVector2D& UV) const;
    FString Status() const;
    void Command(const FString& Key);
    void Reconnect();
    double ReadNumber(const FString& Section,const FString& Key,double Default) const;
    bool WriteNumber(const FString& Section,const FString& Key,double Value);
    FString GunSection() const;
    UTexture2D* GetPreview() const { return Preview; }
    void StartC4Test();
    bool IsC4Test() const { return bC4Test; }
    const FOBC4PlacementTracker& GetC4Test() const { return C4Test; }
    static FString SettingsPath();
    static FString RuntimeDir();
private:
    friend class FOBUICaptureSmoke;
    friend class FOBDeviceSettingsSmoke;
    void HandleGun(const FOBOpenFireEvents& Events);
    void SpawnTarget();
    void StartHelper(int32 Device);
    void StopHelpers();
    void SetCameraPreviewRequested(bool bRequested) const;
    void SuspendPresentation();
    void PrepareGunAudio();
    void PlayGunFeedback(bool bReload,bool bEmpty=false);
    UPROPERTY(Transient) TObjectPtr<USoundBase> GunFireSound;
    UPROPERTY(Transient) TObjectPtr<USoundBase> GunEmptySound;
    UPROPERTY(Transient) TObjectPtr<USoundBase> GunReloadSound;
    TArray<TWeakObjectPtr<UAudioComponent>> GunFeedbackAudio;
    TMap<TWeakObjectPtr<UMediaPlayer>,float> SuspendedMedia;
    TArray<TWeakObjectPtr<UAudioComponent>> SuspendedAudio;
    TSharedPtr<IInputProcessor> InputProcessor;
    TSharedPtr<SWidget> Screen;
    TWeakPtr<SWidget> PreviousFocus;
    TWeakObjectPtr<APlayerController> PausedController;
    UPROPERTY(Transient) TObjectPtr<UTexture2D> Preview;
    FString PreviewFrameId;
    double LastPoll=0,LastPreviewPoll=0,OpenedRealTime=0;
    double PreviewStatsStart=0,PreviewUploadSeconds=0;
    int32 PreviewUploads=0;
    int32 Mode=0,Ammo=MagazineCapacity,NextMark=0;
    bool bWasPaused=false,bWasCursor=false,bTriggerNeedsRelease=true;
    TArray<FVector2D> Marks;
    FVector2D TargetPosition=FVector2D(.5,.5);
    FProcHandle GunProcess,CameraProcess;
    FString GunStop,CameraStop;
    FString CameraSessionDir;
    FString HelperError;
    bool bC4Test=false;
    FOBC4PlacementTracker C4Test;
};

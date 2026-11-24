#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "OBStageCinematicWidget.generated.h"
class UImage;
class UOBUIVideo;
class UMediaSource;
class UMediaPlayer;
class UMediaSoundComponent;

UCLASS()
class OPERATIONBREAKPOINT_API UOBStageCinematicWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    bool OpenVideo(UMediaSource* Source, bool bVideoAudio, float AudioVolumeMultiplier=1.f);
    void CloseVideo();
    UMediaPlayer* GetMediaPlayer() const;
    FSimpleMulticastDelegate OnVideoOpened;
    FSimpleMulticastDelegate OnVideoFinished;
protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeDestruct() override;
private:
    UFUNCTION() void HandleOpened(FString URL);
    UFUNCTION() void HandleFailed(FString URL);
    UFUNCTION() void HandleFinished();
    UPROPERTY(Transient) TObjectPtr<UImage> VideoImage;
    UPROPERTY(Transient) TObjectPtr<UOBUIVideo> Video;
    UPROPERTY(Transient) TObjectPtr<UMediaSoundComponent> MediaSound;
    bool bFinished=false;
};

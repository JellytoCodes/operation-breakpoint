#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "OBUIVideo.generated.h"
class UImage;
class UMediaPlayer;
class UMediaSource;
class UMediaTexture;
class UMaterialInstanceDynamic;

/** Each UI instance owns its player, texture and material; closing one cannot stop another. */
UCLASS()
class OPERATIONBREAKPOINT_API UOBUIVideo : public UObject
{
    GENERATED_BODY()
public:
    bool Initialize(UImage* Image,bool bBindImmediately=true);
    void BindToImage(UImage* Image) const;
    bool Open(UMediaSource* Source,bool bLoop,bool bPlayOnOpen);
    void Close();
    UMediaPlayer* GetPlayer() const { return Player; }
    bool IsReadyToDisplay() const;
private:
    UPROPERTY(Transient) TObjectPtr<UMediaPlayer> Player;
    UPROPERTY(Transient) TObjectPtr<UMediaTexture> Texture;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> Material;
};

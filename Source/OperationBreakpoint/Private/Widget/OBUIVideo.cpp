#include "Widget/OBUIVideo.h"
#include "Components/Image.h"
#include "MediaPlayer.h"
#include "MediaSource.h"
#include "MediaTexture.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

bool UOBUIVideo::Initialize(UImage* Image,bool bBindImmediately)
{
    auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Blueprints/Widget/Media/M_UIVideo.M_UIVideo"));
    if (!Image || !Base) return false;
    Player=NewObject<UMediaPlayer>(this);
    Texture=NewObject<UMediaTexture>(this);
    Texture->NewStyleOutput=true;
    Texture->SetMediaPlayer(Player); Texture->UpdateResource();
    Material=UMaterialInstanceDynamic::Create(Base,this);
    Material->SetTextureParameterValue(TEXT("VideoTexture"),Texture);
    if (bBindImmediately) BindToImage(Image);
    return true;
}
void UOBUIVideo::BindToImage(UImage* Image) const { if (Image && Material) Image->SetBrushFromMaterial(Material); }
bool UOBUIVideo::Open(UMediaSource* Source,bool bLoop,bool bPlayOnOpen)
{
    if (!Player || !Source) return false;
    Player->PlayOnOpen=bPlayOnOpen; Player->SetLooping(bLoop);
    return Player->OpenSource(Source);
}
void UOBUIVideo::Close() { if (Player) Player->Close(); }
bool UOBUIVideo::IsReadyToDisplay() const
{
    return Player && Texture && Player->IsPlaying() && Player->GetTime().GetTotalSeconds()>.1
        && Texture->GetWidth()>16 && Texture->GetHeight()>16;
}

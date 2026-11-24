#include "Widget/OBDamageScreenWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"

UOBDamageScreenWidget::UOBDamageScreenWidget(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
    SetIsFocusable(false);
    HitMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Assets/FX/Materials/M_UI_PlayerHit.M_UI_PlayerHit")));
    InjuredMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Assets/FX/Materials/M_UI_PlayerInjured.M_UI_PlayerInjured")));
}
void UOBDamageScreenWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    auto* Root=WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(),TEXT("DamageEffects"));
    WidgetTree->RootWidget=Root;
    auto AddImage=[&](const TCHAR* Name,UMaterialInterface* Material,TObjectPtr<UMaterialInstanceDynamic>& MID) {
        auto* Image=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),Name);
        auto* Slot=Root->AddChildToOverlay(Image);Slot->SetHorizontalAlignment(HAlign_Fill);Slot->SetVerticalAlignment(VAlign_Fill);
        if (Material) { MID=UMaterialInstanceDynamic::Create(Material,this);Image->SetBrushFromMaterial(MID); }
        Image->SetVisibility(ESlateVisibility::Collapsed);return Image;
    };
    InjuredImage=AddImage(TEXT("InjuredPulse"),InjuredMaterial.LoadSynchronous(),InjuredMID);
    HitImage=AddImage(TEXT("HitFracture"),HitMaterial.LoadSynchronous(),HitMID);
    SetVisibility(ESlateVisibility::HitTestInvisible);
}
void UOBDamageScreenWidget::PlayHit()
{
    // An unresolved brush renders as a full-screen white quad in UMG.
    if (!HitMID || !HitImage) return;
    HitElapsed=0.f;
    HitStartedAt=GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0;
    if (HitMID) HitMID->SetScalarParameterValue(TEXT("Age"),0.f);
    if (HitImage) HitImage->SetVisibility(ESlateVisibility::HitTestInvisible);
}
void UOBDamageScreenWidget::SetInjuredActive(bool bActive)
{
    if (bInjuredActive==bActive) return;
    bInjuredActive=bActive;InjuredElapsed=0.f;
    InjuredStartedAt=GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0;
    if (InjuredMID) InjuredMID->SetScalarParameterValue(TEXT("Age"),0.f);
    if (InjuredImage) InjuredImage->SetVisibility(bActive && InjuredMID ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}
void UOBDamageScreenWidget::ResetEffects()
{
    HitElapsed=.5f;HitStartedAt=-1;InjuredElapsed=0.f;bInjuredActive=false;
    if (HitImage) HitImage->SetVisibility(ESlateVisibility::Collapsed);
    if (InjuredImage) InjuredImage->SetVisibility(ESlateVisibility::Collapsed);
}
void UOBDamageScreenWidget::UpdateForTime(double Now,float Aspect)
{
    if (HitStartedAt>=0) HitElapsed=FMath::Clamp(float(Now-HitStartedAt),0.f,.5f);
    if (bInjuredActive) InjuredElapsed=FMath::Fmod(float(FMath::Max(0.,Now-InjuredStartedAt)),2.7f);
    UpdateVisuals(Aspect);
}
void UOBDamageScreenWidget::UpdateVisuals(float Aspect)
{
    if (IsHitActive() && HitMID) { HitMID->SetScalarParameterValue(TEXT("Age"),HitElapsed);HitMID->SetScalarParameterValue(TEXT("Aspect"),Aspect); }
    if (!IsHitActive() && HitImage) HitImage->SetVisibility(ESlateVisibility::Collapsed);
    if (bInjuredActive && InjuredMID) InjuredMID->SetScalarParameterValue(TEXT("Age"),InjuredElapsed);
}
void UOBDamageScreenWidget::NativeTick(const FGeometry& Geometry,float DeltaTime)
{
    Super::NativeTick(Geometry,DeltaTime);
    // Slate delta can include a loading frame before the hit was requested.
    // Use the real clock at the actual damage event, independent of dilation.
    const FVector2D Size=Geometry.GetLocalSize();
    UpdateForTime(GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.,Size.Y>0.f ? Size.X/Size.Y : 16.f/9.f);
}
void UOBDamageScreenWidget::NativeDestruct()
{
    ResetEffects();Super::NativeDestruct();
}

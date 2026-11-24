#include "Widget/OBRadialProgressBar.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Widgets/Notifications/SProgressBar.h"

void UOBRadialProgressBar::SetRadialPercent(float Value)
{
    SetPercent(FMath::IsFinite(Value) ? FMath::Clamp(Value,0.f,1.f) : 0.f);
    UpdateRadialMaterial();
}
void UOBRadialProgressBar::SynchronizeProperties()
{
    Super::SynchronizeProperties();
    UpdateRadialMaterial();
}
void UOBRadialProgressBar::UpdateRadialMaterial()
{
    if (!MyProgressBar.IsValid()) return; // Asset templates keep a persistent material brush.
    auto Style=GetWidgetStyle();
    auto* Base=Cast<UMaterialInterface>(Style.FillImage.GetResourceObject());
    if (!Base) return;
    if (Base!=RadialMaterial)
    {
        RadialMaterial=UMaterialInstanceDynamic::Create(Base,this);
        Style.FillImage.SetResourceObject(RadialMaterial);
        SetWidgetStyle(Style);
    }
    RadialMaterial->SetScalarParameterValue(TEXT("Progress"),GetPercent());
    // Percent remains the real logical value; the material performs the radial
    // mask, so Slate must not additionally crop the texture from left to right.
    MyProgressBar->SetPercent(1.f);
}

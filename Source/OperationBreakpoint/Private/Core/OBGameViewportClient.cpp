#include "Core/OBGameViewportClient.h"
#include "Core/OBGameInstance.h"
#include "Core/OBPlayerController.h"
#include "Engine/World.h"
#include "InputKeyEventArgs.h"
#include "Input/OBDeviceSettingsSubsystem.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widget/OBHUD.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SNullWidget.h"

namespace
{
class SOBCrosshair : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SOBCrosshair) {} SLATE_END_ARGS()
    void Construct(const FArguments&) { SetVisibility(EVisibility::HitTestInvisible); }
    FVector2D ComputeDesiredSize(float) const override { return FVector2D(30, 30); }
    int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&,
        FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle&, bool) const override
    {
        const FVector2D Scale = Geometry.GetLocalSize() / FVector2D(30, 30);
        const FSlateBrush* Brush = FCoreStyle::Get().GetBrush("WhiteBrush");
        auto Draw = [&](FVector2D Position, FVector2D Size, int32 Offset, FLinearColor Tint) {
            FSlateDrawElement::MakeBox(Elements, Layer + Offset,
                Geometry.ToPaintGeometry(Size * Scale, FSlateLayoutTransform(Position * Scale)), Brush,
                ESlateDrawEffect::None, Tint);
        };
        const FVector2D Positions[] = {{2, 14}, {19, 14}, {14, 2}, {14, 19}};
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(Positions); ++Index)
        {
            const FVector2D Size = Index < 2 ? FVector2D(9, 2) : FVector2D(2, 9);
            Draw(Positions[Index] - FVector2D(1, 1), Size + FVector2D(2, 2), 0, FLinearColor::Black);
            Draw(Positions[Index], Size, 1, FLinearColor::White);
        }
        return Layer + 1;
    }
};
}

bool UOBGameViewportClient::InputKey(const FInputKeyEventArgs& Event)
{
    auto* PC = GetWorld() ? Cast<AOBPlayerController>(GetWorld()->GetFirstPlayerController()) : nullptr;
    if (PC && PC->HandleOperatorShortcut(Event.Key, Event.Event)) return true;
    if (Event.Key==EKeys::F5)
    {
        if (Event.Event==IE_Pressed)
            if (PC) PC->ReturnToMainMenu();
        return true;
    }
    return Super::InputKey(Event);
}

TOptional<TSharedRef<SWidget>> UOBGameViewportClient::MapCursor(FViewport* InViewport, const FCursorReply& Reply)
{
    if (Reply.GetCursorType() != EMouseCursor::Default || !GetWorld()) return Super::MapCursor(InViewport, Reply);
    const auto* Instance = GetWorld()->GetGameInstance<UOBGameInstance>();
    const auto* PC = Cast<AOBPlayerController>(GetWorld()->GetFirstPlayerController());
    const auto* Settings = Instance ? Instance->GetSubsystem<UOBDeviceSettingsSubsystem>() : nullptr;
    const auto* HUD = PC ? PC->GetOBHUD() : nullptr;
    if ((Settings && Settings->IsOpen()) || (HUD && HUD->IsMenuVisible())) return {};
    if (!Instance || !PC) return Super::MapCursor(InViewport, Reply);
    if (!Instance->IsCrosshairEnabled()) return SNullWidget::NullWidget;
    if (!Crosshair) Crosshair = SNew(SOBCrosshair);
    return Crosshair.ToSharedRef();
}

#include "Widget/OBHUD.h"

#include "GameFramework/PlayerController.h"
#include "Widget/OBTitleWidget.h"
#include "Widget/OBPlayerOverlayWidget.h"
#include "Widget/OBResultWidget.h"
#include "Training/OBTrainingResultWidget.h"
#include "Kismet/GameplayStatics.h"

namespace
{
    template<typename T>
    bool EnsureWidget(APlayerController* Owner, TSubclassOf<T> Class, TObjectPtr<T>& Widget)
    {
        if (!IsValid(Owner) || !Owner->IsLocalController()) return false;
        if (IsValid(Widget)) return true;
        if (!Class || Class->HasAnyClassFlags(CLASS_Abstract)) return false;
        Widget = CreateWidget<T>(Owner, Class);
        return IsValid(Widget);
    }

    bool MountWidget(UUserWidget* Widget, int32 ZOrder)
    {
        if (Widget->IsInViewport()) return true;
        Widget->SetVisibility(ESlateVisibility::Collapsed);
        return Widget->AddToPlayerScreen(ZOrder);
    }

    void CollapseWidget(UUserWidget* Widget)
    {
        if (IsValid(Widget)) Widget->SetVisibility(ESlateVisibility::Collapsed);
    }
}

AOBHUD::AOBHUD()
{
    PrimaryActorTick.bCanEverTick = false;
}

void AOBHUD::BeginPlay()
{
    Super::BeginPlay();
    // A controller may survive map travel after a title/result menu used UI-only input.
    ApplyInputMode(nullptr);
    if ((InitialWidget == EOBInitialWidget::Title || UGameplayStatics::GetCurrentLevelName(this,true)==TEXT("TitleMap")) && TitleWidgetClass)
    {
        ShowTitleWidget();
    }
    else if (InitialWidget == EOBInitialWidget::PlayerOverlay && PlayerOverlayWidgetClass)
    {
        ShowPlayerOverlayWidget();
    }
}

void AOBHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    for (UUserWidget* Widget : { static_cast<UUserWidget*>(TitleWidget.Get()),
        static_cast<UUserWidget*>(PlayerOverlayWidget.Get()), ResultWidget.Get() })
    {
        if (IsValid(Widget)) Widget->RemoveFromParent();
    }
    TitleWidget = nullptr;
    PlayerOverlayWidget = nullptr;
    ResultWidget = nullptr;
    Super::EndPlay(EndPlayReason);
}

bool AOBHUD::ShowTitleWidget()
{
    if (!EnsureWidget(GetOwningPlayerController(), TitleWidgetClass, TitleWidget)) return false;
    if (!MountWidget(TitleWidget, 100)) return false;
    HideScreenWidgets();
    TitleWidget->SetVisibility(ESlateVisibility::Visible);
    ApplyInputMode(TitleWidget);
    return true;
}

bool AOBHUD::ShowPlayerOverlayWidget()
{
    if (!EnsureWidget(GetOwningPlayerController(), PlayerOverlayWidgetClass, PlayerOverlayWidget)) return false;
    if (!MountWidget(PlayerOverlayWidget, 0)) return false;
    HideScreenWidgets();
    PlayerOverlayWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
    ApplyInputMode(nullptr);
    return true;
}

bool AOBHUD::ShowResultWidget(TSubclassOf<UUserWidget> FallbackClass)
{
    const TSubclassOf<UUserWidget> Class = ResultWidgetClass ? ResultWidgetClass : FallbackClass;
    if (!EnsureWidget(GetOwningPlayerController(), Class, ResultWidget)) return false;
    if (!MountWidget(ResultWidget, 100)) return false;
    HideScreenWidgets();
    ResultWidget->SetVisibility(ESlateVisibility::Visible);
    ApplyInputMode(ResultWidget);
    return true;
}

void AOBHUD::HideScreenWidgets()
{
    CollapseWidget(TitleWidget);
    CollapseWidget(PlayerOverlayWidget);
    CollapseWidget(ResultWidget);
}

void AOBHUD::HideAllWidgets()
{
    HideScreenWidgets();
    ApplyInputMode(nullptr);
}

bool AOBHUD::IsMenuVisible() const
{
    return (IsValid(TitleWidget) && TitleWidget->IsInViewport() && TitleWidget->IsVisible())
        || (IsValid(ResultWidget) && ResultWidget->IsInViewport() && ResultWidget->IsVisible())
        || (IsValid(PlayerOverlayWidget) && (PlayerOverlayWidget->IsTrainingResultVisible() || PlayerOverlayWidget->IsTrainingPauseVisible()));
}

UOBResultWidget* AOBHUD::GetResultWidget() const
{
    return Cast<UOBResultWidget>(ResultWidget);
}

bool AOBHUD::ShowTrainingResult(const FOBTrainingResultData& Data)
{
    if (!ShowPlayerOverlayWidget()) return false;
    CollapseWidget(TitleWidget); CollapseWidget(ResultWidget);
    PlayerOverlayWidget->ShowTrainingResult(Data);
    return PlayerOverlayWidget->IsTrainingResultVisible();
}

void AOBHUD::ApplyInputMode(UUserWidget* MenuWidget)
{
    APlayerController* Controller = GetOwningPlayerController();
    if (!IsValid(Controller) || !Controller->IsLocalController()) return;
    Controller->bShowMouseCursor = true;
    if (MenuWidget)
    {
        FInputModeUIOnly Mode;
        Mode.SetLockMouseToViewportBehavior(EMouseLockMode::LockAlways);
        Controller->SetInputMode(Mode);
    }
    else
    {
        FInputModeGameAndUI Mode;
        Mode.SetHideCursorDuringCapture(false);
        Mode.SetLockMouseToViewportBehavior(EMouseLockMode::LockAlways);
        Controller->SetInputMode(Mode);
    }
}

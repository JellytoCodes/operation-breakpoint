#include "Input/OBOpenFireInputComponent.h"
#include "Input/OBOpenFireSubsystem.h"
#include "Core/OBPlayerController.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "UnrealClient.h"

UOBOpenFireInputComponent::UOBOpenFireInputComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bTickEvenWhenPaused = true;
}

void UOBOpenFireInputComponent::BeginPlay()
{
    Super::BeginPlay();
    Controller = Cast<AOBPlayerController>(GetOwner());
    if (Controller && Controller->IsLocalController() && GetWorld()->GetGameInstance())
        Receiver = GetWorld()->GetGameInstance()->GetSubsystem<UOBOpenFireSubsystem>();
    SetComponentTickEnabled(Receiver != nullptr);
    if (Receiver) Receiver->OnInputReceived.AddUObject(this, &ThisClass::HandleInput);
}

void UOBOpenFireInputComponent::EndPlay(EEndPlayReason::Type Reason)
{
    if (Receiver) Receiver->OnInputReceived.RemoveAll(this);
    Super::EndPlay(Reason);
}

bool UOBOpenFireInputComponent::HasGameFocus() const
{
    const ULocalPlayer* Local = Controller ? Controller->GetLocalPlayer() : nullptr;
    return Local && Local->ViewportClient && Local->ViewportClient->Viewport
        && Local->ViewportClient->Viewport->IsForegroundWindow();
}

void UOBOpenFireInputComponent::UpdateCursor()
{
    FVector2D Aim;
    if (!Receiver || !Receiver->GetAimPosition(Aim)) return;
    int32 Width = 0, Height = 0;
    Controller->GetViewportSize(Width, Height);
    if (Width > 0 && Height > 0)
        Controller->SetMouseLocation(FMath::RoundToInt(Aim.X*(Width-1)), FMath::RoundToInt(Aim.Y*(Height-1)));
}

void UOBOpenFireInputComponent::HandleInput(const FOBOpenFireEvents& Events)
{
    if (!Controller) return;
    const bool bFocused = HasGameFocus();
    if (bFocused) UpdateCursor();
    // Returning to a window with the trigger held must not activate a hold menu.
    if (!bFocused)
    {
        Controller->CancelOpenFireInput();
        bHadFocus = false;
        return;
    }
    if (!bHadFocus)
    {
        Controller->CancelOpenFireInput();
        bHadFocus = !Events.bInteractionHeld;
        return;
    }
    Controller->ApplyOpenFireInput(Events);
    bHadFocus = bFocused;
}

void UOBOpenFireInputComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Tick)
{
    Super::TickComponent(DeltaTime, TickType, Tick);
    if (!Receiver || !Controller) return;
    if (!HasGameFocus() || !Receiver->IsConnected())
    {
        Controller->CancelOpenFireInput();
        bHadFocus = false;
    }
    else UpdateCursor();
}

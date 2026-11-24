

#include "MediaPipe/OBMediaPipeInputComponent.h"

#include "Engine/GameInstance.h"
#include "Input/OBDeviceSettingsSubsystem.h"
#include "GameFramework/Character.h"
#include "MediaPipe/OBMediaPipeOSCSubsystem.h"
#include "Character/OBActionComponent.h"
#include "Character/OBPlayerSplineComponent.h"

void UOBMediaPipeInputComponent::BeginPlay()
{
    Super::BeginPlay();

    OwnerCharacter = CastChecked<ACharacter>(GetOwner());
    MediaPipeSubsystem = GetWorld()->GetGameInstance()->GetSubsystem<UOBMediaPipeOSCSubsystem>();

    MediaPipeSubsystem->OnCrouchStateChanged.AddUObject(this, &ThisClass::HandleCrouchStateChanged);
    HandleCrouchStateChanged(MediaPipeSubsystem->IsCrouching());
}

void UOBMediaPipeInputComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (MediaPipeSubsystem) MediaPipeSubsystem->OnCrouchStateChanged.RemoveAll(this);
    Super::EndPlay(EndPlayReason);
}

void UOBMediaPipeInputComponent::HandleCrouchStateChanged(bool bIsCrouching)
{
    if (!bPoseInputEnabled || !OwnerCharacter) return;
    if (auto* Settings=GetWorld()->GetGameInstance()->GetSubsystem<UOBDeviceSettingsSubsystem>(); Settings && Settings->IsOpen()) return;
    const UOBActionComponent* ActionComponent = OwnerCharacter->FindComponentByClass<UOBActionComponent>();

    // A timeout/stand packet must still release a held crouch during a pause.
    if (bIsCrouching && ActionComponent && ActionComponent->GetActionState() == EOBActionState::Prepare) return;
    if (const auto* Rail=OwnerCharacter->FindComponentByClass<UOBPlayerSplineComponent>(); Rail && Rail->IsCameraTuningForQA()) return;

    bIsCrouching ? OwnerCharacter->Crouch() : OwnerCharacter->UnCrouch();
}

void UOBMediaPipeInputComponent::RefreshPoseState()
{
    if (MediaPipeSubsystem && MediaPipeSubsystem->IsBridgeConnected())
        HandleCrouchStateChanged(MediaPipeSubsystem->IsCrouching());
}

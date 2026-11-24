#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Input/OBOpenFireTypes.h"
#include "OBOpenFireInputComponent.generated.h"

class AOBPlayerController;
class UOBOpenFireSubsystem;

UCLASS()
class OPERATIONBREAKPOINT_API UOBOpenFireInputComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UOBOpenFireInputComponent();
    virtual void BeginPlay() override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Tick) override;
private:
    void HandleInput(const FOBOpenFireEvents& Events);
    bool HasGameFocus() const;
    void UpdateCursor();
    UPROPERTY(Transient)
    TObjectPtr<AOBPlayerController> Controller;
    UPROPERTY(Transient)
    TObjectPtr<UOBOpenFireSubsystem> Receiver;
    bool bHadFocus = false;
};

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Input/OBOpenFireTypes.h"
#include "OBOpenFireSubsystem.generated.h"

class UOSCServer;
struct FOSCMessage;
DECLARE_MULTICAST_DELEGATE_OneParam(FOBOpenFireInputReceived, const FOBOpenFireEvents&);

UCLASS()
class OPERATIONBREAKPOINT_API UOBOpenFireSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    static bool IsRequested();
    void EnableRuntimeInput();
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    UFUNCTION(BlueprintPure, Category="OB|OpenFire")
    bool IsConnected() const;
    UFUNCTION(BlueprintPure, Category="OB|OpenFire")
    bool GetAimPosition(FVector2D& Position) const;
    const FOBOpenFireState& GetState() const { return State; }
    FOBOpenFireInputReceived OnInputReceived;

private:
    void HandleMessage(const FOSCMessage& Message, const FString& Sender, uint16 Port);
    UPROPERTY(Transient)
    TObjectPtr<UOSCServer> Server;
    FOBOpenFireState State;
    bool bAllowDemo = false;
};

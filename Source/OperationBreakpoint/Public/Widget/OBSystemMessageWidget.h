#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "OBSystemMessageWidget.generated.h"

UCLASS(Abstract, Blueprintable)
class OPERATIONBREAKPOINT_API UOBSystemMessageWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "OB|UI|SystemMessage")
    void SetMessage(const FText& InMessage);

    UPROPERTY(BlueprintReadOnly, Category = "OB|UI|SystemMessage")
    FText Message;

protected:
    virtual void NativeConstruct() override;

    UFUNCTION(BlueprintImplementableEvent, Category = "OB|UI|SystemMessage")
    void OnMessageUpdated();
};

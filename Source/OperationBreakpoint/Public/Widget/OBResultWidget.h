#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "OBResultWidget.generated.h"

USTRUCT(BlueprintType)
struct OPERATIONBREAKPOINT_API FOBResultData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OB|UI|Result")
    bool bSuccess = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OB|UI|Result")
    int32 Score = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OB|UI|Result")
    int32 KillCount = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OB|UI|Result")
    float ElapsedSeconds = 0.0f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOBResultWidgetAction);

UCLASS(Abstract, Blueprintable)
class OPERATIONBREAKPOINT_API UOBResultWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "OB|UI|Result")
    void SetResultData(const FOBResultData& InResultData);

    UPROPERTY(BlueprintReadOnly, Category = "OB|UI|Result")
    FOBResultData ResultData;

    UPROPERTY(BlueprintAssignable, Category = "OB|UI|Result")
    FOBResultWidgetAction OnRetryRequested;

    UPROPERTY(BlueprintAssignable, Category = "OB|UI|Result")
    FOBResultWidgetAction OnTitleRequested;

    UFUNCTION(BlueprintCallable, Category = "OB|UI|Result")
    void RequestRetry();

    UFUNCTION(BlueprintCallable, Category = "OB|UI|Result")
    void RequestTitle();

protected:
    virtual void NativeConstruct() override;

    UFUNCTION(BlueprintImplementableEvent, Category = "OB|UI|Result")
    void OnResultUpdated();
};

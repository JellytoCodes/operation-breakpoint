#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Training/OBTrainingTypes.h"
#include "OBHUD.generated.h"

class UUserWidget;
class UOBTitleWidget;
class UOBPlayerOverlayWidget;
class UOBResultWidget;
class UOBTrainingResultWidget;

UENUM(BlueprintType)
enum class EOBInitialWidget : uint8
{
    None,
    Title,
    PlayerOverlay
};

/** Owns the local player's screen widgets. Gameplay decides when to change screens. */
UCLASS(Blueprintable)
class OPERATIONBREAKPOINT_API AOBHUD : public AHUD
{
    GENERATED_BODY()

public:
    AOBHUD();

    UFUNCTION(BlueprintCallable, Category = "OB|UI")
    bool ShowTitleWidget();

    UFUNCTION(BlueprintCallable, Category = "OB|UI")
    bool ShowPlayerOverlayWidget();

    /** Fallback preserves the ResultWidgetClass saved on existing player controllers. */
    UFUNCTION(BlueprintCallable, Category = "OB|UI")
    bool ShowResultWidget(TSubclassOf<UUserWidget> FallbackClass = nullptr);
    UFUNCTION(BlueprintCallable, Category = "OB|UI")
    bool ShowTrainingResult(const FOBTrainingResultData& Result);

    UFUNCTION(BlueprintCallable, Category = "OB|UI")
    void HideAllWidgets();

    UFUNCTION(BlueprintPure, Category = "OB|UI")
    bool IsMenuVisible() const;

    UFUNCTION(BlueprintPure, Category = "OB|UI")
    UOBTitleWidget* GetTitleWidget() const { return TitleWidget; }

    UFUNCTION(BlueprintPure, Category = "OB|UI")
    UOBPlayerOverlayWidget* GetPlayerOverlayWidget() const { return PlayerOverlayWidget; }

    /** Null for a legacy WBP_Result whose parent is still UUserWidget. */
    UFUNCTION(BlueprintPure, Category = "OB|UI")
    UOBResultWidget* GetResultWidget() const;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "OB|UI")
    EOBInitialWidget InitialWidget = EOBInitialWidget::PlayerOverlay;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "OB|UI|Classes")
    TSubclassOf<UOBTitleWidget> TitleWidgetClass;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "OB|UI|Classes")
    TSubclassOf<UOBPlayerOverlayWidget> PlayerOverlayWidgetClass;

    // UUserWidget also accepts the project's existing, unreparented WBP_Result.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "OB|UI|Classes")
    TSubclassOf<UUserWidget> ResultWidgetClass;

private:
    friend class FOBOpenFireInputModesTest;
    void HideScreenWidgets();
    void ApplyInputMode(UUserWidget* MenuWidget);

    UPROPERTY(Transient)
    TObjectPtr<UOBTitleWidget> TitleWidget;

    UPROPERTY(Transient)
    TObjectPtr<UOBPlayerOverlayWidget> PlayerOverlayWidget;

    UPROPERTY(Transient)
    TObjectPtr<UUserWidget> ResultWidget;

};

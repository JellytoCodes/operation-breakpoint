#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MediaPipe/OBBreachGuideTypes.h"
#include "OBTrainingStatusWidget.generated.h"
class AOBTrainingDirector;
class UTextBlock;
class UImage;
class UCanvasPanel;
class UOverlay;
class UProgressBar;
class UTexture2D;
class UOBRadialProgressBar;
struct FOBC4PlacementTracker;
UENUM(BlueprintType)
enum class EOBTrainingMarkerPreview : uint8 { Aim, Breach };
UCLASS(Blueprintable)
class OPERATIONBREAKPOINT_API UOBTrainingStatusWidget : public UUserWidget
{
    GENERATED_BODY()
  public:
    virtual void SynchronizeProperties() override;
#if WITH_EDITOR
    virtual void OnSelectedByDesigner() override;
    virtual void OnDeselectedByDesigner() override;
#endif
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Training|Designer") EOBTrainingMarkerPreview DesignerPreview=EOBTrainingMarkerPreview::Aim;
    // Native fallback also seeds the initial editable Designer hierarchy.
    void BuildDefaultLayout();
    void UpdateAimMarkers(FVector2D WidgetSize, FVector2D ViewportPixels, FVector2D TargetUV, float RadiusPixels, float Progress);
    void UpdateBreachMarkers(FVector2D WidgetSize, const FOBBreachGuideState& State, const FText& Message);
    void UpdateC4Markers(FVector2D WidgetSize,const FOBBreachGuideState& State,const FOBC4PlacementTracker& Placement,const FText& Message);
    void UpdateDesignerMarkers(FVector2D WidgetSize);
    void SetDirector(AOBTrainingDirector* InDirector);
    bool HasAimCompletionFeedback() const;
    UPROPERTY(EditDefaultsOnly,BlueprintReadWrite,Category="Training|Guide Style") TSoftObjectPtr<UTexture2D> AimIdleTexture=TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/Assets/UI/Training_UI/Aim/Aim_Red.Aim_Red")));
    UPROPERTY(EditDefaultsOnly,BlueprintReadWrite,Category="Training|Guide Style") TSoftObjectPtr<UTexture2D> AimSuccessTexture=TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/Assets/UI/Training_UI/Aim/Aim_Green.Aim_Green")));
    UPROPERTY(EditDefaultsOnly,BlueprintReadWrite,Category="Training|Guide Style",meta=(ClampMin="0.0",Units="s")) float AimCompletionFeedbackSeconds=.35f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Training|Guide Style") FLinearColor AimColor = FLinearColor::Red;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Training|Guide Style") FLinearColor ProgressColor = FLinearColor::Yellow;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Training|Guide Style") FLinearColor LeftHandColor = FLinearColor(0.1f, 0.7f, 1.f);
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Training|Guide Style") FLinearColor RightHandColor = FLinearColor(1.f, 0.55f, 0.05f);
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Training|Guide Style") FLinearColor SuccessColor = FLinearColor(0.1f, 1.f, 0.3f);
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Training|Guide Style", meta = (ClampMin = "1")) float GuideLineWidth = 3;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Training|Guide Style", meta = (ClampMin = "1")) float WristMarkerRadius = 10;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Training|Guide Style", meta = (ClampMin = "1")) int32 GuideLabelFontSize = 18;

  protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry &Geometry, float DeltaSeconds) override;

  private:
    UPROPERTY(Transient) TObjectPtr<AOBTrainingDirector> Director;
    UPROPERTY(Transient) TObjectPtr<UTexture2D> LoadedAimIdle;
    UPROPERTY(Transient) TObjectPtr<UTexture2D> LoadedAimSuccess;
    UPROPERTY(Transient, meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> BreachText;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UCanvasPanel> AimLayer;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UCanvasPanel> BreachLayer;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UImage> AimOutline;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UImage> AimTarget;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UImage> AimProgressOutline;
    // UWidget also accepts the legacy Image while the saved Blueprint is migrated.
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UWidget> AimProgress;
    UFUNCTION() void HandleAimCompleted(int32 Count,int32 Required);
    FVector2D CompletedAimUV=FVector2D(.5,.5);
    double AimCompletionUntil=0;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UImage> CameraFrame;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> CameraLabel;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UOverlay> LeftHandArea;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UOverlay> RightHandArea;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UImage> LeftHandTarget;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UImage> RightHandTarget;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UImage> LeftHandSuccess;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UImage> RightHandSuccess;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UOverlay> LeftWrist;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UOverlay> RightWrist;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UImage> LeftWristTarget;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UImage> RightWristTarget;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UImage> LeftWristSuccess;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UImage> RightWristSuccess;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> LeftHandLabel;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> RightHandLabel;
    UPROPERTY(Transient) FSlateBrush TrainingTargetBrush;
    UPROPERTY(Transient) FSlateBrush TrainingSuccessBrush;
    bool bUsingC4Brushes=false;
};

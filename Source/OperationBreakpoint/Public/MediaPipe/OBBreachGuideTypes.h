#pragma once
#include "CoreMinimal.h"
#include "OBBreachGuideTypes.generated.h"

USTRUCT(BlueprintType)
struct OPERATIONBREAKPOINT_API FOBBreachGuideState
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FVector2D CameraSize = FVector2D(640, 480);
    UPROPERTY(BlueprintReadOnly) FVector2D LeftMin = FVector2D(0.68125, 0.28125);
    UPROPERTY(BlueprintReadOnly) FVector2D LeftMax = FVector2D(0.74875, 0.36875);
    UPROPERTY(BlueprintReadOnly) FVector2D RightMin = FVector2D(0.25125, 0.28125);
    UPROPERTY(BlueprintReadOnly) FVector2D RightMax = FVector2D(0.31875, 0.36875);
    UPROPERTY(BlueprintReadOnly) FVector2D LeftWrist = FVector2D::ZeroVector;
    UPROPERTY(BlueprintReadOnly) FVector2D RightWrist = FVector2D::ZeroVector;
    UPROPERTY(BlueprintReadOnly) float LeftVisibility = 0;
    UPROPERTY(BlueprintReadOnly) float RightVisibility = 0;
    UPROPERTY(BlueprintReadOnly) float Progress = 0;
    UPROPERTY(BlueprintReadOnly) bool bFresh = false;
    UPROPERTY(BlueprintReadOnly) bool bMirrored = false;
    UPROPERTY(BlueprintReadOnly) bool bBreachMode = false;
    UPROPERTY(BlueprintReadOnly) bool bPlayerTracked = false;
    UPROPERTY(BlueprintReadOnly) bool bLeftInside = false;
    UPROPERTY(BlueprintReadOnly) bool bRightInside = false;
    UPROPERTY(BlueprintReadOnly) bool bReady = false;
};

// Coordinates are in widget-local Slate units so viewport resolution and DPI scale together.
struct OPERATIONBREAKPOINT_API FOBBreachGuideLayout
{
    static FBox2D CameraRect(const FVector2D& WidgetSize, const FVector2D& CameraSize);
    static FVector2D MapPoint(const FVector2D& UV, const FBox2D& Rect);
    static bool DecodePacket(const TArray<float>& Values, FOBBreachGuideState& Out);
};

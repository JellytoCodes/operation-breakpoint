#pragma once
#include "CoreMinimal.h"
#include "OBStageScore.generated.h"

USTRUCT(BlueprintType)
struct OPERATIONBREAKPOINT_API FOBStageZoneResult
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) int32 Zone=0;
    UPROPERTY(BlueprintReadOnly) int32 PlannedEnemies=0;
    UPROPERTY(BlueprintReadOnly) int32 PlayerKills=0;
    UPROPERTY(BlueprintReadOnly) int32 ShotsFired=0;
    UPROPERTY(BlueprintReadOnly) int32 EnemyHits=0;
    UPROPERTY(BlueprintReadOnly) int32 HostageHits=0;
    UPROPERTY(BlueprintReadOnly) int32 HeadHits=0;
    UPROPERTY(BlueprintReadOnly) int32 BodyHits=0;
    UPROPERTY(BlueprintReadOnly) int32 LimbHits=0;
    UPROPERTY(BlueprintReadOnly) int32 RemainingHearts=0;
    UPROPERTY(BlueprintReadOnly) double ElapsedSeconds=0;
    UPROPERTY(BlueprintReadOnly) double RemainingSeconds=0;
    UPROPERTY(BlueprintReadOnly) double KillAssessmentSum=0;
    UPROPERTY(BlueprintReadOnly) double KillScore=0;
    UPROPERTY(BlueprintReadOnly) double TimeScore=0;
    UPROPERTY(BlueprintReadOnly) double HeartScore=0;
    UPROPERTY(BlueprintReadOnly) double Score=0;
    UPROPERTY(BlueprintReadOnly) FString TimeGrade;
    UPROPERTY(BlueprintReadOnly) FString SupportReason;
    UPROPERTY(BlueprintReadOnly) FString FailureReason;
    UPROPERTY(BlueprintReadOnly) bool bStarted=false;
    UPROPERTY(BlueprintReadOnly) bool bFinalized=false;
    UPROPERTY(BlueprintReadOnly) bool bRescued=false;
};

struct OPERATIONBREAKPOINT_API FOBStageScoring
{
    static double HitWeight(FName HitZone);
    static void Evaluate(FOBStageZoneResult& Zone);
    static double Average(const TArray<FOBStageZoneResult>& Zones);
    static FString Rank(double Score);
};

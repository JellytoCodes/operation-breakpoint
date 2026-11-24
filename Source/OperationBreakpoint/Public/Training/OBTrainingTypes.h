#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Stage/OBStageScore.h"
#include "OBTrainingTypes.generated.h"

class USoundBase;

UENUM(BlueprintType)
enum class EOBTrainingDialogueCharacter : uint8 { System, Instructor };
UENUM(BlueprintType)
enum class EOBTrainingDialogueLocation : uint8 { Zone_A, Zone_B };
UENUM(BlueprintType)
enum class EOBTrainingDialogueLayer : uint8 { Main, Sub };
UENUM(BlueprintType)
enum class EOBTrainingDialogueEvent : uint8 { A_Guide, A_Training, B_Guide, B_Training };
UENUM(BlueprintType)
enum class EOBTrainingDialogueCondition : uint8 { None, Pass, Fail };

USTRUCT(BlueprintType)
struct OPERATIONBREAKPOINT_API FOBTrainingDialogueRow : public FTableRowBase
{
    GENERATED_BODY()
    // The DataTable RowName is the dialogue ID.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EOBTrainingDialogueCharacter Character = EOBTrainingDialogueCharacter::Instructor;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EOBTrainingDialogueLocation Location = EOBTrainingDialogueLocation::Zone_A;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EOBTrainingDialogueLayer Layer = EOBTrainingDialogueLayer::Sub;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bUIOpen = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FText Script;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EOBTrainingDialogueEvent Event = EOBTrainingDialogueEvent::A_Guide;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EOBTrainingDialogueCondition Condition = EOBTrainingDialogueCondition::None;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) int32 Prev_ID = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) float Delay_Time = 0;
    // Guide pages with zero duration stay open until their trigger advance.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) float Display_Time = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftObjectPtr<USoundBase> Voice;
#if WITH_EDITORONLY_DATA
    UPROPERTY(EditAnywhere) FString Note;
#endif
};

// Preserve the values used by existing Blueprint assets.
UENUM(BlueprintType)
enum class EOBTrainingStep : uint8
{
    Idle = 0,
    Intro = 1,
    Welcome = 2,
    WeaponCheck = 3,
    Reload = 4,
    Calibration = 5,
    Accuracy = 6,
    MovingToB = 7,
    TimedTargets = 8,
    MovingToC = 9,
    Breach = 10,
    Complete = 11,
    Error = 12,
    AimHold = 13,
    Cover = 14,
    Countdown = 15,
    Moving = 16
};

UENUM(BlueprintType)
enum class EOBTrainingZone : uint8
{
    A,
    B,
    C
};

UENUM(BlueprintType)
enum class EOBTrainingOutcome : uint8
{
    NotRun,
    Passed,
    Failed,
    Aborted
};

UENUM(BlueprintType)
enum class EOBTrainingReportStatus : uint8
{
    NotRequested,
    Exporting,
    Ready,
    Failed
};

USTRUCT(BlueprintType)
struct OPERATIONBREAKPOINT_API FOBTrainingZoneConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OB|Training")
    EOBTrainingZone Zone = EOBTrainingZone::A;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OB|Training", meta = (ClampMin = "0"))
    int32 RailPointIndex = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OB|Training")
    TArray<EOBTrainingStep> Steps;
};

USTRUCT(BlueprintType)
struct OPERATIONBREAKPOINT_API FOBTrainingStepResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "OB|Training")
    EOBTrainingStep Step = EOBTrainingStep::Idle;
    UPROPERTY(BlueprintReadOnly, Category = "OB|Training")
    EOBTrainingOutcome Outcome = EOBTrainingOutcome::NotRun;
    UPROPERTY(BlueprintReadOnly, Category = "OB|Training")
    int32 Attempts = 0;
    UPROPERTY(BlueprintReadOnly, Category = "OB|Training")
    float ElapsedSeconds = 0.0f;
};

USTRUCT(BlueprintType)
struct OPERATIONBREAKPOINT_API FOBTrainingResultData
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "OB|Training")
    FGuid SessionId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OB|Training")
    FString LearnerName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OB|Training")
    FString LearnerId;
    UPROPERTY(BlueprintReadOnly, Category = "OB|Training")
    FDateTime StartedAtUtc;
    UPROPERTY(BlueprintReadOnly, Category = "OB|Training")
    FDateTime CompletedAtUtc;
    UPROPERTY(BlueprintReadOnly, Category = "OB|Training")
    bool bFinished = false;
    UPROPERTY(BlueprintReadOnly, Category = "OB|Training")
    bool bPassed = false;
    UPROPERTY(BlueprintReadOnly, Category = "OB|Training")
    bool bAborted = false;
    UPROPERTY(BlueprintReadOnly, Category = "OB|Training")
    bool bUsedTestInputs = false;
    // A route prototype can complete without qualifying for a certificate.
    UPROPERTY(BlueprintReadOnly, Category = "OB|Training")
    bool bPrototypeFlow = false;
    UPROPERTY(BlueprintReadOnly, Category = "OB|Training")
    bool bScenarioCompleted = false;
    UPROPERTY(BlueprintReadOnly, Category = "OB|Training")
    FString AbortReason;
    UPROPERTY(BlueprintReadOnly, Category = "OB|Training")
    float TotalSeconds = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "OB|Training")
    TArray<FOBTrainingStepResult> Steps;
    UPROPERTY(BlueprintReadOnly, Category = "OB|Training")
    int32 ShotsFired = 0;
    UPROPERTY(BlueprintReadOnly, Category = "OB|Training")
    int32 ValidHits = 0;
    UPROPERTY(BlueprintReadOnly, Category="OB|Stage|Result") bool bHasStageScore=false;
    UPROPERTY(BlueprintReadOnly, Category="OB|Stage|Result") double StageScore=0;
    UPROPERTY(BlueprintReadOnly, Category="OB|Stage|Result") FString StageRank;
    UPROPERTY(BlueprintReadOnly, Category="OB|Stage|Result") TArray<FOBStageZoneResult> StageZones;
    UPROPERTY(BlueprintReadOnly, Category = "OB|Training")
    int32 UniqueTargetsHit = 0;
    UPROPERTY(BlueprintReadOnly, Category = "OB|Training")
    int32 TotalTargets = 0;
    UPROPERTY(BlueprintReadOnly, Category = "OB|Training")
    float RecordedFireSeconds = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "OB|Training")
    int32 PassingTargetCount = 6;
    UPROPERTY(BlueprintReadOnly, Category = "OB|Training")
    float RecordedFireLimitSeconds = 30.0f;

    bool HasPassed(EOBTrainingStep Step) const
    {
        const FOBTrainingStepResult *Found =
            Steps.FindByPredicate([Step](const FOBTrainingStepResult &Item) { return Item.Step == Step; });
        return Found && Found->Outcome == EOBTrainingOutcome::Passed;
    }

    bool MeetsCompletionRequirements() const
    {
        return bFinished && !bAborted && !bUsedTestInputs && PassingTargetCount > 0 && Steps.Num() == 9 &&
               FMath::IsFinite(TotalSeconds) && TotalSeconds >= 0 && TotalTargets >= PassingTargetCount &&
               UniqueTargetsHit >= PassingTargetCount && UniqueTargetsHit <= TotalTargets &&
               ValidHits >= UniqueTargetsHit && ShotsFired >= ValidHits && FMath::IsFinite(RecordedFireSeconds) &&
               RecordedFireLimitSeconds > 0 && RecordedFireSeconds >= RecordedFireLimitSeconds - 0.05f &&
               HasPassed(EOBTrainingStep::WeaponCheck) && HasPassed(EOBTrainingStep::AimHold) &&
               HasPassed(EOBTrainingStep::Cover) && HasPassed(EOBTrainingStep::Countdown) &&
               HasPassed(EOBTrainingStep::Reload) && HasPassed(EOBTrainingStep::Calibration) &&
               HasPassed(EOBTrainingStep::Accuracy) && HasPassed(EOBTrainingStep::TimedTargets) &&
               HasPassed(EOBTrainingStep::Breach) && !Steps.ContainsByPredicate([](const FOBTrainingStepResult &Item) {
                   return Item.Outcome != EOBTrainingOutcome::Passed || Item.Attempts < 1 ||
                          !FMath::IsFinite(Item.ElapsedSeconds) || Item.ElapsedSeconds < 0;
               });
    }

    // The course result shown to the trainee is independent of certificate eligibility.
    bool IsScenarioPassed() const
    {
        if (bAborted) return false;
        if (bPrototypeFlow)
            return bScenarioCompleted && FMath::IsFinite(RecordedFireSeconds) && RecordedFireSeconds >= 0 &&
                FMath::IsFinite(RecordedFireLimitSeconds) && RecordedFireLimitSeconds > 0 &&
                RecordedFireSeconds <= RecordedFireLimitSeconds;
        return HasPassed(EOBTrainingStep::TimedTargets) && HasPassed(EOBTrainingStep::Breach);
    }
};

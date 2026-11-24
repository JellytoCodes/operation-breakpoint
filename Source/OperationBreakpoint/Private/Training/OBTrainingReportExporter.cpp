#include "Training/OBTrainingReportExporter.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

namespace
{
FString StepLabel(EOBTrainingStep Step)
{
    switch (Step)
    {
    case EOBTrainingStep::WeaponCheck:
        return TEXT("총기 확인");
    case EOBTrainingStep::AimHold:
        return TEXT("조준 유지");
    case EOBTrainingStep::Reload:
        return TEXT("재장전");
    case EOBTrainingStep::Calibration:
        return TEXT("센서 보정");
    case EOBTrainingStep::Accuracy:
        return TEXT("영점 사격");
    case EOBTrainingStep::Cover:
        return TEXT("엄폐");
    case EOBTrainingStep::Countdown:
        return TEXT("사격 준비");
    case EOBTrainingStep::TimedTargets:
        return TEXT("기록사격");
    case EOBTrainingStep::Breach:
        return TEXT("브리칭");
    default:
        return TEXT("기타");
    }
}

FString OutcomeLabel(EOBTrainingOutcome Outcome)
{
    switch (Outcome)
    {
    case EOBTrainingOutcome::Passed:
        return TEXT("합격");
    case EOBTrainingOutcome::Failed:
        return TEXT("불합격");
    case EOBTrainingOutcome::Aborted:
        return TEXT("중단");
    default:
        return TEXT("미진행");
    }
}

FString BuildJson(const FOBTrainingResultData &Result)
{
    const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("schema_version"), 1);
    Root->SetStringField(TEXT("session_id"), Result.SessionId.ToString(EGuidFormats::DigitsWithHyphens));
    Root->SetStringField(TEXT("learner_name"), Result.LearnerName);
    Root->SetStringField(TEXT("learner_id"), Result.LearnerId);
    Root->SetStringField(TEXT("completed_at_utc"), Result.CompletedAtUtc.ToIso8601());
    Root->SetBoolField(TEXT("passed"), Result.bPassed);
    Root->SetBoolField(TEXT("finished"), Result.bFinished);
    Root->SetBoolField(TEXT("aborted"), Result.bAborted);
    Root->SetBoolField(TEXT("test_inputs"), Result.bUsedTestInputs);
    Root->SetNumberField(TEXT("total_seconds"), Result.TotalSeconds);
    Root->SetNumberField(TEXT("shots_fired"), Result.ShotsFired);
    Root->SetNumberField(TEXT("valid_hits"), Result.ValidHits);
    Root->SetNumberField(TEXT("unique_targets_hit"), Result.UniqueTargetsHit);
    Root->SetNumberField(TEXT("total_targets"), Result.TotalTargets);
    Root->SetNumberField(TEXT("recorded_fire_seconds"), Result.RecordedFireSeconds);
    Root->SetNumberField(TEXT("passing_target_count"), Result.PassingTargetCount);
    Root->SetNumberField(TEXT("recorded_fire_limit_seconds"), Result.RecordedFireLimitSeconds);
    TArray<TSharedPtr<FJsonValue>> Steps;
    for (const FOBTrainingStepResult &Step : Result.Steps)
    {
        const TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("name"), StepLabel(Step.Step));
        Item->SetStringField(TEXT("code"),
                             StaticEnum<EOBTrainingStep>()->GetNameStringByValue(static_cast<int64>(Step.Step)));
        Item->SetStringField(TEXT("outcome"), OutcomeLabel(Step.Outcome));
        Item->SetNumberField(TEXT("attempts"), Step.Attempts);
        Item->SetNumberField(TEXT("elapsed_seconds"), Step.ElapsedSeconds);
        Steps.Add(MakeShared<FJsonValueObject>(Item));
    }
    Root->SetArrayField(TEXT("steps"), Steps);
    FString Json;
    FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Json));
    return Json;
}
} // namespace

bool UOBTrainingReportExporter::RequestPdfExport(const FOBTrainingResultData &Result)
{
    if (!Result.SessionId.IsValid() || !Result.bPassed || !Result.MeetsCompletionRequirements())
        return false;
    if (GetStatus(Result.SessionId) == EOBTrainingReportStatus::Exporting)
        return false;
    const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("TrainingReports"));
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString Base = Directory / Result.SessionId.ToString(EGuidFormats::Digits);
    const FString Input = Base + TEXT(".json");
    const FString Output = Base + TEXT(".pdf");
    FString Executable = GeneratorExecutable;
    FString Arguments;
    if (Executable.IsEmpty())
        Executable = FPaths::ProjectDir() / TEXT("Binaries/Win64/TrainingReportGenerator.exe");
    if (FPaths::IsRelative(Executable))
        Executable = FPaths::ProjectDir() / Executable;
#if !UE_BUILD_SHIPPING
    // A project-local development venv; never depend on a Codex installation at runtime.
    if (!FPaths::FileExists(Executable) && GeneratorExecutable.IsEmpty())
    {
        Executable = FPaths::ProjectDir() / TEXT("Tools/TrainingReport/.venv/Scripts/python.exe");
        Arguments = FString::Printf(
            TEXT("\"%s\" "),
            *FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Tools/TrainingReport/training_report.py")));
    }
#endif
    Executable = FPaths::ConvertRelativePathToFull(Executable);
    auto Fail = [&](const FString &Reason) {
        Statuses.Add(Result.SessionId, EOBTrainingReportStatus::Failed);
        Errors.Add(Result.SessionId, Reason);
        OnReportExportFinished.Broadcast(Result.SessionId, false, Reason);
        return false;
    };
    if (!FPaths::FileExists(Executable))
        return Fail(TEXT("PDF 생성기가 없습니다. Tools/TrainingReport/build_report_generator.ps1을 실행하세요."));
    if (!FFileHelper::SaveStringToFile(BuildJson(Result), *Input, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        return Fail(TEXT("PDF 입력 데이터를 저장하지 못했습니다."));
    Arguments += FString::Printf(TEXT("--input \"%s\" --output \"%s\""), *Input, *Output);
    FExportJob Job;
    Job.SessionId = Result.SessionId;
    Job.InputPath = Input;
    Job.OutputPath = Output;
    Job.StartedAt = FPlatformTime::Seconds();
    Job.Process =
        FPlatformProcess::CreateProc(*Executable, *Arguments, false, true, true, nullptr, 0, nullptr, nullptr);
    if (!Job.Process.IsValid())
        return Fail(TEXT("PDF 생성기를 시작하지 못했습니다."));
    Jobs.Add(Job);
    Statuses.Add(Result.SessionId, EOBTrainingReportStatus::Exporting);
    Errors.Remove(Result.SessionId);
    if (!PollHandle.IsValid())
        PollHandle =
            FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::PollExports), 0.1f);
    return true;
}

bool UOBTrainingReportExporter::PollExports(float)
{
    for (int32 Index = Jobs.Num() - 1; Index >= 0; --Index)
    {
        FExportJob &Job = Jobs[Index];
        if (FPlatformTime::Seconds() - Job.StartedAt > FMath::Max(1.0f, ExportTimeoutSeconds))
        {
            FPlatformProcess::TerminateProc(Job.Process, true);
            FinishJob(Index, false,
                      TEXT("PDF 생성 제한시간을 초과했습니다. 결과는 보관되어 있으며 재출력할 수 있습니다."));
        }
        else if (!FPlatformProcess::IsProcRunning(Job.Process))
        {
            int32 ExitCode = -1;
            FPlatformProcess::GetProcReturnCode(Job.Process, &ExitCode);
            const bool bSuccess = ExitCode == 0 && IFileManager::Get().FileSize(*Job.OutputPath) > 0;
            FinishJob(Index, bSuccess, TEXT("PDF 생성에 실패했습니다. 생성기·글꼴과 저장 경로를 확인하세요."));
        }
    }
    const bool bContinue = !Jobs.IsEmpty();
    if (!bContinue)
        PollHandle.Reset();
    return bContinue;
}

void UOBTrainingReportExporter::FinishJob(int32 Index, bool bSuccess, const FString &Error)
{
    const FGuid Id = Jobs[Index].SessionId;
    const FString Path = Jobs[Index].OutputPath;
    FPlatformProcess::CloseProc(Jobs[Index].Process);
    Jobs.RemoveAt(Index);
    Statuses.Add(Id, bSuccess ? EOBTrainingReportStatus::Ready : EOBTrainingReportStatus::Failed);
    if (bSuccess)
    {
        Paths.Add(Id, Path);
        Errors.Remove(Id);
    }
    else
        Errors.Add(Id, Error);
    OnReportExportFinished.Broadcast(Id, bSuccess, bSuccess ? Path : Error);
}

void UOBTrainingReportExporter::Shutdown()
{
    if (PollHandle.IsValid())
        FTSTicker::GetCoreTicker().RemoveTicker(PollHandle);
    PollHandle.Reset();
    for (FExportJob &Job : Jobs)
    {
        FPlatformProcess::TerminateProc(Job.Process, true);
        FPlatformProcess::CloseProc(Job.Process);
    }
    Jobs.Reset();
}

EOBTrainingReportStatus UOBTrainingReportExporter::GetStatus(FGuid Id) const
{
    const auto *Found = Statuses.Find(Id);
    return Found ? *Found : EOBTrainingReportStatus::NotRequested;
}
FString UOBTrainingReportExporter::GetReportPath(FGuid Id) const
{
    return Paths.FindRef(Id);
}
FString UOBTrainingReportExporter::GetLastError(FGuid Id) const
{
    return Errors.FindRef(Id);
}

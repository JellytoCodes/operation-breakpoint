#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Training/OBTrainingReportExporter.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBTrainingReportExportTest, "OperationBreakpoint.Training.ReportExport",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOBTrainingReportExportTest::RunTest(const FString &)
{
    auto *Exporter = NewObject<UOBTrainingReportExporter>();
    FOBTrainingResultData Result;
    Result.SessionId = FGuid::NewGuid();
    Result.bFinished = true;
    Result.StartedAtUtc = Result.CompletedAtUtc = FDateTime::UtcNow();
    Result.LearnerName = TEXT("자동 테스트");
    Result.TotalTargets = 10;
    Result.ShotsFired = Result.ValidHits = Result.UniqueTargetsHit = 6;
    Result.RecordedFireSeconds = Result.TotalSeconds = 30;
    const TArray<EOBTrainingStep> Required = {
        EOBTrainingStep::WeaponCheck, EOBTrainingStep::AimHold,      EOBTrainingStep::Reload,
        EOBTrainingStep::Calibration, EOBTrainingStep::Accuracy,     EOBTrainingStep::Cover,
        EOBTrainingStep::Countdown,   EOBTrainingStep::TimedTargets, EOBTrainingStep::Breach};
    for (auto Step : Required)
    {
        FOBTrainingStepResult Record;
        Record.Step = Step;
        Record.Outcome = EOBTrainingOutcome::Passed;
        Record.Attempts = 1;
        Result.Steps.Add(Record);
    }
    Result.bPassed = Result.MeetsCompletionRequirements();
    const FString Base =
        FPaths::ProjectSavedDir() / TEXT("TrainingReports") / Result.SessionId.ToString(EGuidFormats::Digits);
    ON_SCOPE_EXIT
    {
        Exporter->Shutdown();
        IFileManager::Get().Delete(*(Base + TEXT(".json")));
        IFileManager::Get().Delete(*(Base + TEXT(".pdf")));
    };
    FOBTrainingResultData Failed = Result;
    Failed.bPassed = false;
    TestFalse(TEXT("Unqualified session cannot launch exporter"), Exporter->RequestPdfExport(Failed));
    TestTrue(TEXT("Qualified export starts external generator"), Exporter->RequestPdfExport(Result));
    TestFalse(TEXT("Duplicate request while exporting is rejected"), Exporter->RequestPdfExport(Result));
    auto Wait = [&]() {
        const double Started = FPlatformTime::Seconds();
        while (Exporter->GetStatus(Result.SessionId) == EOBTrainingReportStatus::Exporting &&
               FPlatformTime::Seconds() - Started < 20)
        {
            FPlatformProcess::Sleep(0.02f);
            const auto ActiveHandle = Exporter->PollHandle;
            if (!Exporter->PollExports(0.02f) && ActiveHandle.IsValid())
                FTSTicker::GetCoreTicker().RemoveTicker(ActiveHandle);
        }
        return Exporter->GetStatus(Result.SessionId) == EOBTrainingReportStatus::Ready;
    };
    TestTrue(TEXT("UE JSON to executable to PDF completes"), Wait());
    const FString FirstPath = Exporter->GetReportPath(Result.SessionId);
    TestTrue(TEXT("PDF exists"), IFileManager::Get().FileSize(*FirstPath) > 1000);
    TestTrue(TEXT("Re-export same session starts"), Exporter->RequestPdfExport(Result));
    TestTrue(TEXT("Re-export completes"), Wait());
    TestEqual(TEXT("Re-export preserves record/path identity"), Exporter->GetReportPath(Result.SessionId), FirstPath);
    Exporter->GeneratorExecutable = FPaths::ProjectSavedDir() / TEXT("Tests/AbsentReportGenerator.exe");
    TestFalse(TEXT("Missing executable reports failure"), Exporter->RequestPdfExport(Result));
    TestEqual(TEXT("Failure status exposed to UI"), Exporter->GetStatus(Result.SessionId),
              EOBTrainingReportStatus::Failed);
    TestTrue(TEXT("Export failure preserves qualification"), Result.bPassed);
    return true;
}
#endif

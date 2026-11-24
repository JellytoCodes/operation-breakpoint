#include "Training/OBTrainingProgressSubsystem.h"
#include "Training/OBTrainingReportExporter.h"
#include "HAL/FileManager.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogOBTrainingProgress, Log, All);

void UOBTrainingProgressSubsystem::Initialize(FSubsystemCollectionBase &Collection)
{
    Super::Initialize(Collection);
    ReportExporter = NewObject<UOBTrainingReportExporter>(this);
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("TrainingRecords");
    TArray<FString> Files;
    IFileManager::Get().FindFiles(Files, *(Directory / TEXT("*.json")), true, false);
    for (const FString &File : Files)
    {
        FString Json;
        FOBTrainingResultData Result;
        const FString Path = Directory / File;
        if (IFileManager::Get().FileSize(*Path) > 1024 * 1024 || !FFileHelper::LoadFileToString(Json, *Path) ||
            !FJsonObjectConverter::JsonObjectStringToUStruct(Json, &Result) || !Result.SessionId.IsValid() ||
            !Result.bFinished || Result.SessionId.ToString(EGuidFormats::Digits) != FPaths::GetBaseFilename(File))
            continue;
        Result.bPassed = Result.MeetsCompletionRequirements();
        const FOBTrainingResultData *Latest = Results.Find(LatestSessionId);
        if (!Latest || Result.CompletedAtUtc > Latest->CompletedAtUtc)
            LatestSessionId = Result.SessionId;
        Results.Add(Result.SessionId, Result);
        PersistedSessions.Add(Result.SessionId);
    }
}

void UOBTrainingProgressSubsystem::Deinitialize()
{
    if (ReportExporter)
        ReportExporter->Shutdown();
    Super::Deinitialize();
}

bool UOBTrainingProgressSubsystem::CommitTrainingResult(const FOBTrainingResultData &Input)
{
    if (!Input.SessionId.IsValid() || !Input.bFinished)
        return false;
    if (PersistedSessions.Contains(Input.SessionId))
        return true;
    const FOBTrainingResultData *Existing = Results.Find(Input.SessionId);
    const bool bNew = Existing == nullptr;
    // Retry saving the same snapshot after disk failure, never the caller's edited result.
    FOBTrainingResultData Result = Existing ? *Existing : Input;
    Result.bPassed = Result.MeetsCompletionRequirements();
    FString Json;
    if (!FJsonObjectConverter::UStructToJsonObjectString(Result, Json))
        return false;
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("TrainingRecords");
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString Path = Directory / (Result.SessionId.ToString(EGuidFormats::Digits) + TEXT(".json"));
    const FString Temporary = Path + TEXT(".tmp");
    const bool bSaved =
        FFileHelper::SaveStringToFile(Json, *Temporary, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) &&
        IFileManager::Get().Move(*Path, *Temporary, true, true);
    Results.Add(Result.SessionId, Result);
    LatestSessionId = Result.SessionId;
    if (bSaved)
        PersistedSessions.Add(Result.SessionId);
    if (bNew)
        OnTrainingResultStored.Broadcast(Result);
    if (!bSaved)
        UE_LOG(LogOBTrainingProgress, Error,
               TEXT("Training result remains available in this session but could not be saved: %s"), *Path);
    return bSaved;
}

bool UOBTrainingProgressSubsystem::HasCompletedTraining() const
{
    for (const auto &Entry : Results)
        if (Entry.Value.bPassed)
            return true;
    return false;
}

bool UOBTrainingProgressSubsystem::GetTrainingResult(FGuid Id, FOBTrainingResultData &Result) const
{
    if (const auto *Found = Results.Find(Id))
    {
        Result = *Found;
        return true;
    }
    return false;
}

bool UOBTrainingProgressSubsystem::GetLatestTrainingResult(FOBTrainingResultData &Result) const
{
    return GetTrainingResult(LatestSessionId, Result);
}

bool UOBTrainingProgressSubsystem::RequestPdfExport(FGuid Id)
{
    const auto *Result = Results.Find(Id);
    if (!Result || !ReportExporter)
        return false;
    const FOBTrainingResultData Snapshot = *Result;
    return CommitTrainingResult(Snapshot) && ReportExporter->RequestPdfExport(Snapshot);
}

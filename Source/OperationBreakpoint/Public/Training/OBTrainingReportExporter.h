#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "HAL/PlatformProcess.h"
#include "UObject/Object.h"
#include "Training/OBTrainingTypes.h"
#include "OBTrainingReportExporter.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOBTrainingReportFinished, FGuid, SessionId, bool, bSuccess, FString,
                                               PathOrError);

/** Owned by the progress subsystem so map travel does not cancel an export. */
UCLASS(Config = Game)
class OPERATIONBREAKPOINT_API UOBTrainingReportExporter : public UObject
{
    GENERATED_BODY()
  public:
    bool RequestPdfExport(const FOBTrainingResultData &Result);
    void Shutdown();

    UPROPERTY(BlueprintAssignable, Category = "OB|Training|Report")
    FOBTrainingReportFinished OnReportExportFinished;
    UPROPERTY(Config, EditAnywhere, Category = "OB|Training|Report")
    FString GeneratorExecutable;
    UPROPERTY(Config, EditAnywhere, Category = "OB|Training|Report")
    float ExportTimeoutSeconds = 60.0f;

    EOBTrainingReportStatus GetStatus(FGuid SessionId) const;
    FString GetReportPath(FGuid SessionId) const;
    FString GetLastError(FGuid SessionId) const;

  private:
    friend class FOBTrainingReportExportTest;
    struct FExportJob
    {
        FGuid SessionId;
        FProcHandle Process;
        FString InputPath, OutputPath;
        double StartedAt = 0.0;
    };
    bool PollExports(float DeltaSeconds);
    void FinishJob(int32 Index, bool bSuccess, const FString &Error);
    TArray<FExportJob> Jobs;
    TMap<FGuid, EOBTrainingReportStatus> Statuses;
    TMap<FGuid, FString> Paths, Errors;
    FTSTicker::FDelegateHandle PollHandle;
};

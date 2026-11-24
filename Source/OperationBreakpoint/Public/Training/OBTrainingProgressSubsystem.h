#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Training/OBTrainingTypes.h"
#include "OBTrainingProgressSubsystem.generated.h"

class UOBTrainingReportExporter;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOBTrainingResultStored, FOBTrainingResultData, Result);

UCLASS()
class OPERATIONBREAKPOINT_API UOBTrainingProgressSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
  public:
    virtual void Initialize(FSubsystemCollectionBase &Collection) override;
    virtual void Deinitialize() override;

    UFUNCTION(BlueprintCallable, Category = "OB|Training")
    bool CommitTrainingResult(const FOBTrainingResultData &Result);
    UFUNCTION(BlueprintPure, Category = "OB|Training")
    bool HasCompletedTraining() const;
    UFUNCTION(BlueprintPure, Category = "OB|Training")
    bool GetTrainingResult(FGuid SessionId, FOBTrainingResultData &Result) const;
    UFUNCTION(BlueprintPure, Category = "OB|Training")
    bool GetLatestTrainingResult(FOBTrainingResultData &Result) const;
    UFUNCTION(BlueprintCallable, Category = "OB|Training|Report")
    bool RequestPdfExport(FGuid SessionId);
    UFUNCTION(BlueprintPure, Category = "OB|Training")
    bool IsResultSaved(FGuid SessionId) const
    {
        return PersistedSessions.Contains(SessionId);
    }
    UFUNCTION(BlueprintPure, Category = "OB|Training|Report")
    UOBTrainingReportExporter *GetReportExporter() const
    {
        return ReportExporter;
    }

    UPROPERTY(BlueprintAssignable, Category = "OB|Training")
    FOBTrainingResultStored OnTrainingResultStored;

  private:
    UPROPERTY(Transient)
    TMap<FGuid, FOBTrainingResultData> Results;
    UPROPERTY(Transient)
    TObjectPtr<UOBTrainingReportExporter> ReportExporter;
    FGuid LatestSessionId;
    TSet<FGuid> PersistedSessions;
};

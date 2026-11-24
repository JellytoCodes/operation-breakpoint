#pragma once
#include "CoreMinimal.h"
#include "Tasks/StateTreeAITask.h"
#include "OBStateTreeZone6GuardTask.generated.h"

USTRUCT()
struct OPERATIONBREAKPOINT_API FOBStateTreeZone6GuardTaskData
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,Category=Context) TObjectPtr<AActor> Actor;
};
USTRUCT(meta=(DisplayName="OB Zone6 Guard Hostage",Category="OB|AI|Zone6"))
struct OPERATIONBREAKPOINT_API FOBStateTreeZone6GuardTask : public FStateTreeAITaskBase
{
    GENERATED_BODY()
    using FInstanceDataType=FOBStateTreeZone6GuardTaskData;
    virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
    virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,const FStateTreeTransitionResult& Transition) const override;
    virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context,float DeltaTime) const override;
};

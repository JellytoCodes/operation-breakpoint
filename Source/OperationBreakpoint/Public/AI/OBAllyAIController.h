#pragma once
#include "CoreMinimal.h"
#include "AIController.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "OBAllyAIController.generated.h"

/** Dense ship decks need a larger search than the engine's default 2048 nodes. */
UCLASS()
class OPERATIONBREAKPOINT_API UOBAllyNavigationFilter : public UNavigationQueryFilter
{
    GENERATED_BODY()
protected:
    virtual void InitializeFilter(const ANavigationData& NavData,const UObject* Querier,FNavigationQueryFilter& Filter) const override;
};

UCLASS()
class OPERATIONBREAKPOINT_API AOBAllyAIController : public AAIController
{
    GENERATED_BODY()
public:
    AOBAllyAIController(const FObjectInitializer& ObjectInitializer);
};

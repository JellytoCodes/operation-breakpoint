#include "AI/OBAllyAIController.h"

void UOBAllyNavigationFilter::InitializeFilter(const ANavigationData& NavData,const UObject* Querier,FNavigationQueryFilter& Filter) const
{
    Super::InitializeFilter(NavData,Querier,Filter);
    Filter.SetMaxSearchNodes(16384);
}

AOBAllyAIController::AOBAllyAIController(const FObjectInitializer& Initializer)
    : Super(Initializer)
{
    bWantsPlayerState=false;
}

#include "Widget/OBResultWidget.h"

void UOBResultWidget::NativeConstruct()
{
    Super::NativeConstruct();
    OnResultUpdated();
}

void UOBResultWidget::SetResultData(const FOBResultData& InResultData)
{
    ResultData = InResultData;
    OnResultUpdated();
}

void UOBResultWidget::RequestRetry()
{
    OnRetryRequested.Broadcast();
}

void UOBResultWidget::RequestTitle()
{
    OnTitleRequested.Broadcast();
}

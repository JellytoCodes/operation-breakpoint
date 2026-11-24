#include "Widget/OBSystemMessageWidget.h"

void UOBSystemMessageWidget::NativeConstruct()
{
    Super::NativeConstruct();
    OnMessageUpdated();
}

void UOBSystemMessageWidget::SetMessage(const FText& InMessage)
{
    Message = InMessage;
    OnMessageUpdated();
}

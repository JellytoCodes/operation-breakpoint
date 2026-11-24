#pragma once
#include "CoreMinimal.h"
#include "Engine/GameViewportClient.h"
#include "OBGameViewportClient.generated.h"

// The operator reset also works while title/cinematic UI owns input focus.
UCLASS()
class OPERATIONBREAKPOINT_API UOBGameViewportClient : public UGameViewportClient
{
    GENERATED_BODY()
public:
    virtual bool InputKey(const FInputKeyEventArgs& Event) override;
    virtual TOptional<TSharedRef<SWidget>> MapCursor(FViewport* InViewport, const FCursorReply& Reply) override;
private:
    TSharedPtr<SWidget> Crosshair;
};

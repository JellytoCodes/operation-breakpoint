#pragma once
#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "OBTrainingUIData.generated.h"
class UTexture2D;

// Separate from narration: 3001..3007 guide pages, 3008..3010 warning/pass/fail.
USTRUCT(BlueprintType)
struct OPERATIONBREAKPOINT_API FOBTrainingUIRow : public FTableRowBase
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(MultiLine="true")) FText Title;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(MultiLine="true")) FText Body;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftObjectPtr<UTexture2D> Image;
};

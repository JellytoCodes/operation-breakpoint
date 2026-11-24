#pragma once
#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "OBStageScript.generated.h"
class USoundBase;

// RowName is the spreadsheet ID (2001...). Original enum labels are preserved as names.
USTRUCT(BlueprintType)
struct OPERATIONBREAKPOINT_API FOBStageScriptRow : public FTableRowBase
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FName Character;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FText SpeakerName;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FName Location;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FName Layer;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FText Text;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FName Point;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FName Condition;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 Prev_ID=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0")) float Delay_Time=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0")) float Display_Time=1.2f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TSoftObjectPtr<USoundBase> Sound;
    // Opening subtitles use the single briefing sound's playback clock.
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",Units="s")) float OpeningStartSeconds=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",Units="s")) float OpeningEndSeconds=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FString Note;
};

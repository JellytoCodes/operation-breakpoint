#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "OBEnemyAimEditorLibrary.generated.h"

class UAnimBlueprint;
class UAnimationAsset;
class UAnimMontage;
class UAnimSequenceBase;

/** Scoped editor operations for the existing enemy graphs; no map or runtime state changes. */
UCLASS()
class OPERATIONBREAKPOINT_API UOBEnemyAimEditorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="OB|Enemy|Animation|Editor")
    static FString DescribeEnemyAnimation(UAnimBlueprint* Blueprint);

    /** Returns replaced graph-node references, or -1 for invalid input/compilation failure. */
    UFUNCTION(BlueprintCallable, Category="OB|Enemy|Animation|Editor")
    static int32 ReplaceGraphAnimations(UAnimBlueprint* Blueprint,
        const TArray<UAnimationAsset*>& Sources, const TArray<UAnimationAsset*>& Replacements);

    /** Replaces clips in place while retaining slot/section/notify wiring and montage timing. */
    UFUNCTION(BlueprintCallable, Category="OB|Enemy|Animation|Editor")
    static bool ReplaceMontageAnimation(UAnimMontage* Montage, UAnimSequenceBase* Source, UAnimSequenceBase* Replacement);
};

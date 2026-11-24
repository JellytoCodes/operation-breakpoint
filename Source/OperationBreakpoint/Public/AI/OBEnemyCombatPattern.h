#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "OBEnemyCombatPattern.generated.h"

class UAnimSequence;
class UStateTree;

UENUM(BlueprintType)
enum class EOBEnemyCombatPattern : uint8
{
    Automatic UMETA(DisplayName="Automatic (archetype / cover settings)"),
    Crouch,
    Prone,
    Stand,
    WindowCover,
    RPG
};

UENUM(BlueprintType)
enum class EOBEnemyPosture : uint8 { Stand, Crouch, Prone };

UENUM(BlueprintType)
enum class EOBEnemyPatternAction : uint8 { Posture, Aim, Fire, Reload, CheckAlive };

/** Shared, cooked references. Each tree remains independently editable in the StateTree editor. */
UCLASS(BlueprintType)
class OPERATIONBREAKPOINT_API UOBEnemyCombatPatternSet : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Category="StateTrees") TMap<EOBEnemyCombatPattern,TObjectPtr<UStateTree>> Trees;
    UPROPERTY(EditAnywhere, Category="Animations") TObjectPtr<UAnimSequence> StandAim;
    UPROPERTY(EditAnywhere, Category="Animations") TObjectPtr<UAnimSequence> CrouchAim;
    // A stable prone frame; shot/reload arm motion is layered over this lower-body posture.
    UPROPERTY(EditAnywhere, Category="Animations") TObjectPtr<UAnimSequence> ProneAim;
    UPROPERTY(EditAnywhere, Category="Animations") TObjectPtr<UAnimSequence> CrouchEntry;
    UPROPERTY(EditAnywhere, Category="Animations") TObjectPtr<UAnimSequence> CrouchExit;
    UPROPERTY(EditAnywhere, Category="Animations") TObjectPtr<UAnimSequence> RifleFire;
    UPROPERTY(EditAnywhere, Category="Animations") TObjectPtr<UAnimSequence> RifleReload;
    UPROPERTY(EditAnywhere, Category="Timing", meta=(ClampMin="0.01",Units="s")) float AimSeconds=.8f;
    // Minimum shot period. Zero replays immediately after each complete fire animation.
    UPROPERTY(EditAnywhere, Category="Timing", meta=(ClampMin="0.0",Units="s")) float RifleShotInterval=0.f;
    UPROPERTY(EditAnywhere, Category="Timing", meta=(ClampMin="0.01",Units="s")) float PostureBlendSeconds=.5f;
    UPROPERTY(EditAnywhere, Category="Timing", meta=(ClampMin="0.0",Units="s")) float WindowCoverSeconds=1.f;
    UPROPERTY(EditAnywhere, Category="Timing", meta=(ClampMin="0.0",Units="s")) float RPGReloadStartSeconds=.35f;
    // Sampled separately per enemy and on every new action, without shortening animations.
    UPROPERTY(EditAnywhere, Category="Timing|Variation") bool bRandomizeTiming=true;
    UPROPERTY(EditAnywhere, Category="Timing|Variation",meta=(Units="s")) FVector2D EntryDelayJitter=FVector2D(.05f,.9f);
    UPROPERTY(EditAnywhere, Category="Timing|Variation",meta=(Units="s")) FVector2D ActionDelayJitter=FVector2D(0.f,.8f);
    float SampleEntryDelay() const { return SampleDelay(EntryDelayJitter); }
    float SampleActionDelay() const { return SampleDelay(ActionDelayJitter); }
private:
    float SampleDelay(FVector2D Range) const
    {
        if (!bRandomizeTiming || !FMath::IsFinite(Range.X) || !FMath::IsFinite(Range.Y)) return 0.f;
        const float Low=FMath::Clamp(float(FMath::Min(Range.X,Range.Y)),0.f,5.f);
        const float High=FMath::Clamp(float(FMath::Max(Range.X,Range.Y)),Low,5.f);
        return FMath::FRandRange(Low,High);
    }
};

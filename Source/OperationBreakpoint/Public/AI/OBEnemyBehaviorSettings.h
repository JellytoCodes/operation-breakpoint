#pragma once

#include "CoreMinimal.h"
#include "AI/OBEnemyCombatPattern.h"
#include "OBEnemyBehaviorSettings.generated.h"

UENUM(BlueprintType)
enum class EOBEnemyEntryMode : uint8
{
    RunToStaging,
    WaitAtSpawn
};

UENUM(BlueprintType)
enum class EOBEnemyCoverMode : uint8
{
    None,
    CrouchInPlace,
    MoveToCover
};

USTRUCT(BlueprintType)
struct OPERATIONBREAKPOINT_API FOBEnemyBehaviorSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Entry")
    EOBEnemyEntryMode EntryMode = EOBEnemyEntryMode::RunToStaging;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Entry", meta=(ClampMin="1.0", Units="cm/s"))
    float RunSpeed = 600.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Reaction", meta=(ClampMin="0.0", Units="s"))
    float ReactionDelay = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Reaction")
    bool bFacePlayer = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Reaction", meta=(ClampMin="1.0", Units="deg/s"))
    float TurnSpeed = 180.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover")
    EOBEnemyCoverMode CoverMode = EOBEnemyCoverMode::None;

    // Time spent crouching before the existing combat StateTree starts.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover", meta=(ClampMin="0.0", Units="s"))
    float CoverHoldSeconds = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat")
    bool bStartStateTreeAfterReaction = true;

    // An explicit spawner CombatStateTree takes precedence. RPG troops always use the RPG tree.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat")
    EOBEnemyCombatPattern CombatPattern=EOBEnemyCombatPattern::Automatic;
};

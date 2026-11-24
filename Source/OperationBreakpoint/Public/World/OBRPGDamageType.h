#pragma once

#include "CoreMinimal.h"
#include "GameFramework/DamageType.h"
#include "OBRPGDamageType.generated.h"

// Kept on the damage event all the way from the rocket to the player's cover/life rules.
UCLASS()
class OPERATIONBREAKPOINT_API UOBRPGDamageType : public UDamageType
{
    GENERATED_BODY()
};

#pragma once

#include "Engine/EngineTypes.h"

// Keep these object channels in sync with DefaultEngine.ini. Shot queries also
// restrict object types, so BlockAll scenery cannot intercept a character shot.
namespace OBCombatCollision
{
    constexpr ECollisionChannel PlayerShotTarget = ECC_GameTraceChannel1;
    constexpr ECollisionChannel EnemyBullet = ECC_GameTraceChannel2;
    constexpr ECollisionChannel PlayerBulletTarget = ECC_GameTraceChannel3;
}

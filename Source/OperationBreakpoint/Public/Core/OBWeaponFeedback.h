#pragma once

#include "Kismet/GameplayStatics.h"
#include "Sound/SoundWave.h"

// Shared by live gameplay and the F6 diagnostic screen.
namespace OBWeaponFeedback
{
inline void Prepare(USoundBase* Sound)
{
    if (!Sound) return;
    if (auto* Wave=Cast<USoundWave>(Sound))
        Wave->OverrideLoadingBehavior(ESoundWaveLoadingBehavior::RetainOnLoad);
    UGameplayStatics::PrimeSound(Sound);
}

inline float ReloadStartTime(const USoundBase* Sound)
{
    // This asset's first strong insertion transient is at 0.572s.
    // Keep its attack, and leave any replacement reload asset untrimmed.
    return Sound && Sound->GetPathName()==TEXT("/Game/Assets/FX/SFX/PlaySound/Reload.Reload") ? .565f : 0.f;
}
}

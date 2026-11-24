#include "Training/OBTrainingDirector.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundNodeWavePlayer.h"
#include "Sound/SoundWave.h"

DEFINE_LOG_CATEGORY_STATIC(LogOBTrainingAudio, Log, All);

namespace
{
// Scale at playback so existing map/Blueprint overrides receive the same 50% boost.
constexpr float TrainingBackgroundVolumeScale=1.5f;

UAudioComponent* CreateTrainingSound(const UObject* Context, USoundBase* Sound, float Volume, bool bAutoDestroy=true)
{
    auto* Audio=Sound ? UGameplayStatics::CreateSound2D(Context,Sound,Volume,1.f,0.f,nullptr,false,bAutoDestroy) : nullptr;
    if (Audio) Audio->bIsUISound=false; // World pause also pauses training audio.
    return Audio;
}
void DestroyTrainingSound(TObjectPtr<UAudioComponent>& Audio)
{
    if (IsValid(Audio)) { Audio->Stop(); Audio->DestroyComponent(); }
    Audio=nullptr;
}
}

void AOBTrainingDirector::StartTrainingAudio()
{
    if (!bInitialized || Result.bFinished || IsValid(TrainingMusicAudio)) return;
    // Preload the short signals before the timed exercise/first walking step.
    LoadedWalkingSound=WalkingSound.LoadSynchronous();
    MockStartSiren.LoadSynchronous(); MissionEndSiren.LoadSynchronous();
    USoundBase* Music=TrainingMusic.LoadSynchronous();
    if (auto* Wave=Cast<USoundWave>(Music))
    {
        // A native wave-player loops in the audio renderer without a game-thread
        // restart or changes to the imported SoundWave's saved looping flag.
        TrainingMusicLoop=NewObject<USoundCue>(this);
        auto* Player=NewObject<USoundNodeWavePlayer>(TrainingMusicLoop);
        Player->SetSoundWave(Wave);
        Player->bLooping=true;
        TrainingMusicLoop->FirstNode=Player;
        TrainingMusicLoop->Duration=INDEFINITELY_LOOPING_DURATION;
        TrainingMusicLoop->VolumeMultiplier=1.f;
        Music=TrainingMusicLoop;
    }
    CurrentMusicVolume=FMath::Max(0.f,MusicVolume);
    TrainingMusicAudio=CreateTrainingSound(this,Music,CurrentMusicVolume*TrainingBackgroundVolumeScale,false);
    if (TrainingMusicAudio) TrainingMusicAudio->FadeIn(.5f,1.f);
    FootstepRemaining=0.f;
    UE_LOG(LogOBTrainingAudio,Display,TEXT("TRAINING_AUDIO event=MusicStart sound=%s loop=1 volume=%.3f"),
        *TrainingMusic.GetAssetName(),CurrentMusicVolume*TrainingBackgroundVolumeScale);
}

void AOBTrainingDirector::StopTrainingFootsteps()
{
    if (IsValid(FootstepAudio))
        UE_LOG(LogOBTrainingAudio,Display,TEXT("TRAINING_AUDIO event=FootstepStop"));
    DestroyTrainingSound(FootstepAudio);
    FootstepRemaining=0.f;
}

void AOBTrainingDirector::UpdateTrainingAudio(float DeltaSeconds)
{
    if (!bInitialized || Result.bFinished) return;
    const bool bVoice=(IsValid(PrototypeNarrationAudio) && PrototypeNarrationAudio->IsPlaying()) ||
        (IsValid(PrototypeHintAudio) && PrototypeHintAudio->IsPlaying());
    const float TargetVolume=FMath::Max(0.f,bVoice ? FMath::Min(MusicVolume,MusicVoiceVolume) : MusicVolume);
    CurrentMusicVolume=FMath::FInterpTo(CurrentMusicVolume,TargetVolume,FMath::Max(0.f,DeltaSeconds),5.f);
    if (IsValid(TrainingMusicAudio)) TrainingMusicAudio->SetVolumeMultiplier(CurrentMusicVolume*TrainingBackgroundVolumeScale);

    if (!Spline || !Spline->IsMoving() || bPrototypePaused || UGameplayStatics::IsGamePaused(this))
    {
        StopTrainingFootsteps();
        return;
    }
    FootstepRemaining-=FMath::Max(0.f,DeltaSeconds);
    if (FootstepRemaining>0.f) return;
    DestroyTrainingSound(FootstepAudio);
    FootstepAudio=CreateTrainingSound(this,LoadedWalkingSound,FMath::Max(0.f,FootstepVolume)*TrainingBackgroundVolumeScale);
    if (FootstepAudio) FootstepAudio->Play();
    FootstepRemaining=FMath::Max(.1f,FootstepIntervalSeconds);
    UE_LOG(LogOBTrainingAudio,Display,TEXT("TRAINING_AUDIO event=Footstep sound=%s volume=%.3f"),
        *WalkingSound.GetAssetName(),FMath::Max(0.f,FootstepVolume)*TrainingBackgroundVolumeScale);
}

void AOBTrainingDirector::PlayTrainingSignal(const TSoftObjectPtr<USoundBase>& Sound, const TCHAR* Event)
{
    DestroyTrainingSound(TrainingSignalAudio);
    if (auto* Asset=Sound.LoadSynchronous())
    {
        TrainingSignalAudio=CreateTrainingSound(this,Asset,1.f);
        if (TrainingSignalAudio) TrainingSignalAudio->Play();
        UE_LOG(LogOBTrainingAudio,Display,TEXT("TRAINING_AUDIO event=%s sound=%s"),Event,*Asset->GetName());
    }
}

void AOBTrainingDirector::StopTrainingAudio(bool bFadeMusic)
{
    StopTrainingFootsteps();
    DestroyTrainingSound(TrainingSignalAudio);
    if (bFadeMusic && IsValid(TrainingMusicAudio)) TrainingMusicAudio->FadeOut(.5f,0.f);
    else
    {
        DestroyTrainingSound(TrainingMusicAudio);
        TrainingMusicLoop=nullptr;
        LoadedWalkingSound=nullptr;
    }
}

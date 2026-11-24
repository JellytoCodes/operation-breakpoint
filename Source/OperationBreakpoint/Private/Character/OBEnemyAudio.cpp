#include "Character/OBEnemyCharacter.h"
#include "Components/AudioComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "Engine/World.h"

namespace
{
    void ConfigureSpatialAudio(UAudioComponent* Audio,float InnerRadius,float Falloff)
    {
        Audio->bAutoActivate=false;
        Audio->bAutoDestroy=false;
        Audio->bStopWhenOwnerDestroyed=true;
        Audio->bIsUISound=false;
        Audio->bAllowSpatialization=true;
        Audio->bOverrideAttenuation=true;
        auto& Settings=Audio->AttenuationOverrides;
        Settings.bAttenuate=true;
        Settings.bSpatialize=true;
        Settings.AttenuationShape=EAttenuationShape::Sphere;
        Settings.AttenuationShapeExtents=FVector(InnerRadius,0,0);
        Settings.FalloffDistance=Falloff;
        Settings.DistanceAlgorithm=EAttenuationDistanceModel::Logarithmic;
        Settings.NonSpatializedRadiusStart=0.f;
        Settings.NonSpatializedRadiusEnd=0.f;
    }
}

void AOBEnemyCharacter::InitializeEnemyAudio()
{
    EnemyAudioOrigin=CreateDefaultSubobject<USceneComponent>(TEXT("EnemyAudioOrigin"));
    EnemyAudioOrigin->SetupAttachment(GetMesh());
    EnemyFireAudio=CreateDefaultSubobject<UAudioComponent>(TEXT("EnemyFireAudio"));
    EnemyFireAudio->SetupAttachment(EnemyAudioOrigin);
    ConfigureSpatialAudio(EnemyFireAudio,200.f,8000.f);
    EnemyFireAudio->SetVolumeMultiplier(.2f);
    // Preserve the tail of a previous shot when another round is fired.
    EnemyFireAudio->bCanPlayMultipleInstances=true;
    EnemyVoiceAudio=CreateDefaultSubobject<UAudioComponent>(TEXT("EnemyVoiceAudio"));
    EnemyVoiceAudio->SetupAttachment(EnemyAudioOrigin);
    ConfigureSpatialAudio(EnemyVoiceAudio,150.f,3000.f);
    EnemyVoiceAudio->SetVolumeMultiplier(3.f);
    EnemyVoiceAudio->bCanPlayMultipleInstances=false;
    EnemyReloadAudio=CreateDefaultSubobject<UAudioComponent>(TEXT("EnemyReloadAudio"));
    EnemyReloadAudio->SetupAttachment(EnemyAudioOrigin);
    ConfigureSpatialAudio(EnemyReloadAudio,150.f,3000.f);
    EnemyReloadAudio->SetVolumeMultiplier(.2f);
    EnemyReloadAudio->bCanPlayMultipleInstances=false;
}

void AOBEnemyCharacter::AttachEnemyAudioOrigin()
{
    if (!EnemyAudioOrigin || !GetMesh()) return;
    // A custom mesh without the socket still emits from this character, never the world origin.
    const FName Socket=GetMesh()->DoesSocketExist(EnemyAudioSocketName) ? EnemyAudioSocketName
        : GetMesh()->DoesSocketExist(TEXT("head")) ? FName(TEXT("head")) : NAME_None;
    EnemyAudioOrigin->AttachToComponent(GetMesh(),FAttachmentTransformRules::KeepRelativeTransform,Socket);
}

void AOBEnemyCharacter::PostInitializeComponents()
{
    Super::PostInitializeComponents();
    AttachEnemyAudioOrigin();
    if (!IsRPGTrooper() && EnemyRifleFireSound) EnemyFireAudio->SetSound(EnemyRifleFireSound);
    if (IsRPGTrooper() && EnemyRPGFireSound) EnemyFireAudio->SetSound(EnemyRPGFireSound);
    EnemyFireAudio->SetVolumeMultiplier(.2f);
    EnemyReloadAudio->SetVolumeMultiplier(.2f);
    EnemyVoiceAudio->SetVolumeMultiplier(3.f);
    if (EnemyRPGReloadSound) EnemyReloadAudio->SetSound(EnemyRPGReloadSound);
}

void AOBEnemyCharacter::PlayEnemyFireSound()
{
    // Changing archetype must not give the launcher an inherited AK shot.
    if (IsRPGTrooper() && EnemyRPGFireSound && EnemyFireAudio && EnemyFireAudio->Sound!=EnemyRPGFireSound)
        EnemyFireAudio->SetSound(EnemyRPGFireSound);
    if (IsRPGTrooper() && EnemyFireAudio && EnemyFireAudio->Sound==EnemyRifleFireSound) return;
    if (!bDead && bEncounterCombatant && EnemyFireAudio && EnemyFireAudio->Sound)
        EnemyFireAudio->Play();
}

void AOBEnemyCharacter::PlayEnemyReloadSound()
{
    if (bDead || !bEncounterCombatant || !IsRPGTrooper() || !EnemyReloadAudio || !EnemyRPGReloadSound) return;
    if (EnemyReloadAudio->Sound!=EnemyRPGReloadSound) EnemyReloadAudio->SetSound(EnemyRPGReloadSound);
    EnemyReloadAudio->Play();
}

int32 AOBEnemyCharacter::PlayEncounterEntryVoices(const TArray<TObjectPtr<AOBEnemyCharacter>>& Enemies)
{
    TArray<AOBEnemyCharacter*> Candidates;
    TArray<AOBEnemyCharacter*> RPGCandidates;
    bool bRPGVoiceReserved=false;
    for (AOBEnemyCharacter* Enemy : Enemies)
    {
        if (!IsValid(Enemy)) continue;
        bRPGVoiceReserved |= Enemy->bRPGEntryVoiceAssigned;
        if (Enemy->bDead || !Enemy->bEncounterCombatant || Enemy->bEntryVoiceAssigned) continue;
        if (!Enemy->EnemyEntryVoiceLines.IsEmpty() || (Enemy->IsRPGTrooper() && Enemy->EnemyRPGEntryVoice)) Candidates.AddUnique(Enemy);
        if (Enemy->IsRPGTrooper() && Enemy->EnemyRPGEntryVoice) RPGCandidates.AddUnique(Enemy);
    }
    const int32 Budget=Candidates.IsEmpty() ? 0 : FMath::RandRange(1,FMath::Min(2,Candidates.Num()));
    TSet<USoundBase*> Used;float Delay=.05f;int32 Assigned=0;
    auto Schedule=[&](AOBEnemyCharacter* Enemy,USoundBase* Line) {
        Used.Add(Line);
        Enemy->PendingEntryVoice=Line;Enemy->bEntryVoiceAssigned=true;
        TWeakObjectPtr<AOBEnemyCharacter> Weak=Enemy;
        Enemy->GetWorldTimerManager().SetTimer(Enemy->EntryVoiceTimer,FTimerDelegate::CreateLambda([Weak]() {
            if (auto* Speaker=Weak.Get()) { Speaker->PlayEnemyVoice(Speaker->PendingEntryVoice);Speaker->PendingEntryVoice=nullptr; }
        }),Delay,false);
        Delay+=FMath::Clamp(Line->GetDuration(),.2f,30.f)+.2f;++Assigned;
    };
    // Reserve one of the same two voice slots for exactly one launcher on entry.
    if (!bRPGVoiceReserved && !RPGCandidates.IsEmpty())
    {
        auto* Enemy=RPGCandidates[FMath::RandHelper(RPGCandidates.Num())];
        Enemy->bRPGEntryVoiceAssigned=true;Candidates.Remove(Enemy);
        Schedule(Enemy,Enemy->EnemyRPGEntryVoice);
    }
    while (!Candidates.IsEmpty() && Assigned<Budget)
    {
        const int32 Index=FMath::RandHelper(Candidates.Num());auto* Enemy=Candidates[Index];Candidates.RemoveAtSwap(Index);
        TArray<USoundBase*> Lines;
        for (USoundBase* Line : Enemy->EnemyEntryVoiceLines) if (Line && !Used.Contains(Line)) Lines.AddUnique(Line);
        if (!Lines.IsEmpty()) Schedule(Enemy,Lines[FMath::RandHelper(Lines.Num())]);
    }
    return Assigned;
}

bool AOBEnemyCharacter::PlayEnemyVoice(USoundBase* VoiceLine)
{
    if (bDead || !bEncounterCombatant || !EnemyVoiceAudio || (!VoiceLine && !EnemyVoiceAudio->Sound)) return false;
    EnemyVoiceAudio->Stop();
    if (VoiceLine) EnemyVoiceAudio->SetSound(VoiceLine);
    EnemyVoiceAudio->Play();
    return true;
}

void AOBEnemyCharacter::StopEnemyVoice()
{
    if (EnemyVoiceAudio) EnemyVoiceAudio->Stop();
}

void AOBEnemyCharacter::StopEnemyAudio()
{
    if (GetWorld()) GetWorldTimerManager().ClearTimer(EntryVoiceTimer);
    PendingEntryVoice=nullptr;
    if (EnemyReloadAudio) EnemyReloadAudio->Stop();
    if (EnemyFireAudio) EnemyFireAudio->Stop();
    StopEnemyVoice();
}

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Character/OBEnemyCharacter.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundNodeRandom.h"
#include "Sound/SoundNodeWavePlayer.h"
#include "Sound/SoundWaveProcedural.h"
#include "TimerManager.h"
#include "Character/OBPlayerCharacter.h"
#include "World/OBRPGRocketActor.h"
#include "Sound/SoundAttenuation.h"
#include "UObject/UObjectIterator.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBEnemyEngagementAudioTest,"OperationBreakpoint.Combat.EnemyEngagementAudio",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOBEnemyEngagementAudioTest::RunTest(const FString&)
{
    auto* Rifle=LoadClass<AOBEnemyCharacter>(nullptr,TEXT("/Game/Blueprints/Enemy/BP_EnemyCharacter.BP_EnemyCharacter_C"));
    auto* RPG=LoadClass<AOBEnemyCharacter>(nullptr,TEXT("/Game/Blueprints/Enemy/BP_RPGTrooper.BP_RPGTrooper_C"));
    if (!TestNotNull(TEXT("Rifle Blueprint loads"),Rifle) || !TestNotNull(TEXT("RPG Blueprint loads"),RPG)) return false;
    for (auto* Class : {Rifle,RPG})
    {
        const auto* Defaults=Class->GetDefaultObject<AOBEnemyCharacter>();
        TestEqual(TEXT("The five renamed voice lines are persisted in the production Blueprint"),Defaults->EnemyEntryVoiceLines.Num(),5);
        TSet<USoundBase*> Unique;
        for (USoundBase* Line : Defaults->EnemyEntryVoiceLines)
        {
            if (TestNotNull(TEXT("Voice asset reference resolves"),Line)) { Unique.Add(Line);TestTrue(TEXT("Voice uses the English identifier"),Line->GetName().StartsWith(TEXT("Enemy_Entry_VO_"))); }
        }
        TestEqual(TEXT("The voice pool contains five distinct assets"),Unique.Num(),5);
        TestTrue(TEXT("The RPG reload clip is persisted"),Defaults->EnemyRPGReloadSound && Defaults->EnemyRPGReloadSound->GetName()==TEXT("RPG-7_reload"));
        TestTrue(TEXT("The RPG launch clip is persisted"),Defaults->EnemyRPGFireSound && Defaults->EnemyRPGFireSound->GetName()==TEXT("RPG-7shot_start"));
        TestTrue(TEXT("The RPG-only entry voice uses an English asset name"),Defaults->EnemyRPGEntryVoice && Defaults->EnemyRPGEntryVoice->GetName()==TEXT("Enemy_RPG_Entry_VO_01"));
    }
    auto* Cue=Cast<USoundCue>(Rifle->GetDefaultObject<AOBEnemyCharacter>()->EnemyRifleFireSound);
    auto* Random=Cue ? Cast<USoundNodeRandom>(Cue->FirstNode) : nullptr;
    if (!TestNotNull(TEXT("AK fire uses a random cue"),Random)) return false;
    TestEqual(TEXT("AK random node has exactly two variants"),Random->ChildNodes.Num(),2);
    TestTrue(TEXT("AK variants cycle without immediate repetition"),bool(Random->bRandomizeWithoutReplacement));
    TSet<FName> ShotNames;
    for (USoundNode* Child : Random->ChildNodes)
        if (auto* Wave=Cast<USoundNodeWavePlayer>(Child);Wave && Wave->GetSoundWave()) ShotNames.Add(Wave->GetSoundWave()->GetFName());
    TestTrue(TEXT("The provided AK clips are both referenced"),ShotNames.Contains(TEXT("AK_47_Shot1")) && ShotNames.Contains(TEXT("AK_47_Shot2")));
    auto* W=UWorld::CreateWorld(EWorldType::Game,false);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
    W->SetGameInstance(NewObject<UGameInstance>(GEngine));FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));W->SetGameMode(URL);W->bAllowAudioPlayback=true;
    ON_SCOPE_EXIT { W->DestroyWorld(false);GEngine->DestroyWorldContext(W); };
    if (!TestNotNull(TEXT("An actual audio device exists"),W->GetAudioDeviceRaw())) return false;
    TArray<TObjectPtr<AOBEnemyCharacter>> Enemies;TArray<TObjectPtr<USoundBase>> Lines;
    for (int32 I=0;I<5;++I)
    {
        auto* Wave=NewObject<USoundWaveProcedural>(W);Wave->SetSampleRate(24000);Wave->NumChannels=1;Wave->Duration=.3f;
        TArray<uint8> PCM;PCM.SetNumZeroed(24000*2);Wave->QueueAudio(PCM.GetData(),PCM.Num());Lines.Add(Wave);
    }
    for (int32 I=0;I<7;++I) {
        auto* E=W->SpawnActor<AOBEnemyCharacter>();E->EnemyEntryVoiceLines=Lines;Enemies.Add(E);
        TestEqual(TEXT("Shot playback gain is 20 percent"),E->EnemyFireAudio->VolumeMultiplier,.2f);
        TestEqual(TEXT("Reload playback gain is 20 percent"),E->EnemyReloadAudio->VolumeMultiplier,.2f);
    TestEqual(TEXT("Voice playback gain is tripled"),E->EnemyVoiceAudio->VolumeMultiplier,3.f);
    }
    Enemies[5]->SetEncounterCombatant(false);Enemies[6]->bDead=true;
    for (int32 Round=0;Round<32;++Round)
    {
        const int32 Count=AOBEnemyCharacter::PlayEncounterEntryVoices(Enemies);TestTrue(TEXT("Only one or two representatives are assigned"),Count>=1 && Count<=2);
        TSet<USoundBase*> Used;int32 Actual=0;
        for (int32 I=0;I<Enemies.Num();++I)
        {
            auto* E=Enemies[I].Get();
            if (!E->PendingEntryVoice) continue;
            ++Actual;TestTrue(TEXT("Only living combatants receive entry speech"),I<5);
            TestFalse(TEXT("Representatives do not repeat a line within an entry"),Used.Contains(E->PendingEntryVoice));Used.Add(E->PendingEntryVoice);
        }
        TestEqual(TEXT("Reservations match the representative budget"),Actual,Count);
        for (AOBEnemyCharacter* E : Enemies) { E->StopEnemyAudio();E->bEntryVoiceAssigned=false; }
    }
    auto* RPGLine=NewObject<USoundWaveProcedural>(W);RPGLine->SetSampleRate(24000);RPGLine->NumChannels=1;RPGLine->Duration=.3f;
    for (int32 I=0;I<2;++I) { Enemies[I]->ConfigureEnemyArchetype(EOBEnemyArchetype::RPGTrooper);Enemies[I]->EnemyRPGEntryVoice=RPGLine; }
    for (int32 Round=0;Round<16;++Round)
    {
        const int32 Count=AOBEnemyCharacter::PlayEncounterEntryVoices(Enemies);int32 RPGVoices=0;
        TestTrue(TEXT("RPG entry shares the existing two-person voice budget"),Count>=1 && Count<=2);
        for (int32 I=0;I<Enemies.Num();++I)
            if (Enemies[I]->PendingEntryVoice==RPGLine) { ++RPGVoices;TestTrue(TEXT("Only an RPG trooper receives the special entry line"),I<2); }
        TestEqual(TEXT("Exactly one of multiple RPG troopers calls out"),RPGVoices,1);
        AOBEnemyCharacter::PlayEncounterEntryVoices(Enemies);
        int32 Reservations=0;for (AOBEnemyCharacter* E : Enemies) Reservations+=E->bRPGEntryVoiceAssigned ? 1 : 0;
        TestEqual(TEXT("Repeated encounter notification never assigns a second RPG shout"),Reservations,1);
        for (AOBEnemyCharacter* E : Enemies) { E->StopEnemyAudio();E->bEntryVoiceAssigned=false;E->bRPGEntryVoiceAssigned=false; }
    }
    auto* Player=W->SpawnActor<AOBPlayerCharacter>(FVector(100,0,0),FRotator::ZeroRotator);
    auto* Rocket=W->SpawnActor<AOBRPGRocketActor>();
    TestTrue(TEXT("Native rocket references the supplied player impact clip"),Rocket->PlayerImpactSound.ToSoftObjectPath().ToString().EndsWith(TEXT("RPG-7shot_end.RPG-7shot_end")));
    TestNotNull(TEXT("The impact sound asset resolves"),Rocket->PlayerImpactSound.LoadSynchronous());
    Rocket->PlayerImpactSound=Lines[1].Get();Rocket->FallbackImpactEffect.Reset();
    auto ImpactCount=[&]() { int32 Count=0;for (TObjectIterator<UAudioComponent> It;It;++It) if (It->GetWorld()==W && It->Sound==Lines[1] && It->IsPlaying()) { ++Count;TestEqual(TEXT("Player impact sound gain is 20 percent"),It->VolumeMultiplier,.2f);TestTrue(TEXT("Player impact remains distance attenuated"),It->GetAttenuationSettingsToApply() && It->GetAttenuationSettingsToApply()->bAttenuate); }return Count; };
    FHitResult PlayerHit(Player,Player->GetCapsuleComponent(),Player->GetActorLocation(),FVector::UpVector);
    Rocket->OnBulletImpact(PlayerHit);TestEqual(TEXT("Player collision starts exactly one impact sound"),ImpactCount(),1);
    Rocket->OnBulletImpact(PlayerHit);TestEqual(TEXT("Duplicate impacts never replay the clip"),ImpactCount(),1);
    auto* Miss=W->SpawnActor<AOBRPGRocketActor>();Miss->PlayerImpactSound=Lines[1].Get();Miss->FallbackImpactEffect.Reset();
    Miss->OnBulletImpact(FHitResult());TestEqual(TEXT("A non-player impact does not play the player hit clip"),ImpactCount(),1);
    auto* Launcher=W->SpawnActor<AOBEnemyCharacter>();Launcher->ConfigureEnemyArchetype(EOBEnemyArchetype::RPGTrooper);Launcher->EnemyRPGReloadSound=Lines[0];
    Launcher->EnemyRPGFireSound=Lines[2];Launcher->EnemyFireAudio->SetSound(Lines[3]);
    Launcher->PlayEnemyFireSound();
    TestTrue(TEXT("RPG launch selects its own clip instead of an inherited rifle sound"),Launcher->EnemyFireAudio->Sound==Lines[2] && Launcher->EnemyFireAudio->IsPlaying());
    int32 ReloadStarts=0;
    Launcher->EnemyReloadAudio->OnAudioPlayStateChangedNative.AddLambda([&](const UAudioComponent*,EAudioComponentPlayState State) { if (State==EAudioComponentPlayState::Playing) ++ReloadStarts; });
    Launcher->SetCombatState(EOBEnemyCombatState::Reloading);
    TestEqual(TEXT("Entering RPG reload starts the attached sound exactly once"),ReloadStarts,1);
    Launcher->SetCombatState(EOBEnemyCombatState::Reloading);TestEqual(TEXT("Duplicate state notifications do not double-play reload"),ReloadStarts,1);
    const auto* Attenuation=Launcher->EnemyReloadAudio->GetAttenuationSettingsToApply();
    TestTrue(TEXT("Reload remains a character-attached distance-attenuated sound"),Launcher->EnemyReloadAudio->GetAttachParent()==Launcher->EnemyAudioOrigin && Attenuation && Attenuation->bAttenuate && Attenuation->bSpatialize);
    Launcher->SetEncounterCombatant(false);TestFalse(TEXT("Leaving combat stops the reload"),Launcher->EnemyReloadAudio->IsPlaying());
    Launcher->EnemyReloadAudio->OnAudioPlayStateChangedNative.Clear();
    return !HasAnyErrors();
}
#endif

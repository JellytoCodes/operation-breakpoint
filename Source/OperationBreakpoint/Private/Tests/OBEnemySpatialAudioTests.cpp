#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "AI/OBAIController.h"
#include "Animation/AnimSequence.h"
#include "Character/OBActionComponent.h"
#include "Character/OBEnemyCharacter.h"
#include "Character/OBPlayerCharacter.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/OBPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "Sound/SoundWaveProcedural.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBEnemySpatialAudioTest,"OperationBreakpoint.Combat.EnemySpatialAudio",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOBEnemySpatialAudioTest::RunTest(const FString&)
{
    TGuardValue<bool> ScriptGuard(GAllowActorScriptExecutionInEditor,true);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));World->SetGameMode(URL);
    World->bAllowAudioPlayback=true;
    ON_SCOPE_EXIT { World->DestroyWorld(false);GEngine->DestroyWorldContext(World); };
    if (!TestNotNull(TEXT("An actual audio device is available for playback-gate verification"),World->GetAudioDeviceRaw())) return false;
    auto* PC=World->SpawnActor<AOBPlayerController>();World->AddController(PC);
    auto* Player=World->SpawnActor<AOBPlayerCharacter>(FVector(0,0,100),FRotator::ZeroRotator);PC->Possess(Player);
    Player->GetActionComponent()->SetExternalActionState(true);
    for (const TCHAR* Path : {TEXT("/Game/Blueprints/Enemy/BP_EnemyCharacter.BP_EnemyCharacter_C"),
        TEXT("/Game/Blueprints/Enemy/BP_RPGTrooper.BP_RPGTrooper_C")})
    {
        auto* Class=LoadClass<AOBEnemyCharacter>(nullptr,Path);
        if (!TestNotNull(TEXT("Production enemy Blueprint loads"),Class)) return false;
        auto* Enemy=World->SpawnActor<AOBEnemyCharacter>(Class,FVector(900,500,100),FRotator(0,35,0));
        auto* Body=Enemy->GetMesh();auto* Origin=Enemy->EnemyAudioOrigin.Get();
        auto* Fire=Enemy->EnemyFireAudio.Get();auto* Voice=Enemy->EnemyVoiceAudio.Get();
        if (!TestNotNull(TEXT("Character audio origin exists"),Origin) || !TestNotNull(TEXT("Fire audio exists"),Fire)
            || !TestNotNull(TEXT("Voice audio exists"),Voice)) return false;
        TestEqual(TEXT("Production Blueprint voice gain is tripled after defaults"),Voice->VolumeMultiplier,3.f);
        TestTrue(TEXT("The saved mesh contains the named character audio socket"),Body->DoesSocketExist(Enemy->EnemyAudioSocketName));
        TestTrue(TEXT("Audio uses the named animated socket, not a fixed world location"),Origin->GetAttachParent()==Body && Origin->GetAttachSocketName()==Enemy->EnemyAudioSocketName);
        TestTrue(TEXT("Both emitters share the character origin"),Fire->GetAttachParent()==Origin && Voice->GetAttachParent()==Origin);
        TestNotNull(TEXT("Production rifle cue is configured"),Enemy->EnemyRifleFireSound.Get());
        TestNotNull(TEXT("Production RPG reload clip is configured"),Enemy->EnemyRPGReloadSound.Get());
        TestEqual(TEXT("Five entry lines are configured"),Enemy->EnemyEntryVoiceLines.Num(),5);
        TestFalse(TEXT("Unassigned voice stays silent"),Enemy->PlayEnemyVoice());
        TestTrue(TEXT("Rapid shots preserve previous tails while dialogue is exclusive"),Fire->bCanPlayMultipleInstances && !Voice->bCanPlayMultipleInstances);
        for (auto* Audio : {Fire,Voice})
        {
            const auto* Settings=Audio->GetAttenuationSettingsToApply();
            if (!TestNotNull(TEXT("Emitter has effective attenuation settings"),Settings)) return false;
            TestTrue(TEXT("Emitter is positional and never a UI/2D sound"),Audio->bAllowSpatialization && !Audio->bIsUISound && Audio->bOverrideAttenuation && Settings->bAttenuate && Settings->bSpatialize);
            TestFalse(TEXT("No sound autoplays on spawn"),Audio->bAutoActivate);
            const FTransform Source(FRotator(0,35,0),FVector(1400,600,200));
            const float Radius=Settings->AttenuationShapeExtents.X,Range=Settings->FalloffDistance;
            float Last=2.f;
            for (float Fraction : {0.f,.1f,.4f,.8f,1.1f})
            {
                const float Gain=Settings->Evaluate(Source,Source.GetLocation()+FVector(Radius+Range*Fraction,0,0));
                TestTrue(TEXT("Moving the listener farther away monotonically lowers the actual engine attenuation gain"),Gain<=Last && Gain>=0.f && Gain<=1.f);
                Last=Gain;
            }
            TestTrue(TEXT("Outside the configured radius the sound attenuates to silence"),Last<=UE_KINDA_SMALL_NUMBER);
            AddInfo(FString::Printf(TEXT("%s %s inner=%.0f cm falloff=%.0f cm"),*Enemy->GetClass()->GetName(),*Audio->GetName(),Radius,Range));
        }
        TestTrue(TEXT("Shots have a longer audible range than speech"),Fire->AttenuationOverrides.GetMaxDimension()>Voice->AttenuationOverrides.GetMaxDimension());
        if (!TestNotNull(TEXT("Actual character posture assets exist"),Enemy->CombatPatterns.Get())) return false;
        FVector StandingOrigin=FVector::ZeroVector;
        Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
        int32 PoseIndex=0;
        for (auto* Pose : {Enemy->CombatPatterns->StandAim.Get(),Enemy->CombatPatterns->ProneAim.Get()})
        {
            Body->SetAnimation(Pose);Body->SetPosition(0.f,false);Body->TickAnimation(0.f,false);Body->RefreshBoneTransforms();
            Origin->UpdateComponentToWorld();Fire->UpdateComponentToWorld();Voice->UpdateComponentToWorld();
            TestTrue(TEXT("Both emitters follow the head socket in the evaluated posture"),Fire->GetComponentLocation().Equals(Body->GetSocketLocation(Enemy->EnemyAudioSocketName),.1f)
                && Voice->GetComponentLocation().Equals(Fire->GetComponentLocation(),.1f));
            if (!PoseIndex++) StandingOrigin=Origin->GetComponentLocation();
        }
        TestTrue(TEXT("Prone moves the sound source with the head instead of leaving it standing"),FVector::Distance(StandingOrigin,Origin->GetComponentLocation())>40.f);
        Enemy->SetActorLocationAndRotation(FVector(1500,-500,100),FRotator(0,120,0));Origin->UpdateComponentToWorld();
        TestTrue(TEXT("Translation/rotation keep the sound origin attached to the actor"),Origin->GetComponentLocation().Equals(Body->GetSocketLocation(Enemy->EnemyAudioSocketName),.1f));

        // Transient silent PCM exercises the real audio device without adding or choosing any user sound asset.
        auto SilentClip=[&]() {
            auto* Wave=NewObject<USoundWaveProcedural>(Enemy);Wave->SetSampleRate(24000);Wave->NumChannels=1;Wave->Duration=10.f;
            TArray<uint8> Silence;Silence.SetNumZeroed(24000*2*10);Wave->QueueAudio(Silence.GetData(),Silence.Num());return Wave;
        };
        Enemy->EnemyRifleFireSound=nullptr;Enemy->EnemyRPGFireSound=nullptr;Fire->SetSound(nullptr); // Isolate the actual audio gate using silent PCM below.
        int32 FireStarts=0,VoiceStarts=0;
        Fire->OnAudioPlayStateChangedNative.AddLambda([&](const UAudioComponent*,EAudioComponentPlayState State) { if (State==EAudioComponentPlayState::Playing) ++FireStarts; });
        Voice->OnAudioPlayStateChangedNative.AddLambda([&](const UAudioComponent*,EAudioComponentPlayState State) { if (State==EAudioComponentPlayState::Playing) ++VoiceStarts; });
        ON_SCOPE_EXIT {
            if (IsValid(Fire)) Fire->OnAudioPlayStateChangedNative.Clear();
            if (IsValid(Voice)) Voice->OnAudioPlayStateChangedNative.Clear();
        };
        Enemy->EncounterBehavior.EntryMode=EOBEnemyEntryMode::WaitAtSpawn;Enemy->EncounterBehavior.bStartStateTreeAfterReaction=false;
        if (!Enemy->GetController()) Enemy->SpawnDefaultController();
        auto* AI=Cast<AOBAIController>(Enemy->GetController());
        if (!TestNotNull(TEXT("Actual AI controller exists"),AI)) return false;
        AI->PrepareEncounterEntry(Enemy->GetActorTransform(),Enemy->GetActorTransform(),0.f);Enemy->BeginCombatPattern();
        TestTrue(TEXT("An unassigned fire sound does not block an actual projectile"),Enemy->FirePatternRound());
        TestEqual(TEXT("No missing-asset fire sound starts"),FireStarts,0);
        Fire->SetSound(SilentClip());
        TestTrue(TEXT("A configured shot still produces one actual projectile"),Enemy->FirePatternRound());
        TestEqual(TEXT("Each actual projectile starts its attached fire sound once"),FireStarts,1);
        Enemy->FireBullet();
        TestEqual(TEXT("A duplicate animation notify cannot duplicate the shot audio"),FireStarts,1);
        Player->GetActionComponent()->SetExternalActionState(false);
        TestFalse(TEXT("Prepare prevents actual firing"),Enemy->FirePatternRound());
        TestEqual(TEXT("Blocked firing remains silent"),FireStarts,1);
        Player->GetActionComponent()->SetExternalActionState(true);
        TestTrue(TEXT("Blueprint/StateTree can request a specific voice line"),Enemy->PlayEnemyVoice(SilentClip()));
        TestEqual(TEXT("The voice uses its own actual audio playback channel"),VoiceStarts,1);
        TestTrue(TEXT("Fire and voice can play together"),Fire->IsPlaying() && Voice->IsPlaying());
        Enemy->StopCombatPattern();
        TestTrue(TEXT("Stopping the StateTree pattern stops ongoing enemy audio"),!Fire->IsPlaying() && !Voice->IsPlaying());
        Enemy->BeginCombatPattern();Enemy->PlayEnemyVoice();
        Enemy->SetEncounterCombatant(false);
        TestTrue(TEXT("Leaving combat stops both audio channels"),!Fire->IsPlaying() && !Voice->IsPlaying());
        TestFalse(TEXT("A noncombat support actor cannot start an enemy voice"),Enemy->PlayEnemyVoice());
        Enemy->SetEncounterCombatant(true);
        TestTrue(TEXT("A configured voice can be started without passing the asset again"),Enemy->PlayEnemyVoice());
        FDamageEvent Damage;Enemy->TakeDamage(10000.f,Damage,nullptr,nullptr);
        TestTrue(TEXT("Death stops both channels"),!Fire->IsPlaying() && !Voice->IsPlaying());
        TestFalse(TEXT("A dead character cannot restart a voice line"),Enemy->PlayEnemyVoice());
        Fire->OnAudioPlayStateChangedNative.Clear();Voice->OnAudioPlayStateChangedNative.Clear();
        Enemy->Destroy();
    }
    return !HasAnyErrors();
}
#endif

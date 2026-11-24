#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "HAL/ThreadSafeCounter.h"
#include "Training/OBTrainingDirector.h"
#include "Training/OBTrainingTarget.h"
#include "Training/OBRailTrainingTarget.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Character/OBActionComponent.h"
#include "Core/OBPlayerController.h"
#include "Components/AudioComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundNodeWavePlayer.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "AudioDeviceManager.h"

namespace
{
class FTrainingSoundLog : public FOutputDevice
{
public:
    FThreadSafeCounter Hits, Drops, Starts, Ends, Steps;
    bool CanBeUsedOnAnyThread() const override { return true; }
    bool CanBeUsedOnMultipleThreads() const override { return true; }
    void Serialize(const TCHAR* Message,ELogVerbosity::Type,const FName&) override
    {
        const FString Text(Message);
        if (Text.Contains(TEXT("TRAINING_AUDIO event=TargetHit sound=Target_hit"))) Hits.Increment();
        if (Text.Contains(TEXT("TRAINING_AUDIO event=TargetDown sound=Target_down"))) Drops.Increment();
        if (Text.Contains(TEXT("TRAINING_AUDIO event=MockStart sound=Start_Siren"))) Starts.Increment();
        if (Text.Contains(TEXT("TRAINING_AUDIO event=MissionEnd sound=Siren_mission_end"))) Ends.Increment();
        if (Text.Contains(TEXT("TRAINING_AUDIO event=Footstep sound=Footsteps_Walk"))) Steps.Increment();
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBTrainingSoundTest,"OperationBreakpoint.Training.PlaySoundLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOBTrainingSoundTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->bAllowAudioPlayback=true;
    if (auto* Manager=GEngine->GetAudioDeviceManager())
    {
        FAudioDeviceParams Params; Params.AssociatedWorld=World; Params.Scope=EAudioDeviceScope::Shared;
        World->SetAudioDevice(Manager->RequestAudioDevice(Params));
    }
    auto* Director=World->SpawnActor<AOBTrainingDirector>();
    FTrainingSoundLog Feedback;
    GLog->AddOutputDevice(&Feedback);
    ON_SCOPE_EXIT
    {
        Director->EndPlay(EEndPlayReason::Destroyed);
        GLog->RemoveOutputDevice(&Feedback);
        World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    };
    if (!TestTrue(TEXT("Lifecycle verification uses a real audio device"),GEngine->UseSound() && World->GetAudioDevice().IsValid())) return false;
    auto* Player=World->SpawnActor<AOBPlayerCharacter>();
    auto* PC=World->SpawnActor<AOBPlayerController>(); PC->Possess(Player);
    auto* Spline=Player->GetPlayerSplineComponent();
    Spline->PlayerCharacter=Player; Spline->PlayerCamera=Player->GetPlayerCamera();
    Director->Controller=PC; Director->Action=Player->GetActionComponent(); Director->Spline=Spline;
    Director->bInitialized=true; Director->bQASession=true;
    Director->Result.SessionId=FGuid::NewGuid();
    Director->AccuracyTarget=World->SpawnActor<AOBTrainingTarget>();
    for (int32 I=0;I<6;++I) Director->TimedTargets.Add(World->SpawnActor<AOBTrainingTarget>());
    Director->StartTrainingAudio();
    auto* Music=Director->TrainingMusicAudio.Get();
    if (!TestNotNull(TEXT("Background audio component exists"),Music)) return false;
    TestTrue(TEXT("Music starts and follows world pause"),Music->IsPlaying() && !Music->bIsUISound);
    const auto* Loop=Cast<USoundNodeWavePlayer>(Director->TrainingMusicLoop->FirstNode);
    TestTrue(TEXT("Music loops inside the renderer beyond its 60-second source"),Loop && Loop->bLooping && Director->TrainingMusicLoop->IsLooping());
    Director->StartTrainingAudio();
    TestTrue(TEXT("Repeated initialization cannot stack background tracks"),Director->TrainingMusicAudio==Music);

    Spline->RailState=UOBPlayerSplineComponent::EOBRailState::Moving;
    Director->UpdateTrainingAudio(.01f);
    TestEqual(TEXT("Moving starts a walking step"),Feedback.Steps.GetValue(),1);
    Director->UpdateTrainingAudio(.1f);
    TestEqual(TEXT("Step cadence does not retrigger every frame"),Feedback.Steps.GetValue(),1);
    Spline->RailState=UOBPlayerSplineComponent::EOBRailState::TurningAtTrainingStop;
    Director->UpdateTrainingAudio(.6f);
    TestTrue(TEXT("Turning stops footsteps immediately"),Director->FootstepAudio==nullptr);
    TestEqual(TEXT("Turning is not walking"),Feedback.Steps.GetValue(),1);
    Spline->RailState=UOBPlayerSplineComponent::EOBRailState::Moving;
    Director->bPrototypePaused=true; Director->UpdateTrainingAudio(.6f);
    TestEqual(TEXT("Basic pause does not emit footsteps or end siren"),Feedback.Steps.GetValue()+Feedback.Ends.GetValue(),1);
    Director->bPrototypePaused=false;

    Director->PrototypeNarrationAudio=UGameplayStatics::CreateSound2D(Director,Director->WalkingSound.Get());
    Director->PrototypeNarrationAudio->Play(); Director->UpdateTrainingAudio(.3f);
    TestTrue(TEXT("Narration ducks music"),Director->CurrentMusicVolume<=Director->MusicVoiceVolume+.001f);
    Director->PrototypeNarrationAudio->Stop(); Director->PrototypeNarrationAudio=nullptr;
    Director->UpdateTrainingAudio(.3f);
    TestTrue(TEXT("Music recovers after narration"),FMath::IsNearlyEqual(Director->CurrentMusicVolume,Director->MusicVolume));

    auto* Target=Director->TimedTargets[0].Get();
    Target->SetTargetEnabled(false,true);
    TestEqual(TEXT("Initial/QA reset is silent"),Feedback.Drops.GetValue(),0);
    Target->SetTargetEnabled(true,true);
    Target->HandleScoringHit(Target->GetActorLocation());
    Target->SetTargetEnabled(false,false,true);
    Target->SetTargetEnabled(false,false,true);
    TestEqual(TEXT("A target impact plays one hit"),Feedback.Hits.GetValue(),1);
    TestEqual(TEXT("A repeated lowering request cannot duplicate its sound"),Feedback.Drops.GetValue(),1);
    auto* Rail=World->SpawnActor<AOBRailTrainingTarget>();
    Rail->HandleScoringHit(Rail->GetActorLocation());
    Rail->SetTargetEnabled(false,true); // End-of-group cleanup while the rail is folding.
    TestEqual(TEXT("Rail impact also plays once"),Feedback.Hits.GetValue(),2);
    TestEqual(TEXT("Rail hit folding emits one down sound despite cleanup"),Feedback.Drops.GetValue(),2);

    Spline->RailState=UOBPlayerSplineComponent::EOBRailState::Action;
    Director->bUseSplinePrototype=true; Director->PrototypeRouteIndex=3;
    Director->CurrentStep=EOBTrainingStep::Countdown; Director->bPrototypeMockStartPending=true;
    Director->StartPrototypeMockTraining(); Director->StartPrototypeMockTraining();
    TestEqual(TEXT("Mock start signal plays once"),Feedback.Starts.GetValue(),1);
    TestEqual(TEXT("Start signal enables actual timed shooting"),Director->CurrentStep,EOBTrainingStep::TimedTargets);
    Director->FinishTraining(false,TEXT("Audio lifecycle verification"));
    Director->FinishTraining(false);
    TestEqual(TEXT("Scenario completion plays exactly one mission-end signal"),Feedback.Ends.GetValue(),1);
    TestTrue(TEXT("Result stops walking"),Director->FootstepAudio==nullptr);
    Director->StopTrainingAudio(false);
    TestTrue(TEXT("Teardown releases music and signal components"),!Director->TrainingMusicAudio && !Director->TrainingSignalAudio && !Director->TrainingMusicLoop);
    return !HasAnyErrors();
}
#endif

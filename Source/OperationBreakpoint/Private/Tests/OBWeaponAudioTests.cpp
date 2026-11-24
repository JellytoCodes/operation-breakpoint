#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "HAL/ThreadSafeCounter.h"
#include "Core/OBPlayerController.h"
#include "Core/OBWeaponFeedback.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBActionComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "InputKeyEventArgs.h"
#include "Sound/SoundBase.h"

namespace
{
class FWeaponAudioLog : public FOutputDevice
{
public:
    FThreadSafeCounter Fire, Empty, Reload, ImmediateReload;
    bool CanBeUsedOnAnyThread() const override { return true; }
    bool CanBeUsedOnMultipleThreads() const override { return true; }
    void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override
    {
        if (Category != TEXT("LogOBShooting")) return;
        const FString Text(Message);
        if (Text.Contains(TEXT("Weapon feedback=Fire sound=Shooting"))) Fire.Increment();
        if (Text.Contains(TEXT("Weapon feedback=Empty sound=Shooting_EmptyMagazine"))) Empty.Increment();
        if (Text.Contains(TEXT("Weapon feedback=Reload sound=Reload"))) Reload.Increment();
        if (Text.Contains(TEXT("Weapon feedback=Reload sound=Reload start_seconds=0.565"))) ImmediateReload.Increment();
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBWeaponAudioTest, "OperationBreakpoint.Training.WeaponAudio",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOBWeaponAudioTest::RunTest(const FString&)
{
    UClass* Class = LoadClass<AOBPlayerController>(nullptr, TEXT("/Game/Blueprints/Player/BP_OBPlayerController.BP_OBPlayerController_C"));
    if (!TestNotNull(TEXT("Actual player controller BP loads"), Class)) return false;
    auto* Defaults = Cast<AOBPlayerController>(Class->GetDefaultObject());
    auto* Fire = Defaults->FireSound.LoadSynchronous();
    auto* Empty = Defaults->EmptyFireSound.LoadSynchronous();
    auto* Reload = Defaults->ReloadSound.LoadSynchronous();
    if (!TestNotNull(TEXT("Live-fire effect is assigned and loadable"), Fire) ||
        !TestNotNull(TEXT("Empty-fire effect is assigned and loadable"), Empty) ||
        !TestNotNull(TEXT("Reload effect is assigned and loadable"), Reload)) return false;
    TestTrue(TEXT("The two feedback sounds are distinct and audible-length assets"),
        Fire != Empty && Fire->GetDuration() > 0 && Empty->GetDuration() > 0);
    TestTrue(TEXT("Reload has a distinct confirmation sound"), Reload != Fire && Reload != Empty && Reload->GetDuration() > 0);
    TestTrue(TEXT("Known reload asset skips its quiet introduction"),FMath::IsNearlyEqual(OBWeaponFeedback::ReloadStartTime(Reload),.565f));
    TestEqual(TEXT("Other feedback assets keep their original onset"),OBWeaponFeedback::ReloadStartTime(Fire),0.f);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    FWeaponAudioLog Feedback;
    GLog->AddOutputDevice(&Feedback);
    ON_SCOPE_EXIT { GLog->RemoveOutputDevice(&Feedback); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Player = World->SpawnActor<AOBPlayerCharacter>();
    auto* PC = World->SpawnActor<AOBPlayerController>(Class);
    PC->Possess(Player);
    Player->GetActionComponent()->SetExternalActionState(true);
    PC->SetTrainingControls(true, true, true, false);
    PC->SetTrainingAmmo(3, 3);
    int32 Shots = 0;
    int32 Reloads = 0;
    PC->OnShotResolved.AddLambda([&](const FHitResult&) { ++Shots; });
    PC->OnReloadCompleted.AddLambda([&]() { ++Reloads; });
    const auto Key = [PC](FKey Button, EInputEvent Event)
    {
        PC->InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0),
            Button, Event, FPlatformTime::Cycles64()));
    };
    const FVector Start(0,0,2000), End(1000,0,2000);
    for (int32 I=0; I<3; ++I) PC->FireAlongRay(Start, End);
    TestEqual(TEXT("Each actual live round requests one fire sound"), Feedback.Fire.GetValue(), 3);
    TestEqual(TEXT("Live rounds still consume ammo and resolve shots"), Shots, 3);
    PC->FireAlongRay(Start, End);
    TestEqual(TEXT("Empty ray fire requests a click instead"), Feedback.Empty.GetValue(), 1);
    TestEqual(TEXT("Empty trigger cannot score a shot"), Shots, 3);
    TestEqual(TEXT("Empty magazine never becomes negative"), PC->GetCurrentAmmo(), 0);
    PC->FireAtCursor();
    TestEqual(TEXT("Empty mouse/gun trigger clicks before cursor ray lookup"), Feedback.Empty.GetValue(), 2);
    PC->SetTrainingControls(true, false, true, false);
    PC->FireAlongRay(Start, End);
    TestEqual(TEXT("Reload stage gives empty feedback while shooting remains disabled"), Feedback.Empty.GetValue(), 3);
    PC->SetTrainingControls(true, false, false, false);
    PC->FireAlongRay(Start, End);
    TestEqual(TEXT("Guide-only trigger stays silent"), Feedback.Empty.GetValue(), 3);
    Key(EKeys::RightMouseButton, IE_Pressed);
    TestEqual(TEXT("Disallowed reload stays silent"), Feedback.Reload.GetValue(), 0);
    PC->SetTrainingControls(true, true, true, false);
    Player->GetActionComponent()->SetExternalActionState(false);
    PC->FireAlongRay(Start, End);
    TestEqual(TEXT("Prepare blocks weapon sounds"), Feedback.Empty.GetValue(), 3);
    Key(EKeys::RightMouseButton, IE_Pressed);
    TestEqual(TEXT("Prepare cannot produce reload confirmation"), Feedback.Reload.GetValue(), 0);
    Player->GetActionComponent()->SetExternalActionState(true);
    Key(EKeys::R, IE_Pressed);
    Key(EKeys::R, IE_Released);
    TestEqual(TEXT("The former R key no longer refills the magazine"), PC->GetCurrentAmmo(), 0);
    Key(EKeys::RightMouseButton, IE_Pressed);
    TestEqual(TEXT("Successful ammo refill plays one reload sound"), Feedback.Reload.GetValue(), 1);
    TestEqual(TEXT("Actual gameplay reload requests the immediate audible section"),Feedback.ImmediateReload.GetValue(),1);
    TestEqual(TEXT("Reload confirmation corresponds to a full magazine"), PC->GetCurrentAmmo(), 3);
    Key(EKeys::RightMouseButton, IE_Repeat);
    Key(EKeys::RightMouseButton, IE_Released);
    TestEqual(TEXT("A right-click hold and release complete only one reload"), Reloads, 1);
    PC->CompleteReload();
    TestEqual(TEXT("Already-full magazine does not repeat refill confirmation"), Feedback.Reload.GetValue(), 1);
    PC->FireAlongRay(Start, End);
    TestEqual(TEXT("Reloaded magazine resumes the normal fire effect"), Feedback.Fire.GetValue(), 4);
    TestEqual(TEXT("Reloaded fire resolves a real shot again"), Shots, 4);
    return true;
}
#endif

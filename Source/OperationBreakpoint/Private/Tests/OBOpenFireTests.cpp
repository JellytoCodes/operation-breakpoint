#if WITH_DEV_AUTOMATION_TESTS
#include "Input/OBOpenFireTypes.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/ScopeExit.h"
#include "Core/OBPlayerController.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBActionComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "InputKeyEventArgs.h"
#include "Input/OBOpenFireSubsystem.h"
#include "Engine/GameInstance.h"
#include "Subsystems/SubsystemCollection.h"
#include "UObject/StrongObjectPtr.h"
#include "OSCAddress.h"
#include "OSCClient.h"
#include "OSCManager.h"
#include "OSCMessage.h"

namespace
{
FString FrameJson()
{
    return TEXT("{\"v\":1,\"session\":\"test\",\"device\":\"gun\",\"sequence\":1,\"x\":0.25,\"y\":0.75,"
        "\"connected\":true,\"aim_valid\":true,\"demo\":false,\"calibrated\":true,\"trigger\":false,\"reload\":false,"
        "\"calibrate\":false,\"trigger_count\":0,\"reload_count\":0,\"calibrate_count\":0}");
}
FOBOpenFireFrame Sample()
{
    FOBOpenFireFrame Frame;
    FOBOpenFireFrame::Decode(FrameJson(), Frame);
    return Frame;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBOpenFireProtocolTest, "OperationBreakpoint.OpenFire.Protocol",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOBOpenFireProtocolTest::RunTest(const FString&)
{
    FOBOpenFireFrame Frame;
    TestTrue(TEXT("Decode tool's full-state input"), FOBOpenFireFrame::Decode(FrameJson(), Frame));
    TestTrue(TEXT("Optional physical Y offset is accepted"),FOBOpenFireFrame::Decode(
        FrameJson().Replace(TEXT("\"x\":0.25"),TEXT("\"aim_y_up\":0.125,\"x\":0.25")),Frame));
    TestEqual(TEXT("Physical offset metadata survives decoding"),Frame.AimYOffsetUp,.125);
    TestFalse(TEXT("Invalid physical offset metadata is rejected"),FOBOpenFireFrame::Decode(
        FrameJson().Replace(TEXT("\"x\":0.25"),TEXT("\"aim_y_up\":3,\"x\":0.25")),Frame));
    TestTrue(TEXT("Normalized coordinates map directly to the viewport"), Frame.ViewportPixel(FIntPoint(1921, 1081)).Equals(FVector2D(480, 810)));
    const TArray<TPair<FString, FString>> Replacements = {
        {TEXT("\"v\":1"), TEXT("\"v\":2")},
        {TEXT("\"sequence\":1"), TEXT("\"sequence\":1.5")},
        {TEXT("\"sequence\":1"), TEXT("\"sequence\":\"1\"")},
        {TEXT("\"trigger\":false"), TEXT("\"trigger\":\"false\"")},
        {TEXT("\"trigger_count\":0"), TEXT("\"trigger_count\":-1")},
        {TEXT("\"x\":0.25"), TEXT("\"x\":1e999")},
        {TEXT("\"connected\":true"), TEXT("\"connected\":false")},
        {TEXT("\"demo\":false,"), TEXT("")},
        {TEXT("\"device\":\"gun\""), TEXT("\"device\":\"\"")}
    };
    for (const auto& Pair : Replacements)
        TestFalse(TEXT("Malformed or inconsistent packet is rejected"), FOBOpenFireFrame::Decode(FrameJson().Replace(*Pair.Key, *Pair.Value), Frame));
    Frame = Sample(); Frame.Position.X = -.01;
    TestFalse(TEXT("Off-screen aim does not clamp to an edge target"), Frame.IsOnScreen());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBOpenFireStreamTest, "OperationBreakpoint.OpenFire.StreamLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOBOpenFireStreamTest::RunTest(const FString&)
{
    FOBOpenFireState State;
    auto Frame = Sample();
    Frame.bTrigger = true; Frame.TriggerCount = 4;
    TestTrue(TEXT("First held sample establishes a baseline"), State.Accept(Frame, 1., false));
    TestFalse(TEXT("Connection cannot fire a held trigger"), State.Events.bTriggerPressed || State.Events.bTriggerHeld);
    TestFalse(TEXT("Duplicate packet rejected"), State.Accept(Frame, 1.01, false));
    Frame.Sequence++; Frame.bTrigger = false;
    State.Accept(Frame, 1.02, false);
    Frame.Sequence++; Frame.bTrigger = true; Frame.TriggerCount++;
    State.Accept(Frame, 1.03, false);
    TestTrue(TEXT("A new trigger produces one press"), State.Events.bTriggerPressed && State.Events.bTriggerHeld);
    Frame.Sequence++;
    State.Accept(Frame, 1.04, false);
    TestFalse(TEXT("Heartbeats cannot repeat a shot"), State.Events.bTriggerPressed);
    Frame.Sequence++; Frame.bTrigger = false; Frame.TriggerCount++;
    State.Accept(Frame, 1.05, false);
    TestTrue(TEXT("Counter retains a tap when down packet is missing"), State.Events.bTriggerPressed && !State.Events.bTriggerHeld);
    auto Other = Frame; Other.Session = TEXT("other"); Other.Device = TEXT("other-gun");
    TestFalse(TEXT("A second source cannot replace an active gun"), State.Accept(Other, 1.06, false));
    TestFalse(TEXT("Bridge timeout suppresses stale aim"), State.CanAim(1.5));
    Frame.Sequence++; Frame.bTrigger = true; Frame.TriggerCount++;
    State.Accept(Frame, 1.6, false);
    TestFalse(TEXT("Returning from timeout does not fire a held trigger"), State.Events.bTriggerPressed || State.Events.bTriggerHeld);
    Frame.Sequence++; Frame.bConnected = false; Frame.bAimValid = false; Frame.bTrigger = false;
    State.Accept(Frame, 1.61, false);
    TestFalse(TEXT("Explicit stop invalidates aim immediately"), State.CanAim(1.61));
    Other.bDemo = true;
    TestFalse(TEXT("Synthetic input is rejected in hardware mode"), State.Accept(Other, 1.62, false));
    TestTrue(TEXT("Synthetic input requires explicit opt-in"), State.Accept(Other, 1.62, true));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBOpenFireControllerRoutingTest, "OperationBreakpoint.OpenFire.ControllerRouting",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOBOpenFireControllerRoutingTest::RunTest(const FString&)
{
    const FString OldCommandLine(FCommandLine::Get());
    FCommandLine::Set(*(OldCommandLine + TEXT(" -OBOpenFire")));
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); FCommandLine::Set(*OldCommandLine); };
    auto* Player = World->SpawnActor<AOBPlayerCharacter>();
    auto* Controller = World->SpawnActor<AOBPlayerController>();
    Controller->Possess(Player);
    Player->GetActionComponent()->SetExternalActionState(true);
    Controller->SetTrainingControls(true, true, true, false);
    Controller->SetTrainingAmmo(1, 3);
    int32 Triggers = 0, Reloads = 0, Calibrations = 0, Shots = 0;
    Controller->OnTriggerPressed.AddLambda([&] { ++Triggers; });
    Controller->OnReloadCompleted.AddLambda([&] { ++Reloads; });
    Controller->OnAimCalibrationCompleted.AddLambda([&] { ++Calibrations; });
    Controller->OnShotResolved.AddLambda([&](const FHitResult&) { ++Shots; });
    Controller->ApplyOpenFireInput(false, false, false, false);
    Controller->ApplyOpenFireInput(true, true, false, false);
    TestEqual(TEXT("A combat press without aim cannot advance training"), Triggers, 0);
    TestTrue(TEXT("Trigger hold reaches existing UI input state"), Controller->IsTriggerHeld());
    Controller->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton, IE_Pressed, 1.f));
    TestEqual(TEXT("Duplicate native HID click cannot fire a second trigger"), Triggers, 0);
    TestEqual(TEXT("Missing or stale coordinates cannot resolve a shot"), Shots, 0);
    TestEqual(TEXT("Missing coordinates cannot consume ammo"), Controller->GetCurrentAmmo(), 1);
    Controller->ApplyOpenFireInput(false, false, true, true);
    TestFalse(TEXT("A stop releases the held trigger"), Controller->IsTriggerHeld());
    TestEqual(TEXT("Reload reaches existing training controls"), Reloads, 1);
    TestEqual(TEXT("Reload refills the existing ammo state"), Controller->GetCurrentAmmo(), 3);
    TestEqual(TEXT("Calibration completion reaches existing training flow"), Calibrations, 1);
    return true;
}

// Exercise the actual OSC socket, parsing, game-thread dispatch, state updates,
// timeout and stop path. This is synthetic input, never hardware validation.
class FOBOpenFireLoopbackCommand : public IAutomationLatentCommand
{
public:
    explicit FOBOpenFireLoopbackCommand(FAutomationTestBase* InTest) : Test(InTest) {}
    ~FOBOpenFireLoopbackCommand() { if (Receiver.IsValid()) Receiver->Deinitialize(); }
    virtual bool Update() override
    {
        const double Now = FPlatformTime::Seconds();
        if (Stage == 0)
        {
            Started = Now;
            const int32 Port = 19011;
            const FString Previous(FCommandLine::Get());
            FCommandLine::Set(*FString::Printf(TEXT("-OBOpenFireDemo -OBOpenFirePort=%d"), Port));
            Instance.Reset(NewObject<UGameInstance>(GEngine));
            Receiver.Reset(NewObject<UOBOpenFireSubsystem>(Instance.Get()));
            Receiver->Initialize(Collection);
            FCommandLine::Set(*Previous);
            Client.Reset(UOSCManager::CreateOSCClient(TEXT("127.0.0.1"), Port, TEXT("OBOpenFireSyntheticTest"), Instance.Get()));
            Send(1, false, 0, true);
            Stage = 1;
            return false;
        }
        if (Now-Started > 5.)
        {
            Test->AddError(TEXT("Actual loopback receiver did not complete its input/stop sequence"));
            return true;
        }
        const auto& State = Receiver->GetState();
        if (Stage == 1 && State.bHasFrame && State.Frame.Sequence == 1)
        {
            FVector2D Aim;
            Test->TestTrue(TEXT("OSC loopback delivers the calibrated normalized position"),
                Receiver->GetAimPosition(Aim) && Aim.Equals(FVector2D(.25, .75)));
            Send(2, true, 1, true);
            Stage = 2;
        }
        else if (Stage == 2 && State.Frame.Sequence == 2)
        {
            Test->TestTrue(TEXT("Real socket press reaches button events"), State.Events.bTriggerPressed && State.Events.bTriggerHeld);
            PressAt = Now;
            Stage = 3;
        }
        else if (Stage == 3 && Now-PressAt > .45)
        {
            FVector2D Aim;
            Test->TestFalse(TEXT("A stopped sender cannot retain a fresh live ray"), Receiver->GetAimPosition(Aim));
            Send(3, true, 2, true);
            Stage = 4;
        }
        else if (Stage == 4 && State.Frame.Sequence == 3)
        {
            Test->TestFalse(TEXT("Socket reconnect with held trigger is disarmed"), State.Events.bTriggerPressed || State.Events.bTriggerHeld);
            Send(4, false, 2, false);
            Stage = 5;
        }
        else if (Stage == 5 && State.Frame.Sequence == 4)
        {
            Test->TestFalse(TEXT("Tool stop packet immediately disconnects receiver"), Receiver->IsConnected());
            return true;
        }
        return false;
    }
private:
    void Send(int32 Sequence, bool bHeld, int32 Count, bool bConnected)
    {
        FString Json = FrameJson().Replace(TEXT("\"demo\":false"), TEXT("\"demo\":true"))
            .Replace(TEXT("\"sequence\":1"), *FString::Printf(TEXT("\"sequence\":%d"), Sequence))
            .Replace(TEXT("\"trigger_count\":0"), *FString::Printf(TEXT("\"trigger_count\":%d"), Count));
        if (bHeld) Json = Json.Replace(TEXT("\"trigger\":false"), TEXT("\"trigger\":true"));
        if (!bConnected) Json = Json.Replace(TEXT("\"connected\":true"), TEXT("\"connected\":false"))
            .Replace(TEXT("\"aim_valid\":true"), TEXT("\"aim_valid\":false"));
        FOSCMessage Message;
        Message.SetAddress(FOSCAddress(TEXT("/ob/openfire/input")));
        UOSCManager::AddString(Message, Json);
        Client->SendOSCMessage(Message);
    }
    FAutomationTestBase* Test;
    int32 Stage = 0;
    double Started = 0, PressAt = 0;
    FSubsystemCollection<UGameInstanceSubsystem> Collection;
    TStrongObjectPtr<UGameInstance> Instance;
    TStrongObjectPtr<UOBOpenFireSubsystem> Receiver;
    TStrongObjectPtr<UOSCClient> Client;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBOpenFireLoopbackTest, "OperationBreakpoint.OpenFire.ActualOSCLoopback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOBOpenFireLoopbackTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FOBOpenFireLoopbackCommand(this));
    return true;
}
#endif

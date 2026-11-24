#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MediaPipe/OBBreachGuideTypes.h"
#include "MediaPipe/OBMediaPipeOSCSubsystem.h"
#include "OSCManager.h"
#include "OSCAddress.h"
#include "OSCMessage.h"
#include "Engine/GameInstance.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBBreachGuideTest, "OperationBreakpoint.Training.BreachGuide",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOBBreachGuideTest::RunTest(const FString&)
{
    const FBox2D Frame = FOBBreachGuideLayout::CameraRect(FVector2D(1920, 1080), FVector2D(640, 480));
    TestEqual(TEXT("4:3 camera centered in 16:9 viewport"), Frame.Min, FVector2D(240, 0));
    TestEqual(TEXT("4:3 camera retains aspect ratio"), Frame.Max, FVector2D(1680, 1080));
    const FBox2D Wide = FOBBreachGuideLayout::CameraRect(FVector2D(1920, 1080), FVector2D(1280, 720));
    TestEqual(TEXT("Same aspect ratio fills viewport"), Wide.GetSize(), FVector2D(1920, 1080));
    const FBox2D Portrait = FOBBreachGuideLayout::CameraRect(FVector2D(1920, 1080), FVector2D(480, 640));
    TestEqual(TEXT("Portrait camera retains its width"), Portrait.GetSize(), FVector2D(810, 1080));
    const FBox2D Ultrawide = FOBBreachGuideLayout::CameraRect(FVector2D(2560, 1080), FVector2D(640, 480));
    TestEqual(TEXT("Ultrawide horizontal offset"), Ultrawide.Min, FVector2D(560, 0));
    const FBox2D TallViewport = FOBBreachGuideLayout::CameraRect(FVector2D(1080, 1920), FVector2D(1920, 1080));
    TestTrue(TEXT("Landscape camera letterboxed in portrait viewport"), TallViewport.Min.Y > 0 && TallViewport.GetSize().X == 1080);
    TestEqual(TEXT("Zero viewport safe"), FOBBreachGuideLayout::CameraRect(FVector2D::ZeroVector, FVector2D(640, 480)).GetSize(), FVector2D::ZeroVector);
    TestEqual(TEXT("Non-finite camera safe"), FOBBreachGuideLayout::CameraRect(FVector2D(1920, 1080), FVector2D(std::numeric_limits<double>::quiet_NaN(), 480)).GetSize(), FVector2D::ZeroVector);
    const FVector2D HandUV(0.7, 0.45);
    const FBox2D DpiFrame = FOBBreachGuideLayout::CameraRect(FVector2D(1280, 720), FVector2D(640, 480));
    TestTrue(TEXT("DPI 1.5 maps the same physical hand position"), (FOBBreachGuideLayout::MapPoint(HandUV, DpiFrame) * 1.5).Equals(FOBBreachGuideLayout::MapPoint(HandUV, Frame), 0.001));

    TArray<float> Packet = {1, 640, 480, 1, 1, 1,
        0.6475f, 0.3875f, 0.7825f, 0.5625f, 0.2175f, 0.3875f, 0.3525f, 0.5625f,
        0.7f, 0.45f, 0.9f, 0.28f, 0.45f, 0.9f, 1, 1, 1, 1};
    FOBBreachGuideState State;
    TestTrue(TEXT("Atomic feedback decoded"), FOBBreachGuideLayout::DecodePacket(Packet, State));
    TestTrue(TEXT("Both hands ready with mirrored source"), State.bReady && State.bMirrored);
    TestTrue(TEXT("Post-mirror hand coordinates not mirrored a second time"), FMath::IsNearlyEqual(FOBBreachGuideLayout::MapPoint(State.LeftWrist, Frame).X, 1248.0, 0.01));
    const FBox2D LeftBox(FOBBreachGuideLayout::MapPoint(State.LeftMin, Frame), FOBBreachGuideLayout::MapPoint(State.LeftMax, Frame));
    TestTrue(TEXT("Detected hand drawn inside identical box"), LeftBox.IsInside(FOBBreachGuideLayout::MapPoint(State.LeftWrist, Frame)));
    TestEqual(TEXT("Inclusive detector boundary maps to box boundary"), FOBBreachGuideLayout::MapPoint(State.LeftMin, Frame), LeftBox.Min);
    auto Bad = Packet; Bad[8] = Bad[6];
    TestFalse(TEXT("Collapsed ROI rejected"), FOBBreachGuideLayout::DecodePacket(Bad, State));
    TestTrue(TEXT("Invalid packet preserves last valid snapshot"), State.bReady);
    Bad = Packet; Bad.Pop();
    TestFalse(TEXT("Partial packet rejected"), FOBBreachGuideLayout::DecodePacket(Bad, State));
    Bad = Packet; Bad[14] = std::numeric_limits<float>::quiet_NaN();
    TestFalse(TEXT("Non-finite landmark rejected"), FOBBreachGuideLayout::DecodePacket(Bad, State));
    Bad = Packet; Bad[1] = 0;
    TestFalse(TEXT("Zero camera width rejected"), FOBBreachGuideLayout::DecodePacket(Bad, State));
    Bad = Packet; Bad[3] = 2;
    TestFalse(TEXT("Malformed flag rejected"), FOBBreachGuideLayout::DecodePacket(Bad, State));
    Bad = Packet; Bad[16] = 0;
    TestTrue(TEXT("Low-visibility snapshot decoded"), FOBBreachGuideLayout::DecodePacket(Bad, State));
    TestFalse(TEXT("Missing left hand clears ready"), State.bLeftInside || State.bReady);
    TestEqual(TEXT("Missing hand clears progress"), State.Progress, 0.f);
    Bad = Packet; Bad[5] = 0;
    FOBBreachGuideLayout::DecodePacket(Bad, State);
    TestFalse(TEXT("Lost tracking clears both hands and success"), State.bReady || State.bLeftInside || State.bRightInside || State.LeftVisibility > 0 || State.RightVisibility > 0);
    Bad = Packet; Bad[4] = 2;
    FOBBreachGuideLayout::DecodePacket(Bad, State);
    TestFalse(TEXT("Other motion modes cannot show breach success"), State.bReady || State.bBreachMode);

    auto* Instance = NewObject<UGameInstance>();
    auto* Motion = NewObject<UOBMediaPipeOSCSubsystem>(Instance);
    TestFalse(TEXT("Before first packet feedback is unavailable"), Motion->GetBreachGuideState().bFresh);
    FOSCMessage Message;
    UOSCManager::SetOSCMessageAddress(Message, FOSCAddress(TEXT("/mediapipe/breach/guide")));
    for (float Value : Packet) UOSCManager::AddFloat(Message, Value);
    Motion->HandleOSCMessage(Message, TEXT("127.0.0.1"), 8000);
    State = Motion->GetBreachGuideState();
    TestTrue(TEXT("OSC dispatch delivers fresh feedback"), State.bFresh && State.bReady);
    TestFalse(TEXT("Feedback alone never triggers gameplay breach"), Motion->IsBreachRecognized());
    Motion->LastBreachGuideTime = FPlatformTime::Seconds() - 1;
    State = Motion->GetBreachGuideState();
    TestFalse(TEXT("Stale connection clears success and hand markers"), State.bFresh || State.bReady || State.bPlayerTracked || State.LeftVisibility > 0 || State.RightVisibility > 0);
    TestEqual(TEXT("Stale connection clears progress"), State.Progress, 0.f);
    TestEqual(TEXT("Stale guide retains camera geometry"), State.CameraSize, FVector2D(640, 480));
    return true;
}
#endif

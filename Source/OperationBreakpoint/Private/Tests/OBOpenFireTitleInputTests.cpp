#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Widget/OBTitleWidget.h"
#include "Input/OBOpenFireTypes.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBOpenFireTitleInputTest,"OperationBreakpoint.OpenFire.TitleInput",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOBOpenFireTitleInputTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false);GEngine->DestroyWorldContext(World); };
    auto* Class=LoadClass<UOBTitleWidget>(nullptr,TEXT("/Game/Blueprints/Widget/WBP_Title.WBP_Title_C"));
    if(!TestNotNull(TEXT("Title Blueprint"),Class))return false;
    auto* Title=CreateWidget<UOBTitleWidget>(World,Class);
    // Keep video initialization outside this input test; the package smoke checks playback.
    Title->Clips.SetNum(3);
    FOBOpenFireState State;FOBOpenFireFrame Frame;
    Frame.Session=TEXT("title-test");Frame.Device=TEXT("SIMULATED_TITLE_INPUT");
    Frame.bConnected=true;Frame.bAimValid=false;Frame.bInteractionTrigger=true;Frame.InteractionTriggerCount=1;Frame.Sequence=1;
    State.Accept(Frame,FPlatformTime::Seconds(),false);Title->HandleOpenFireStart(State,true);
    TestFalse(TEXT("A trigger already held on entry cannot start the title"),Title->bStartRequested);
    Frame.Sequence++;Frame.bInteractionTrigger=false;State.Accept(Frame,FPlatformTime::Seconds(),false);Title->HandleOpenFireStart(State,true);
    Frame.Sequence++;Frame.bInteractionTrigger=true;Frame.InteractionTriggerCount++;State.Accept(Frame,FPlatformTime::Seconds(),false);Title->HandleOpenFireStart(State,false);
    TestFalse(TEXT("An unfocused window cannot start from physical input"),Title->bStartRequested);
    Title->HandleOpenFireStart(State,true);
    TestFalse(TEXT("Focus restoration with a held trigger stays disarmed"),Title->bStartRequested);
    Frame.Sequence++;Frame.bInteractionTrigger=false;State.Accept(Frame,FPlatformTime::Seconds(),false);Title->HandleOpenFireStart(State,true);
    Frame.Sequence++;Frame.InteractionTriggerCount++;State.Accept(Frame,FPlatformTime::Seconds(),false);Title->HandleOpenFireStart(State,true);
    TestTrue(TEXT("A released UI tap starts the title with zero visible LEDs"),Title->bStartRequested);
    return !HasAnyErrors();
}
#endif

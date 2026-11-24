#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Stage/OBC4Placement.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBC4PlacementTest,"OperationBreakpoint.Stage.C4Placement",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOBC4PlacementTest::RunTest(const FString&)
{
    for (int32 Hand : {1,2})
    {
        FOBC4PlacementTracker P;P.Reset(1.);
        FOBBreachGuideState S;S.bFresh=S.bBreachMode=S.bPlayerTracked=true;
        auto& UV=Hand==1 ? S.LeftWrist : S.RightWrist;auto& Visibility=Hand==1 ? S.LeftVisibility : S.RightVisibility;
        UV=FVector2D(.2,.5);Visibility=.9f;
        TestFalse(TEXT("One visible hand outside the center cannot attach"),P.Update(S,1.1));
        TestTrue(TEXT("Either single hand is tracked without the other"),P.bTracked && P.Hand==Hand);
        UV=P.GetTargetCenter();P.Update(S,1.2);
        for (int32 I=0;I<30;++I) P.Update(S,1.2);
        TestTrue(TEXT("A frozen camera packet cannot accumulate a hold"),!P.bAttached && P.Progress==0);
        P.Update(S,1.3);TestTrue(TEXT("A new frame inside accumulates partial progress"),P.Progress>0 && !P.bAttached);
        UV=FVector2D(.8,.5);P.Update(S,1.4);TestEqual(TEXT("Leaving the target clears attachment progress"),P.Progress,0.f);
        UV=P.GetTargetCenter();P.Update(S,1.5);P.Update(S,1.6);
        S.bFresh=false;P.Update(S,1.6);TestTrue(TEXT("Tracking loss hides the charge and clears progress"),!P.bTracked && P.Progress==0);
        S.bFresh=true;P.Update(S,2.);P.Update(S,2.1);P.Update(S,2.2);
        TestTrue(TEXT("Only continuous fresh frames count toward attachment"),P.Progress>0 && !P.bAttached);
        TestTrue(TEXT("Either single hand can finish attaching C4"),P.Update(S,2.3) && P.bAttached);
        TestFalse(TEXT("Attachment is emitted only once"),P.Update(S,2.4));
    }
    FOBC4PlacementTracker P;P.Reset(0);
    FOBBreachGuideState Both;Both.bFresh=Both.bBreachMode=Both.bPlayerTracked=true;
    Both.LeftVisibility=Both.RightVisibility=1;Both.LeftWrist=FVector2D(.1,.5);Both.RightWrist=FVector2D(.9,.5);
    P.Update(Both,1);Both.RightWrist=FVector2D(.5,.5);P.Update(Both,1.1);
    TestTrue(TEXT("The charge stays with the same visible hand instead of jumping to the other"),P.Hand==1 && P.HandUV.X==.1 && P.Progress==0);
    Both.LeftVisibility=0;P.Update(Both,1.2);
    TestTrue(TEXT("The remaining visible hand can take over without inherited hold progress"),P.Hand==2 && P.Progress==0);
    Both.LeftVisibility=1;Both.LeftWrist=FVector2D(.2,.85);Both.RightWrist=FVector2D(.7,.4);P.Update(Both,1.3);
    TestTrue(TEXT("Raised right wrist wins while the left wrist remains visible below it"),P.Hand==2 && P.HandUV.Equals(Both.RightWrist));
    Both.LeftWrist=FVector2D(.3,.3);Both.RightWrist.Y=.85;P.Update(Both,1.4);
    TestTrue(TEXT("Raising the opposite hand switches C4 without inheriting attachment progress"),P.Hand==1 && P.HandUV.Equals(Both.LeftWrist) && P.Progress==0);
    Both.RightWrist.Y=.29;P.Update(Both,1.5);
    TestTrue(TEXT("Almost equal wrist heights do not flicker between hands"),P.Hand==1);
    Both.RightWrist.Y=.2;P.Update(Both,1.6);
    TestTrue(TEXT("A clearly higher right wrist takes over even though the left is still tracked"),P.Hand==2);
    FOBC4PlacementTracker First;First.Reset(0);Both.LeftWrist.Y=.9;Both.RightWrist.Y=.45;First.Update(Both,1.7);
    TestEqual(TEXT("Initial selection chooses the raised right hand rather than defaulting to left"),First.Hand,2);
    Both.bBreachMode=false;P.Update(Both,1.8);TestFalse(TEXT("Other camera modes cannot place C4"),P.bTracked);
    FOBC4PlacementTracker Raised;Raised.Reset(0);
    Both.bBreachMode=true;Both.LeftVisibility=1;Both.RightVisibility=0;
    TestTrue(TEXT("C4 target width and height are halved and its center is raised by 15 percent"),
        (Raised.TargetMax-Raised.TargetMin).Equals(FVector2D(.09,.11),.00001) && Raised.GetTargetCenter().Equals(FVector2D(.5,.35),.00001));
    double Sample=2.;
    for (FVector2D Outside : {FVector2D(.5,.5),FVector2D(.42,.4),FVector2D(.58,.4)})
    {
        Both.LeftWrist=Outside;
        for (int32 I=0;I<6;++I) { Sample+=.1;Raised.Update(Both,Sample); }
        TestTrue(TEXT("Old center and removed horizontal margins cannot plant C4"),Raised.Progress==0 && !Raised.bAttached);
    }
    return !HasAnyErrors();
}
#endif

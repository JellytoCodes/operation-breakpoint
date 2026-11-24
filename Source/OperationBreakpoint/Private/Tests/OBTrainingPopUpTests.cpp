#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Training/OBTrainingDirector.h"
#include "Training/OBTrainingTarget.h"
#include "Training/OBRailTrainingTarget.h"
#include "Training/OBTrainingDoor.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBActionComponent.h"
#include "Core/OBPlayerController.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "TimerManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBTrainingPopUpTest, "OperationBreakpoint.Training.PopUpTargets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOBTrainingPopUpTest::RunTest(const FString&)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Director = World->SpawnActor<AOBTrainingDirector>();
    ON_SCOPE_EXIT
    {
        Director->StopPopUpTargets();
        World->DestroyWorld(false);
        GEngine->DestroyWorldContext(World);
    };
    auto* HierarchyTarget = World->SpawnActor<AOBTrainingTarget>(FVector(5000, 1200, 80), FRotator(12, 65, 9));
    HierarchyTarget->SetActorScale3D(FVector(1.2f));
    HierarchyTarget->TargetPivot->SetRelativeLocation(FVector(0, 2.5f, 3.5f));
    HierarchyTarget->ArmMesh->SetRelativeLocation(FVector(0, -2.5f, -3.5f));
    HierarchyTarget->BoardMesh->SetRelativeLocation(FVector(0, 0, 90));
    HierarchyTarget->LowerSeconds = 0.5f;
    TestTrue(TEXT("Base mesh is the actor root"), HierarchyTarget->GetRootComponent() == HierarchyTarget->BaseMesh);
    TestTrue(TEXT("Pivot attaches directly to base"), HierarchyTarget->TargetPivot->GetAttachParent() == HierarchyTarget->BaseMesh);
    TestTrue(TEXT("Arm attaches directly to pivot"), HierarchyTarget->ArmMesh->GetAttachParent() == HierarchyTarget->TargetPivot);
    TestTrue(TEXT("Board attaches directly to pivot"), HierarchyTarget->BoardMesh->GetAttachParent() == HierarchyTarget->TargetPivot);
    const FTransform BaseTransform = HierarchyTarget->BaseMesh->GetComponentTransform();
    const FTransform RaisedBoard = HierarchyTarget->BoardMesh->GetComponentTransform();
    const FTransform ScoreOffset = HierarchyTarget->BoardMesh->GetRelativeTransform();
    HierarchyTarget->SetTargetEnabled(false);
    HierarchyTarget->Tick(0.25f);
    TestTrue(TEXT("Base stays fixed during folding"), HierarchyTarget->BaseMesh->GetComponentTransform().Equals(BaseTransform));
    TestTrue(TEXT("Actor transform is the stationary base transform"), HierarchyTarget->GetActorTransform().Equals(BaseTransform));
    TestFalse(TEXT("Board moves around the pivot"), HierarchyTarget->BoardMesh->GetComponentTransform().Equals(RaisedBoard));
    TestTrue(TEXT("Board offset stays attached to pivot"), HierarchyTarget->BoardMesh->GetRelativeTransform().Equals(ScoreOffset));
    HierarchyTarget->SetTargetEnabled(true, true);
    TestTrue(TEXT("Raising restores board pose"), HierarchyTarget->BoardMesh->GetComponentTransform().Equals(RaisedBoard));
    HierarchyTarget->Destroy();
    auto* Player = World->SpawnActor<AOBPlayerCharacter>();
    auto* PC = World->SpawnActor<AOBPlayerController>();
    PC->Possess(Player);
    Director->Controller = PC;
    Director->Action = Player->GetActionComponent();
    Director->Action->SetExternalActionState(true);
    PC->OnShotResolved.AddUObject(Director, &AOBTrainingDirector::HandleShotResolved);
    PC->SetTrainingControls(true, true, true);
    PC->SetTrainingAmmo(100, 100);
    Director->CurrentStep = EOBTrainingStep::TimedTargets;
    Director->bStepActive = true;
    Director->Deadline = 100;
    Director->TargetExposureSeconds = 0.15f;
    Director->TargetGapSeconds = 0.05f;
    for (int32 I = 0; I < 10; ++I)
    {
        AOBTrainingTarget* Target;
        if (I == 9)
        {
            auto* RailTarget = World->SpawnActor<AOBRailTrainingTarget>(FVector(3000, I * 150, 0), FRotator::ZeroRotator);
            RailTarget->StartMoving();
            Target = RailTarget;
        }
        else Target = World->SpawnActor<AOBTrainingTarget>(FVector(3000, I * 150, 0), FRotator::ZeroRotator);
        Target->RaiseSeconds = Target->LowerSeconds = 0.05f;
        Director->TimedTargets.Add(Target);
    }
    auto Fire = [&](AOBTrainingTarget* Target) {
        const FVector Aim = Target->BoardMesh->Bounds.Origin;
        const FVector Normal = Target->GetScoringPlaneNormal();
        PC->FireAlongRay(Aim + Normal * 100, Aim - Normal * 100);
    };
    auto Advance = [&](float Seconds) {
        const int32 Frames = FMath::CeilToInt(Seconds / 0.01f);
        for (int32 I = 0; I < Frames; ++I)
        {
            ++GFrameCounter;
            World->TimeSeconds += 0.01f;
            World->GetTimerManager().Tick(0.01f);
            int32 Raised = 0;
            for (AOBTrainingTarget* Target : Director->TimedTargets)
            {
                if (Target->IsActorTickEnabled()) Target->Tick(0.01f);
                Raised += Target->IsTargetRaised() ? 1 : 0;
            }
            if (Raised > 1) { AddError(TEXT("Several scoring targets are raised at once")); return; }
        }
    };
    Director->StartPopUpTargets();
    TestNull(TEXT("Raising animation has no scoring target"), Director->GetActiveTimedTarget());
    auto* First = Director->ActiveTimedTarget.Get();
    TestFalse(TEXT("Retracted stands stay visible"), Director->TimedTargets.Last()->IsHidden());
    Fire(First);
    TestEqual(TEXT("Moving board cannot score"), Director->Result.ValidHits, 0);
    Advance(0.06f);
    TestEqual(TEXT("First board is ready after its raise motion"), Director->GetActiveTimedTarget(), First);
    Fire(First);
    Fire(First);
    TestEqual(TEXT("Repeated active hit counts twice for accuracy"), Director->Result.ValidHits, 2);
    TestEqual(TEXT("Repeated active hit counts one distinct target"), Director->Result.UniqueTargetsHit, 1);
    const FHitResult StaleHit = [&]() {
        FHitResult Hit;
        const FVector Aim = First->BoardMesh->Bounds.Origin;
        const FVector Normal = First->GetScoringPlaneNormal();
        World->LineTraceSingleByChannel(Hit, Aim + Normal * 100, Aim - Normal * 100, ECC_Visibility);
        return Hit;
    }();
    Advance(0.17f);
    TestNull(TEXT("Exposure timeout retracts the active board"), Director->GetActiveTimedTarget());
    TestFalse(TEXT("Old trace cannot score after retraction"), First->IsScoringHit(StaleHit));
    Fire(First);
    TestEqual(TEXT("Retracted board is excluded from real traces"), Director->Result.ValidHits, 2);

    TSet<AOBTrainingTarget*> Round;
    Round.Add(First);
    AOBTrainingTarget* Last = First;
    int32 Appearances = 1;
    for (int32 Frame = 0; Frame < 1000 && Appearances < 11; ++Frame)
    {
        Advance(0.01f);
        auto* Active = Director->GetActiveTimedTarget();
        if (!Active || Active == Last) continue;
        if (Appearances < 10)
        {
            TestFalse(TEXT("No repeats within one shuffled round"), Round.Contains(Active));
            Round.Add(Active);
        }
        Fire(Active);
        Last = Active;
        ++Appearances;
    }
    TestEqual(TEXT("All ten targets appear in one round"), Round.Num(), 10);
    TestEqual(TEXT("Next round starts with another target"), Appearances, 11);
    TestEqual(TEXT("Actual scoring rays hit all distinct boards"), Director->Result.UniqueTargetsHit, 10);
    TestEqual(TEXT("Popup motion does not end the recorded phase early"), Director->CurrentStep, EOBTrainingStep::TimedTargets);
    Director->StopPopUpTargets();
    Advance(1);
    TestNull(TEXT("Stopping clears pending raise callbacks"), Director->GetActiveTimedTarget());
    TestFalse(TEXT("No popup timer remains after stopping"), World->GetTimerManager().IsTimerActive(Director->PopUpTimer));
    for (AOBTrainingTarget* Target : Director->TimedTargets)
        TestEqual(TEXT("Every stopped target is fully folded"), Target->GetPoseProgress(), 0.f);

    auto* Door = World->SpawnActor<AOBTrainingDoor>();
    const FTransform FrameTransform = Door->FrameMesh->GetComponentTransform();
    const FTransform ActorTransform = Door->GetActorTransform();
    const FVector HandleLocation = Door->HandleMesh->GetComponentLocation();
    TestTrue(TEXT("Native breach starts"), Door->TryBreach(1));
    Door->Tick(Door->BreachDuration + 0.1f);
    TestTrue(TEXT("Breach completes after the animation"), Door->IsBreached());
    TestTrue(TEXT("Door frame stays fixed"), Door->FrameMesh->GetComponentTransform().Equals(FrameTransform));
    TestTrue(TEXT("Door actor stays fixed"), Door->GetActorTransform().Equals(ActorTransform));
    TestFalse(TEXT("Handle follows the rotating leaf"), Door->HandleMesh->GetComponentLocation().Equals(HandleLocation));
    Door->ResetDoor();
    TestFalse(TEXT("Door reset clears the completion flag"), Door->IsBreached());
    return true;
}
#endif

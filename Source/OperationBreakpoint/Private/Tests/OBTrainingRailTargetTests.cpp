#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Training/OBRailTrainingTarget.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBTrainingRailTargetTest, "OperationBreakpoint.Training.RailTarget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOBTrainingRailTargetTest::RunTest(const FString&)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT
    {
        World->DestroyWorld(false);
        GEngine->DestroyWorldContext(World);
    };
    auto* Target = World->SpawnActor<AOBRailTrainingTarget>(FVector(5000, 1200, 80), FRotator(0, 60, 0));
    UStaticMesh* RailAsset = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    Target->BaseMesh->SetStaticMesh(RailAsset);
    UStaticMesh* FullMesh = Target->BoardMesh->GetStaticMesh();
    TestNotNull(TEXT("Full target asset is assigned"), FullMesh);
    if (!FullMesh) return false;
    TestEqual(TEXT("Uses the requested full target mesh"), FullMesh->GetName(),
        FString(TEXT("SM_ShootingTargetFull_01")));
    Target->TravelDistance = 200;
    Target->MoveSpeed = 100;
    Target->StartMoving();
    const FTransform Base = Target->BaseMesh->GetComponentTransform();
    const FTransform ScoreOffset = Target->BoardMesh->GetRelativeTransform();
    const FVector Center = Target->TargetPivot->GetRelativeLocation();
    const FVector OldScore = Target->BoardMesh->Bounds.Origin;
    int32 PoseEvents = 0;
    Target->OnTargetPoseReached.AddLambda([&](AOBTrainingTarget*, bool) { ++PoseEvents; });
    Target->Tick(1.f);
    TestTrue(TEXT("Reaches the positive endpoint"), Target->TargetPivot->GetRelativeLocation().Equals(Center + FVector(100, 0, 0), 0.01f));
    Target->Tick(0.5f);
    TestTrue(TEXT("Reverses without going beyond the rail"), Target->TargetPivot->GetRelativeLocation().Equals(Center + FVector(50, 0, 0), 0.01f));
    TestTrue(TEXT("Actor and base stay fixed during travel"), Target->GetActorTransform().Equals(Base));
    TestTrue(TEXT("The assigned rail stays on the base root"), Target->BaseMesh->GetStaticMesh() == RailAsset);
    TestTrue(TEXT("Board retains its board offset"), Target->BoardMesh->GetRelativeTransform().Equals(ScoreOffset));
    TestEqual(TEXT("Travel does not repeatedly emit pose completion"), PoseEvents, 0);

    auto TraceAt = [&](const FVector& Aim) {
        FHitResult Hit;
        const FVector Normal = Target->GetScoringPlaneNormal();
        World->LineTraceSingleByChannel(Hit, Aim + Normal * 100, Aim - Normal * 100, ECC_Visibility, FCollisionQueryParams(NAME_None, true));
        return Hit;
    };
    TestTrue(TEXT("A real ray scores at the moving board's new position"),
        Target->IsScoringHit(TraceAt(Target->BoardMesh->Bounds.Origin)));
    TestFalse(TEXT("The old board position no longer scores"), Target->IsScoringHit(TraceAt(OldScore)));
    Target->StopMoving();
    const FVector Stopped = Target->TargetPivot->GetRelativeLocation();
    Target->Tick(1);
    TestTrue(TEXT("Manual stop preserves current position"), Target->TargetPivot->GetRelativeLocation().Equals(Stopped));
    TestFalse(TEXT("A stopped upright rail target has no idle tick"), Target->IsActorTickEnabled());
    Target->StartMoving();
    Target->Tick(0.5f);
    TestTrue(TEXT("Resume continues the same route"), Target->TargetPivot->GetRelativeLocation().Equals(Center, 0.01f));

    AOBTrainingTarget* TrainingTarget = Target;
    TrainingTarget->SetTargetEnabled(false, true);
    const FVector Disabled = Target->TargetPivot->GetRelativeLocation();
    Target->Tick(1);
    TestTrue(TEXT("Director's base-class disable also stops travel"), Target->TargetPivot->GetRelativeLocation().Equals(Disabled));
    TestFalse(TEXT("An inactive moving target cannot score"), Target->IsScoringHit(TraceAt(Target->BoardMesh->Bounds.Origin)));
    TrainingTarget->SetTargetEnabled(true, true);
    TestTrue(TEXT("Director reactivation resumes requested motion"), Target->IsMoving());
    TestEqual(TEXT("Enable and disable notify only once each"), PoseEvents, 2);

    Target->ResetMovement();
    Target->Tick(10.5f); // Several reflections in one frame.
    const FVector LargeFramePosition = Target->TargetPivot->GetRelativeLocation();
    Target->ResetMovement();
    for (int32 I = 0; I < 105; ++I) Target->Tick(0.1f);
    TestTrue(TEXT("Large-frame overshoot matches smaller frames"),
        Target->TargetPivot->GetRelativeLocation().Equals(LargeFramePosition, 0.01f));
    Target->MovementAxis = FVector::ZeroVector;
    Target->ResetMovement();
    Target->Tick(1);
    TestTrue(TEXT("An empty direction falls back to local X"), Target->TargetPivot->GetRelativeLocation().Equals(Center + FVector(100, 0, 0), 0.01f));
    Target->TravelDistance = 0;
    Target->ResetMovement();
    TestTrue(TEXT("Zero-length travel stays at the centre"), Target->TargetPivot->GetRelativeLocation().Equals(Center));
    TestFalse(TEXT("Zero-length travel disables the idle tick"), Target->IsActorTickEnabled());
    Target->TravelDistance = 200;
    Target->MoveSpeed = 0;
    Target->StartMoving();
    TestFalse(TEXT("Zero speed disables the idle tick"), Target->IsActorTickEnabled());
    Target->MoveSpeed=100;
    Target->StartMoving();
    const FVector HitPosition=Target->TargetPivot->GetRelativeLocation();
    TrainingTarget->HandleScoringHit(Target->BoardMesh->Bounds.Origin);
    TestFalse(TEXT("Scoring hit immediately stops rail movement"),Target->IsMoving());
    TestTrue(TEXT("Hit keeps ticking for the fold animation"),Target->IsActorTickEnabled());
    TestFalse(TEXT("Folding board no longer scores"),Target->IsTargetRaised());
    TrainingTarget->SetTargetEnabled(false,true);
    Target->Tick(.125f);
    TestTrue(TEXT("Hit interpolates backwards instead of snapping"),Target->TargetPivot->GetRelativeRotation().Roll>0 && Target->TargetPivot->GetRelativeRotation().Roll<90);
    Target->Tick(.125f);
    TestTrue(TEXT("Hit reaches exactly 90 degrees backwards"),Target->TargetPivot->GetRelativeRotation().Equals(Target->RaisedRotation+FRotator(0,0,90),.01f));
    TestTrue(TEXT("Fold preserves the struck rail position"),Target->TargetPivot->GetRelativeLocation().Equals(HitPosition));
    TestFalse(TEXT("Completed fold stops idle ticking"),Target->IsActorTickEnabled());
    TrainingTarget->SetTargetEnabled(true,true);
    TestTrue(TEXT("Retraining restores the upright board"),Target->IsTargetRaised() && Target->TargetPivot->GetRelativeRotation().Equals(Target->RaisedRotation));

    UClass* BlueprintClass = LoadClass<AOBRailTrainingTarget>(nullptr,
        TEXT("/Game/Blueprints/Training/BP_OBRailTrainingTarget.BP_OBRailTrainingTarget_C"));
    TestNotNull(TEXT("Saved rail Blueprint can be loaded"), BlueprintClass);
    if (BlueprintClass)
    {
        auto* BlueprintTarget = World->SpawnActor<AOBRailTrainingTarget>(BlueprintClass,
            FVector(7000, 1200, 80), FRotator(0, 180, 0));
        TArray<USceneComponent*> SceneComponents;
        BlueprintTarget->GetComponents(SceneComponents);
        TestEqual(TEXT("Saved Blueprint has exactly the four required components"), SceneComponents.Num(), 4);
        TestTrue(TEXT("Saved Blueprint uses BaseMesh as the rail root"), BlueprintTarget->GetRootComponent() == BlueprintTarget->BaseMesh);
        TestTrue(TEXT("Saved Blueprint pivot attaches directly to BaseMesh"), BlueprintTarget->TargetPivot->GetAttachParent() == BlueprintTarget->BaseMesh);
        BlueprintTarget->DispatchBeginPlay();
        TestEqual(TEXT("Saved rail Blueprint has backward fold settings"),BlueprintTarget->FoldRotation,FRotator(0,0,90));
        TestEqual(TEXT("Saved rail Blueprint uses a quarter-second hit fold"),BlueprintTarget->LowerSeconds,.25f);
        TestTrue(TEXT("Placed Blueprint automatically starts in play"), BlueprintTarget->IsMoving());
        const FVector ScoreBefore = BlueprintTarget->BoardMesh->Bounds.Origin;
        BlueprintTarget->Tick(0.5f);
        const FVector Aim = BlueprintTarget->BoardMesh->Bounds.Origin;
        TestFalse(TEXT("Blueprint board follows travelling board"), Aim.Equals(ScoreBefore));
        FHitResult Hit;
        const FVector Normal = BlueprintTarget->GetScoringPlaneNormal();
        World->LineTraceSingleByChannel(Hit, Aim + Normal * 100, Aim - Normal * 100, ECC_Visibility, FCollisionQueryParams(NAME_None, true));
        TestTrue(TEXT("Saved Blueprint scores a real ray after moving"), BlueprintTarget->IsScoringHit(Hit));
    }
    return true;
}
#endif

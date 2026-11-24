#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Training/OBTrainingDirector.h"
#include "Training/OBTrainingTarget.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBActionComponent.h"
#include "Core/OBPlayerController.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DecalComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBTrainingAccuracyToleranceTest, "OperationBreakpoint.Training.AccuracyTolerance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOBTrainingAccuracyToleranceTest::RunTest(const FString&)
{
    TGuardValue<bool> ScriptGuard(GAllowActorScriptExecutionInEditor, true);
    UClass* Class = LoadClass<AOBTrainingTarget>(nullptr, TEXT("/Game/Blueprints/Training/BP_TargetActor.BP_TargetActor_C"));
    if (!TestNotNull(TEXT("Zeroing board BP loads"), Class)) return false;
#if WITH_EDITOR
    FStaticMeshCompilingManager::Get().FinishAllCompilation();
#endif
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Player = World->SpawnActor<AOBPlayerCharacter>();
    auto* PC = World->SpawnActor<AOBPlayerController>();
    PC->Possess(Player);
    auto* Director = World->SpawnActor<AOBTrainingDirector>();
    ON_SCOPE_EXIT { Director->EndPlay(EEndPlayReason::Destroyed); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    Director->Controller = PC;
    Director->Action = Player->GetActionComponent();
    Director->bUseSplinePrototype = Director->bInitialized = true;
    Director->PrototypeRouteIndex = 1;
    Director->GroupRadiusCm = 5;
    auto* Board = World->SpawnActor<AOBTrainingTarget>(Class, FVector(3000, 1000, 200), FRotator::ZeroRotator);
    Director->AccuracyTarget = Board;

    auto Trace = [&](const FVector2D& Offset) {
        FHitResult Hit;
        const auto Transform = Board->BoardMesh->GetComponentTransform();
        const FVector Aim = Board->BoardMesh->Bounds.Origin + Transform.TransformVectorNoScale(FVector(Offset.X, 0, Offset.Y));
        const FVector Normal = Board->GetScoringPlaneNormal();
        World->LineTraceSingleByChannel(Hit, Aim + Normal * 300, Aim - Normal * 300,
            ECC_Visibility, FCollisionQueryParams(NAME_None, true));
        TestTrue(TEXT("Physical zeroing ray hits the board"), Board->IsScoringHit(Hit));
        return Hit;
    };
    auto Begin = [&] {
        Board->ClearBulletHoles();
        Director->BeginPrototypeExercise(EOBTrainingStep::Accuracy);
        TestFalse(TEXT("A new group clears its reference"), Director->bAccuracyHasReference);
        TestEqual(TEXT("A new group clears its max error"), Director->MaxAccuracyErrorCm, 0.0);
    };
    for (float Scale : {0.84375f, 2.f})
    {
        Board->SetActorScale3D(FVector(Scale));
        Board->SetActorRotation(FRotator(15, 37, 9));
        Begin();
        Director->HandleShotResolved(Trace(FVector2D(0, 0)));
        Director->HandleShotResolved(Trace(FVector2D(4.99, 0)));
        TestEqual(TEXT("World-space 4.99cm passes on scaled, rotated board"), Director->Hits, 2);
        Director->HandleShotResolved(Trace(FVector2D(-4.99, 0)));
        TestEqual(TEXT("Opposite-side hits within radius pass even when pairwise gap is near 10cm"), Director->CurrentDialogueId, 14);

        Begin();
        Director->HandleShotResolved(Trace(FVector2D(0, 0)));
        Director->HandleShotResolved(Trace(FVector2D(5.01, 0)));
        TestEqual(TEXT("World-space 5.01cm fails regardless of board scale"), Director->Hits, 1);
        TestTrue(TEXT("Measured over-limit error remains visible"), Director->LatestAccuracyErrorCm > 5 && Director->GetAccuracyMeasurementText().Contains(TEXT("5.00cm")));
        Director->HandleShotResolved(Trace(FVector2D(0, 0)));
        TestEqual(TEXT("Failed group restarts all three shots"), Director->Shots, 0);
        TestTrue(TEXT("Failure feedback retains the previous group's measured error"), Director->AccuracyRetryDetails.Contains(TEXT("5.01")));
    }
    Board->SetActorRotation(FRotator::ZeroRotator);
    Director->GroupRadiusCm = GetDefault<AOBTrainingDirector>()->GroupRadiusCm;
    TestEqual(TEXT("Default zeroing radius is doubled to 20cm"), Director->GroupRadiusCm, 20.f);
    Begin();
    Director->HandleShotResolved(Trace(FVector2D(0, 0)));
    Director->HandleShotResolved(Trace(FVector2D(5.9977, 0)));
    Director->HandleShotResolved(Trace(FVector2D(0, 4.2839)));
    TestEqual(TEXT("Observed tight group previously rejected at 5.9977cm now passes"), Director->CurrentDialogueId, 14);
    Begin();
    Director->HandleShotResolved(Trace(FVector2D(0, 0)));
    Director->HandleShotResolved(Trace(FVector2D(20, 0)));
    TestEqual(TEXT("New 20cm boundary is inclusive"), Director->Hits, 2);
    Director->HandleShotResolved(Trace(FVector2D(20.01, 0)));
    TestEqual(TEXT("Over 20cm still restarts the group"), Director->Shots, 0);
    Begin();
    Director->HandleShotResolved(Trace(FVector2D(0, 0)));
    Director->HandleShotResolved(Trace(FVector2D(15, 0)));
    Director->HandleShotResolved(Trace(FVector2D(-15, 0)));
    TestEqual(TEXT("All three board hits within the enlarged radius pass"),Director->CurrentDialogueId,14);
    Begin();
    Director->HandleShotResolved(Trace(FVector2D(0, 0)));
    Director->HandleShotResolved(FHitResult());
    Director->HandleShotResolved(Trace(FVector2D(0, 0)));
    TestEqual(TEXT("All three rounds still need board hits with the relaxed radius"), Director->Shots, 0);
    TestEqual(TEXT("A failed group returns to reload"), Director->CurrentStep, EOBTrainingStep::Reload);
    TestEqual(TEXT("Retry reload starts with an empty magazine"), PC->GetCurrentAmmo(), 0);
    Director->StepResult(EOBTrainingStep::Reload).Outcome = EOBTrainingOutcome::Passed;
    Begin();
    const double DeadlineBeforeFailure = Director->PrototypeDeadline;
    Director->HandleShotResolved(Trace(FVector2D(0, 0)));
    Director->HandleShotResolved(Trace(FVector2D(21, 0)));
    TArray<UDecalComponent*> Holes;
    Board->GetComponents(Holes);
    TestEqual(TEXT("Hits create decals before the result"), Holes.Num(), 2);
    Director->HandleShotResolved(Trace(FVector2D(0, 0)));
    Holes.Reset(); Board->GetComponents(Holes);
    TestEqual(TEXT("Failed zeroing clears all impact decals"), Holes.Num(), 0);
    TestFalse(TEXT("Reload success is revoked after failed zeroing"), Director->Result.HasPassed(EOBTrainingStep::Reload));
    TestEqual(TEXT("Failure cannot extend the inactivity deadline"), Director->PrototypeDeadline, DeadlineBeforeFailure);
    PC->OnReloadCompleted.AddUObject(Director, &AOBTrainingDirector::HandleReloadCompleted);
    PC->CompleteReload();
    TestTrue(TEXT("Actual reload restores the completed mission row"), Director->Result.HasPassed(EOBTrainingStep::Reload));
    TestEqual(TEXT("Actual reload returns to zeroing"), Director->CurrentStep, EOBTrainingStep::Accuracy);
    TestEqual(TEXT("Actual reload gives a fresh three-round group"), PC->GetCurrentAmmo(), 3);
    Director->GroupRadiusCm = 5;
    Begin();
    FHitResult Reference = Trace(FVector2D(0, 0));
    Director->HandleShotResolved(Reference);
    FHitResult Boundary = Reference;
    Boundary.ImpactPoint += FVector(5, 25, 0); // Depth must not contribute to planar spread.
    Director->HandleShotResolved(Boundary);
    TestEqual(TEXT("Exactly 5cm is inclusive and ignores normal-axis depth"), Director->Hits, 2);
    Begin();
    Director->HandleShotResolved(FHitResult());
    Director->HandleShotResolved(Trace(FVector2D(0, 0)));
    TestEqual(TEXT("A missed first shot cannot reuse a previous group's reference"), Director->Hits, 0);
    Director->HandleShotResolved(Trace(FVector2D(0, 0)));
    TestTrue(TEXT("First-shot miss is explained on retry"), Director->AccuracyRetryDetails.Contains(TEXT("첫 탄 미명중")));
    PC->OnShotResolved.AddUObject(Director,&AOBTrainingDirector::HandleShotResolved);
    const FVector Aim=Board->BoardMesh->Bounds.Origin, Normal=Board->GetScoringPlaneNormal();
    for (bool bCrouched : {false,true})
    {
        Begin(); Player->bIsCrouched=bCrouched;
        PC->FireAlongRay(Aim+Normal*300,Aim-Normal*300);
        TestEqual(TEXT("Zeroing accepts both standing and crouched shots"),Director->Hits,1);
        TestEqual(TEXT("Accepted zeroing shot consumes ammo"),PC->GetCurrentAmmo(),2);
    }
    Begin(); Player->bIsCrouched=false;
    Director->bPrototypePaused=true; PC->SetTrainingControls(true);
    Director->ResumePrototype(); PC->FireAlongRay(Aim+Normal*300,Aim-Normal*300);
    TestEqual(TEXT("Resumed zeroing still permits standing fire"),Director->Hits,1);
    Director->Action->SetExternalActionState(false);
    PC->FireAlongRay(Aim+Normal*300,Aim-Normal*300);
    TestEqual(TEXT("Prepare still blocks zeroing shots"),Director->Shots,1);
    TestEqual(TEXT("Prepare does not consume ammo"),PC->GetCurrentAmmo(),2);
    Director->Action->SetExternalActionState(true);
    PC->SetTrainingControls(false); PC->SetCrouchMode(true);
    PC->FireAlongRay(Aim+Normal*300,Aim-Normal*300);
    TestEqual(TEXT("Normal gameplay retains its posture gate"),Director->Shots,1);
    PC->SetTrainingControls(true,true); PC->SetCrouchMode(true);
    PC->FireAlongRay(Aim+Normal*300,Aim-Normal*300);
    TestEqual(TEXT("Other training shooting keeps its posture gate by default"),Director->Shots,1);
    return true;
}
#endif

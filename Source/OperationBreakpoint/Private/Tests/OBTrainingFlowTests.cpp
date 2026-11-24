#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Training/OBTrainingDirector.h"
#include "Training/OBTrainingDoor.h"
#include "Training/OBTrainingTarget.h"
#include "Training/OBTrainingProgressSubsystem.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBActionComponent.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Core/OBPlayerController.h"
#include "World/OBRailPath.h"
#include "Widget/OBPlayerOverlayWidget.h"
#include "Widget/OBSystemMessageWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SplineComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "TimerManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBTrainingFlowTest, "OperationBreakpoint.Training.Flow",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOBTrainingFlowTest::RunTest(const FString &)
{
    TGuardValue<bool> ScriptGuard(GAllowActorScriptExecutionInEditor, true);
    UWorld *World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    UOBPlayerOverlayWidget *Overlay = nullptr;
    AOBTrainingDirector *Director = nullptr;
    ON_SCOPE_EXIT
    {
        if (Director)
            Director->EndPlay(EEndPlayReason::Destroyed);
        if (Overlay)
            Overlay->ResetSystemMessage();
        World->DestroyWorld(false);
        GEngine->DestroyWorldContext(World);
    };
    UClass *OverlayClass = LoadClass<UOBPlayerOverlayWidget>(
        nullptr, TEXT("/Game/Blueprints/Widget/PlayerOverlay/WBP_PlayerOverlay.WBP_PlayerOverlay_C"));
    Overlay = CreateWidget<UOBPlayerOverlayWidget>(World, OverlayClass);
    if (!TestNotNull(TEXT("Overlay"), Overlay))
        return false;
    auto *Message = Cast<UOBSystemMessageWidget>(Overlay->WidgetTree->FindWidget(TEXT("SystemMessage")));
    if (!TestNotNull(TEXT("Designer child"), Message))
        return false;
    Overlay->SetSystemMessage(Message);
    Overlay->SystemMessageFadeIn = nullptr;
    Overlay->SystemMessageFadeOut = nullptr;
    auto *Player = World->SpawnActor<AOBPlayerCharacter>();
    auto *PC = World->SpawnActor<AOBPlayerController>();
    PC->Possess(Player);
    auto *Rail = World->SpawnActor<AOBRailPath>();
    Rail->bExternallyControlled = true;
    Rail->GetSpline()->SetLocationAtSplinePoint(1, FVector(400, 0, 0), ESplineCoordinateSpace::Local);
    Rail->GetSpline()->AddSplinePoint(FVector(800, 0, 0), ESplineCoordinateSpace::Local);
    auto *Spline = Player->GetPlayerSplineComponent();
    auto *Action = Player->GetActionComponent();
    Spline->PlayerCharacter = Player;
    Spline->PlayerCamera = Player->GetPlayerCamera();
    Spline->RailPath = Rail;
    Spline->FirstPersonCameraLocation = Player->GetPlayerCamera()->GetRelativeLocation();
    Spline->FirstPersonCameraRotation = Player->GetPlayerCamera()->GetRelativeRotation();
    Action->PlayerSplineComponent = Spline;
    Action->bAutoAdvanceOnEnemyClear = false;
    Director = World->SpawnActor<AOBTrainingDirector>();
    Director->bUsePopUpTargets = false; // Animated mode has its own collision/timer regression.
    Director->Controller = PC;
    Director->Action = Action;
    Director->Spline = Spline;
    Director->Overlay = Overlay;
    Director->bInitialized = true;
    Director->AccuracyTarget = World->SpawnActor<AOBTrainingTarget>(FVector(2000, 0, 0), FRotator::ZeroRotator);
    for (int32 I = 0; I < 10; ++I)
        Director->TimedTargets.Add(
            World->SpawnActor<AOBTrainingTarget>(FVector(3000, I * 150, 0), FRotator::ZeroRotator));
    Director->BreachDoor = World->SpawnActor<AOBTrainingDoor>();
    Director->StepDelaySeconds = Director->IntroFadeSeconds = Director->WelcomeSeconds = 0.1f;
    Director->Result.SessionId = FGuid::NewGuid();
    Director->Result.StartedAtUtc = FDateTime::UtcNow();
    Director->Result.TotalTargets = 10;
    for (const auto &Zone : Director->Zones)
        for (auto Step : Zone.Steps)
            Director->StepResult(Step);
    Overlay->OnSystemMessageShown.AddDynamic(Director, &AOBTrainingDirector::HandleMessageShown);
    Overlay->OnSystemMessageFinished.AddDynamic(Director, &AOBTrainingDirector::HandleMessageFinished);
    PC->OnTriggerPressed.AddUObject(Director, &AOBTrainingDirector::HandleTriggerPressed);
    PC->OnReloadCompleted.AddUObject(Director, &AOBTrainingDirector::HandleReloadCompleted);
    PC->OnAimCalibrationCompleted.AddUObject(Director, &AOBTrainingDirector::HandleCalibrationCompleted);
    PC->OnShotResolved.AddUObject(Director, &AOBTrainingDirector::HandleShotResolved);
    Spline->OnActionPointReached.AddUObject(Director, &AOBTrainingDirector::HandlePointReached);
    Director->BreachDoor->OnDoorBreached.AddDynamic(Director, &AOBTrainingDirector::HandleDoorBreached);
    auto Advance = [World](float Seconds) {
        ++GFrameCounter;
        World->TimeSeconds += Seconds;
        World->GetTimerManager().Tick(Seconds);
    };
    auto Next = [&]() {
        Advance(0);
        Advance(0.11f);
    };
    FString Error;
    TestTrue(TEXT("Default zone setup is valid"), Director->ValidateConfiguration(Error));
    Director->Zones[1].Steps.Add(EOBTrainingStep::AimHold);
    TestFalse(TEXT("Duplicate stage cannot bypass prerequisites"), Director->ValidateConfiguration(Error));
    Director->Zones[1].Steps.Pop();
    FHitResult Center, Outside;
    World->LineTraceSingleByChannel(Center, FVector(2100, 0, 0), FVector(1900, 0, 0), ECC_Visibility);
    World->LineTraceSingleByChannel(Outside, FVector(2100, 60, 0), FVector(1900, 60, 0), ECC_Visibility);
    TestTrue(TEXT("Real center trace scores"), Director->AccuracyTarget->IsScoringHit(Center));
    TestFalse(TEXT("Real outside trace misses"), Director->AccuracyTarget->IsScoringHit(Outside));
    Advance(0);
    Director->BeginStep(EOBTrainingStep::Intro);
    Next();
    TestEqual(TEXT("Intro advances to welcome"), Director->CurrentStep, EOBTrainingStep::Welcome);
    Next();
    Next();
    TestEqual(TEXT("Welcome precedes first exercise"), Director->CurrentStep, EOBTrainingStep::WeaponCheck);
    Director->HandleTimeout();
    Next();
    TestEqual(TEXT("Prerequisite timeout retries same exercise"), Director->CurrentStep, EOBTrainingStep::WeaponCheck);
    PC->OnTriggerPressed.Broadcast();
    Next();
    TestEqual(TEXT("Reload instruction follows weapon check in A"), Director->CurrentStep, EOBTrainingStep::Reload);
    TestEqual(TEXT("Reload starts empty"), PC->GetCurrentAmmo(), 0);
    Director->bEnableKeyboardTestInputs = true;
    TestTrue(TEXT("Reload instruction includes the right mouse button"),
             Director->GetInstructionText(EOBTrainingStep::Reload).ToString().Contains(TEXT("우클릭")));
    Director->bEnableKeyboardTestInputs = false;
    PC->CompleteReload();
    TestEqual(TEXT("Basic reload fills exactly three rounds in a twenty-round magazine"), PC->GetCurrentAmmo(), 3);
    TestEqual(TEXT("Basic magazine capacity remains twenty"), PC->GetMagazineCapacity(), 20);
    Next();
    TestEqual(TEXT("Aim hold follows reload in A"), Director->CurrentStep, EOBTrainingStep::AimHold);
    const FVector2D View(1920, 1080);
    Director->UpdateAimHold(1, Director->GetAimTargetPosition() * View, View);
    Director->UpdateAimHold(1, FVector2D::ZeroVector, View);
    TestEqual(TEXT("Leaving aim radius clears continuous hold"), Director->HoldSeconds, 0.f);
    TestEqual(TEXT("Default hold radius is tripled to 72 pixels"),Director->AimRadiusPixels,72.f);
    Director->UpdateAimHold(.5f,Director->GetAimTargetPosition()*View+FVector2D(71.9,0),View);
    TestEqual(TEXT("Near the enlarged edge still accumulates hold"),Director->HoldSeconds,.5f);
    Director->UpdateAimHold(.1f,Director->GetAimTargetPosition()*View+FVector2D(72.1,0),View);
    TestEqual(TEXT("Outside the enlarged edge still resets hold"),Director->HoldSeconds,0.f);
    for (int32 I = 0; I < 4; ++I)
    {
        const FVector2D Cursor = Director->GetAimTargetPosition() * View;
        Director->UpdateAimHold(1, Cursor, View);
        Director->UpdateAimHold(1, Cursor, View);
    }
    TestEqual(TEXT("Four holds are required"), Director->AimIndex, 4);
    Next();
    TestEqual(TEXT("Aim completion starts calibration"), Director->CurrentStep, EOBTrainingStep::Calibration);
    // Exercise the actual controller trace, ammo consumption, input guards and shot delegate.
    const auto FireAtTarget = [&](FVector Offset = FVector::ZeroVector) {
        const FVector Aim = Director->AccuracyTarget->BoardMesh->Bounds.Origin + Offset;
        PC->FireAlongRay(Aim + Director->AccuracyTarget->GetActorForwardVector() * 100.f,
                         Aim - Director->AccuracyTarget->GetActorForwardVector() * 100.f);
    };
    Director->bEnableKeyboardTestInputs = true;
    TestTrue(TEXT("Calibration instruction explains C before shooting"),
             Director->GetInstructionText(EOBTrainingStep::Calibration).ToString().Contains(TEXT("C 키")));
    Director->bEnableKeyboardTestInputs = false;
    FireAtTarget();
    TestEqual(TEXT("Calibration cannot fire or consume ammo"), PC->GetCurrentAmmo(), 0);
    TestEqual(TEXT("Calibration cannot record a shot"), Director->Shots, 0);
    PC->NotifyAimCalibrationCompleted();
    Next();
    TestEqual(TEXT("Calibration callback opens actual zeroing stage"), Director->CurrentStep, EOBTrainingStep::Accuracy);
    TestEqual(TEXT("Zeroing starts with three live rounds"), PC->GetCurrentAmmo(), 3);
    Action->SetExternalActionState(false);
    FireAtTarget();
    TestEqual(TEXT("Prepare blocks real firing path"), PC->GetCurrentAmmo(), 3);
    Action->SetExternalActionState(true);
    FireAtTarget();
    TestEqual(TEXT("Real complex trace scores on training target"), Director->Hits, 1);
    TestEqual(TEXT("Real trace consumes one round"), PC->GetCurrentAmmo(), 2);
    FireAtTarget(FVector(0, 20.1, 0));
    FireAtTarget();
    TestEqual(TEXT("Even on the board, >20cm group retries calibration"), Director->PendingStep,
              EOBTrainingStep::Calibration);
    Next();
    PC->NotifyAimCalibrationCompleted();
    Next();
    FireAtTarget();
    FireAtTarget(FVector(0, 5, 0));
    FireAtTarget(FVector(0, 0, 4));
    TestEqual(TEXT("Three real trace shots empty the zeroing magazine"), PC->GetCurrentAmmo(), 0);
    Next();
    TestEqual(TEXT("Zeroing starts travel to B"), Director->CurrentStep, EOBTrainingStep::MovingToB);
    TestEqual(TEXT("Travel blocks player actions"), Action->GetActionState(), EOBActionState::Prepare);
    for (int32 I = 0; I < 300 && Director->CurrentStep == EOBTrainingStep::MovingToB; ++I)
        Spline->TickComponent(1.f / 60, LEVELTICK_All, &Spline->PrimaryComponentTick);
    TestEqual(TEXT("Rail arrival starts B cover"), Director->CurrentStep, EOBTrainingStep::Cover);
    Director->bEnableKeyboardTestInputs = true;
    auto *Movement = Player->GetCharacterMovement();
    Movement->SetMovementMode(MOVE_Walking);
    Action->SetExternalActionState(false);
    Director->TestCover();
    TestFalse(TEXT("V ignores Prepare"), Movement->bWantsToCrouch);
    Action->SetExternalActionState(true);
    Director->TestBreach();
    TestFalse(TEXT("B ignores other zones"), Director->bBreachAccepted);
    Director->TestCover();
    TestTrue(TEXT("V requests a real crouch"), Movement->bWantsToCrouch);
    TestFalse(TEXT("Crouch request does not finish before movement applies it"), Director->bTransitionPending);
    Movement->Crouch(false);
    TestTrue(TEXT("Engine crouch changes actual posture"), Player->bIsCrouched);
    Director->Tick(0);
    Next();
    TestEqual(TEXT("Cover precedes countdown"), Director->CurrentStep, EOBTrainingStep::Countdown);
    TestTrue(TEXT("Countdown retains keyboard cover"), Player->bIsCrouched);
    Director->HandleTimeout();
    Next();
    TestEqual(TEXT("Countdown starts recorded fire"), Director->CurrentStep, EOBTrainingStep::TimedTargets);
    const auto FireRecordedTarget = [&](int32 Index) {
        auto *Target = Director->TimedTargets[Index].Get();
        const FVector Aim = Target->BoardMesh->Bounds.Origin;
        PC->FireAlongRay(Aim + Target->GetActorForwardVector() * 100.f,
                         Aim - Target->GetActorForwardVector() * 100.f);
    };
    Director->TestCover();
    Movement->UnCrouch(false);
    TestFalse(TEXT("Second V stands up"), Player->bIsCrouched);
    FireRecordedTarget(0);
    TestEqual(TEXT("Standing cannot fire B targets"), Director->Result.ShotsFired, 0);
    Director->TestCover();
    Movement->Crouch(false);
    FireRecordedTarget(0);
    FireRecordedTarget(0);
    PC->FireAlongRay(FVector(9000, 0, 0), FVector(9100, 0, 0));
    for (int32 I = 1; I < 6; ++I)
        FireRecordedTarget(I);
    Director->TestReload();
    TestEqual(TEXT("R refills B magazine"), PC->GetCurrentAmmo(), 20);
    TestEqual(TEXT("Distinct targets exclude duplicate"), Director->HitTargets.Num(), 6);
    TestEqual(TEXT("Bullet hits include duplicate"), Director->Result.ValidHits, 7);
    TestEqual(TEXT("Miss still counts as shot"), Director->Result.ShotsFired, 8);
    TestFalse(TEXT("Six hits must not end recorded fire early"), Director->bTransitionPending);
    // Advance the actual timer through the full assessment window.
    Advance(30.1f);
    Next();
    TestEqual(TEXT("Full-duration assessment starts C travel"), Director->CurrentStep, EOBTrainingStep::MovingToC);
    Director->NotifyBreachState(true);
    for (int32 I = 0; I < 300 && Director->CurrentStep == EOBTrainingStep::MovingToC; ++I)
        Spline->TickComponent(1.f / 60, LEVELTICK_All, &Spline->PrimaryComponentTick);
    TestEqual(TEXT("C arrival starts breach"), Director->CurrentStep, EOBTrainingStep::Breach);
    Director->NotifyBreachState(true);
    TestFalse(TEXT("Held gesture cannot breach on arrival"), Director->bBreachAccepted);
    Director->TestBreach();
    TestTrue(TEXT("Keyboard B generates a fresh breach before deadline"), Director->bBreachAccepted);
    TestFalse(TEXT("B waits for the door animation"), Director->bTransitionPending);
    Director->HandleTimeout();
    Director->BreachDoor->Tick(20);
    TestTrue(TEXT("Accepted gesture remains valid after deadline while door opens"),
             Director->BreachDoor->IsBreached());
    Next();
    TestTrue(TEXT("End-to-end flow records finished"), Director->Result.bFinished);
    TestTrue(TEXT("Keyboard course is marked as test input"), Director->Result.bUsedTestInputs);
    TestFalse(TEXT("Debug completion does not qualify"), Director->Result.bPassed);
    FOBTrainingResultData Snapshot = Director->Result;
    Snapshot.bUsedTestInputs = false;
    TestTrue(TEXT("All nine passing records satisfy the remaining qualification criteria"), Snapshot.MeetsCompletionRequirements());
    Snapshot = Director->Result;
    Snapshot.Steps.FindByPredicate([](const auto &S) { return S.Step == EOBTrainingStep::TimedTargets; })->Outcome =
        EOBTrainingOutcome::Failed;
    TestFalse(TEXT("Failed fire plus successful breach does not qualify"), Snapshot.MeetsCompletionRequirements());
    // Exercise a failed final assessment and verify completion is still reported.
    Director->Result.bFinished = false;
    Director->Result.bPassed = false;
    Director->ZoneIndex = 2;
    Director->StageIndex = 0;
    Director->BeginStep(EOBTrainingStep::Breach);
    Advance(10.1f);
    Next();
    TestTrue(TEXT("Failed breach still finishes session"), Director->Result.bFinished);
    TestFalse(TEXT("Failed breach cannot issue certificate"), Director->Result.bPassed);
    // No stale completion from the earlier successful attempt can change the snapshot.
    Director->HandleDoorBreached(Director->BreachDoor, Director->Attempt - 1);
    TestFalse(TEXT("Stale door callback cannot qualify failed assessment"), Director->Result.bPassed);
    // Last prerequisite attempt aborts rather than retrying forever.
    Director->Result.bFinished = false;
    Director->ZoneIndex = 0;
    Director->StageIndex = 0;
    Director->StepResult(EOBTrainingStep::WeaponCheck).Attempts = Director->MaxPrerequisiteAttempts - 1;
    Director->BeginStep(EOBTrainingStep::WeaponCheck);
    Director->HandleTimeout();
    TestTrue(TEXT("Prerequisite retry limit aborts"), Director->Result.bAborted);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBTrainingPersistenceTest, "OperationBreakpoint.Training.Persistence",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOBTrainingPersistenceTest::RunTest(const FString &)
{
    auto *Instance = NewObject<UGameInstance>(GEngine);
    auto *Progress = NewObject<UOBTrainingProgressSubsystem>(Instance);
    FOBTrainingResultData Result;
    Result.SessionId = FGuid::NewGuid();
    Result.bFinished = true;
    Result.StartedAtUtc = Result.CompletedAtUtc = FDateTime::UtcNow();
    Result.TotalTargets = 10;
    Result.ShotsFired = Result.ValidHits = Result.UniqueTargetsHit = 6;
    Result.RecordedFireSeconds = 30;
    const TArray<EOBTrainingStep> Required = {
        EOBTrainingStep::WeaponCheck, EOBTrainingStep::Reload,       EOBTrainingStep::AimHold,
        EOBTrainingStep::Calibration, EOBTrainingStep::Accuracy,     EOBTrainingStep::Cover,
        EOBTrainingStep::Countdown,   EOBTrainingStep::TimedTargets, EOBTrainingStep::Breach};
    for (auto Step : Required)
    {
        FOBTrainingStepResult Record;
        Record.Step = Step;
        Record.Outcome = EOBTrainingOutcome::Passed;
        Record.Attempts = 1;
        Result.Steps.Add(Record);
    }
    const FString Path = FPaths::ProjectSavedDir() / TEXT("TrainingRecords") /
                         (Result.SessionId.ToString(EGuidFormats::Digits) + TEXT(".json"));
    FGuid FailedId = FGuid::NewGuid();
    ON_SCOPE_EXIT
    {
        IFileManager::Get().Delete(*Path);
        IFileManager::Get().Delete(*(FPaths::ProjectSavedDir() / TEXT("TrainingRecords") /
                                     (FailedId.ToString(EGuidFormats::Digits) + TEXT(".json"))));
    };
    TestTrue(TEXT("Snapshot saved"), Progress->CommitTrainingResult(Result));
    TestTrue(TEXT("Saved record exists"), IFileManager::Get().FileExists(*Path));
    TestTrue(TEXT("Qualification available"), Progress->HasCompletedTraining());
    FOBTrainingResultData Mutated = Result;
    Mutated.bAborted = true;
    TestTrue(TEXT("Repeated commit is idempotent"), Progress->CommitTrainingResult(Mutated));
    FOBTrainingResultData Stored;
    Progress->GetTrainingResult(Result.SessionId, Stored);
    TestTrue(TEXT("Immutable original qualification survives caller edits"), Stored.bPassed);
    Mutated.SessionId = FailedId;
    Progress->CommitTrainingResult(Mutated);
    TestTrue(TEXT("Later failure does not erase prior qualification"), Progress->HasCompletedTraining());
    return true;
}
#endif

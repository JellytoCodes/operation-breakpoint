#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Training/OBTrainingDirector.h"
#include "Training/OBTrainingTarget.h"
#include "Training/OBRailTrainingTarget.h"
#include "Training/OBTrainingDoor.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Character/OBActionComponent.h"
#include "Core/OBPlayerController.h"
#include "Widget/OBHudPanelWidget.h"
#include "Widget/OBPlayerOverlayWidget.h"
#include "Widget/OBSystemMessageWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "World/OBRailPath.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"
#include "Sound/SoundBase.h"
#include "Components/AudioComponent.h"
#include "AudioDeviceManager.h"
#include "Animation/WidgetAnimation.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBTrainingDialogueTableTest,"OperationBreakpoint.Training.DialogueTable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOBTrainingDialogueTableTest::RunTest(const FString&)
{
    auto* Table=LoadObject<UDataTable>(nullptr,TEXT("/Game/Blueprints/Training/Data/DT_TrainingDialogue.DT_TrainingDialogue"));
    if (!TestNotNull(TEXT("Saved dialogue table exists"),Table)) return false;
    TestTrue(TEXT("Saved schema matches native row"),Table->GetRowStruct()==FOBTrainingDialogueRow::StaticStruct());
    TestNull(TEXT("RowName replaces the redundant ID property"),FindFProperty<FProperty>(FOBTrainingDialogueRow::StaticStruct(),TEXT("ID")));
    TestEqual(TEXT("Latest table has exactly 22 rows"),Table->GetRowNames().Num(),22);
    const float Display[]={0,3,4,0,0,0,0,0,4,4,3,3,3,5,5,4,4,0,0,4,4,0,0};
    const TCHAR* Voices[]={TEXT(""),TEXT("VO_Sys_Intro"),TEXT("VO_Train_Intro"),
        TEXT("VO_Train_A_Guide_0"),TEXT("VO_Train_A_Guide_1"),TEXT("VO_Train_A_Guide_2"),TEXT("VO_Train_A_Guide_3"),TEXT("VO_Train_A_Guide_4"),
        TEXT("VO_Train_A_Move"),TEXT("VO_Train_Trigger_Fail"),TEXT("VO_Train_Aim_Fail"),TEXT("VO_Train_Cover_Fail"),TEXT("VO_Train_Reload_Fail"),
        TEXT("VO_Train_Zero_Fail"),TEXT("VO_Train_A_Pass"),TEXT("VO_Train_A_Move_B"),TEXT("VO_Train_B_Intro"),
        TEXT("VO_Train_B_Guide_0"),TEXT("VO_Train_B_Guide_1"),TEXT("VO_Train_B_Move"),
        TEXT("VO_Sys_Train_A_Fail"),TEXT("VO_Sys_Train_B_Success"),TEXT("VO_Sys_Train_B_Fail")};
    for (int32 Id=1;Id<=22;++Id)
    {
        const auto* Row=Table->FindRow<FOBTrainingDialogueRow>(FName(*FString::FromInt(Id)),TEXT("Dialogue verification"),false);
        if (!TestNotNull(FString::Printf(TEXT("RowName %d exists"),Id),Row)) continue;
        TestFalse(TEXT("Script is populated"),Row->Script.IsEmpty());
        TestEqual(TEXT("Voice matches the assigned dialogue file"),Row->Voice.GetAssetName(),FString(Voices[Id]));
        auto* Voice=Row->Voice.LoadSynchronous();
        if (TestNotNull(TEXT("Saved soft voice reference resolves"),Voice)) TestTrue(TEXT("Voice contains playable duration"),Voice->GetDuration()>0);
        TestEqual(TEXT("Display time matches specification"),Row->Display_Time,Display[Id]);
        TestEqual(TEXT("Delay matches specification"),Row->Delay_Time,Id==2 || Id==3 || Id==17 ? .5f : 0.f);
        TestEqual(TEXT("Previous ID matches specification"),Row->Prev_ID,(Id>=2 && Id<=8) || Id==15 || (Id>=17 && Id<=19) ? Id-1 : 0);
        TestEqual(TEXT("Speaker enum matches specification"),Row->Character,Id==1 || Id>=20 ? EOBTrainingDialogueCharacter::System : EOBTrainingDialogueCharacter::Instructor);
        TestEqual(TEXT("Location enum matches specification"),Row->Location,Id<=15 || Id==20 ? EOBTrainingDialogueLocation::Zone_A : EOBTrainingDialogueLocation::Zone_B);
        TestEqual(TEXT("Layer enum matches specification"),Row->Layer,Id==1 || Id>=20 ? EOBTrainingDialogueLayer::Main : EOBTrainingDialogueLayer::Sub);
        TestEqual(TEXT("Event enum matches specification"),Row->Event,Id<=8 ? EOBTrainingDialogueEvent::A_Guide : Id<=14 || Id==20 ? EOBTrainingDialogueEvent::A_Training : Id<=19 ? EOBTrainingDialogueEvent::B_Guide : EOBTrainingDialogueEvent::B_Training);
        TestEqual(TEXT("Condition enum matches specification"),Row->Condition,(Id>=9 && Id<=13) || Id==20 || Id==22 ? EOBTrainingDialogueCondition::Fail : Id==14 || Id==21 ? EOBTrainingDialogueCondition::Pass : EOBTrainingDialogueCondition::None);
        TestEqual(TEXT("UI open matches specification"),Row->bUIOpen,(Id>=3 && Id<=7) || Id==17 || Id==18 || Id>=20);
#if WITH_EDITORONLY_DATA
        TestFalse(TEXT("Editor note is retained"),Row->Note.IsEmpty());
#endif
    }
    const auto* Intro=Table->FindRow<FOBTrainingDialogueRow>(TEXT("2"),TEXT("Intro verification"));
    if (Intro) TestEqual(TEXT("Instructor intro uses latest script"),Intro->Script.ToString(),FString(TEXT("반갑습니다. 오늘 전체훈련 진행을 맡은 이중구 교관입니다.")));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBTrainingSplinePrototypeTest, "OperationBreakpoint.Training.SplinePrototype",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOBTrainingSplinePrototypeTest::RunTest(const FString&)
{
    TGuardValue<bool> ScriptGuard(GAllowActorScriptExecutionInEditor, true);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Player = World->SpawnActor<AOBPlayerCharacter>();
    auto* PC = World->SpawnActor<AOBPlayerController>();
    PC->Possess(Player);
    PC->PlayerCameraManager=World->SpawnActor<APlayerCameraManager>();
    PC->PlayerCameraManager->InitializeFor(PC);
    auto* Rail = World->SpawnActor<AOBRailPath>();
    Rail->bExternallyControlled = true;
    Rail->GetSpline()->ClearSplinePoints();
    for (int32 I = 0; I < 18; ++I) Rail->GetSpline()->AddSplinePoint(FVector(I * 100, 0, 0), ESplineCoordinateSpace::Local);
    TMap<int32, FOBRailStopSettings> Stops;
    for (int32 I : {2, 4, 7, 12, 17}) Stops.Add(I);
    auto* StopProperty = FindFProperty<FMapProperty>(AOBRailPath::StaticClass(), TEXT("StopPointSettings"));
    *StopProperty->ContainerPtrToValuePtr<TMap<int32, FOBRailStopSettings>>(Rail) = Stops;
    auto* Spline = Player->GetPlayerSplineComponent();
    auto* Action = Player->GetActionComponent();
    Spline->PlayerCharacter = Player;
    Spline->PlayerCamera = Player->GetPlayerCamera();
    Spline->RailPath = Rail;
    Spline->FirstPersonCameraLocation = Player->GetPlayerCamera()->GetRelativeLocation();
    Spline->FirstPersonCameraRotation = Player->GetPlayerCamera()->GetRelativeRotation();
    Action->PlayerSplineComponent = Spline;
    Action->bAutoAdvanceOnEnemyClear = false;
    auto* Director = World->SpawnActor<AOBTrainingDirector>();
    ON_SCOPE_EXIT
    {
        Director->EndPlay(EEndPlayReason::Destroyed);
        World->DestroyWorld(false);
        GEngine->DestroyWorldContext(World);
    };
    Director->Controller = PC;
    Director->Action = Action;
    Director->Spline = Spline;
    Director->bInitialized = Director->bUseSplinePrototype = true;
    Director->PrototypeNarrationSeconds = 0.02f;
    // This route-flow fixture skips the presentation delay; UIPages covers the real post-voice gate.
    Director->GuideTriggerDelaySeconds = 0.f;
    // Flow tests use accelerated narration; saved voices and timings are verified separately.
    auto* SourceTable=Director->DialogueTable.LoadSynchronous();
    auto* FlowTable=NewObject<UDataTable>(Director);
    FlowTable->RowStruct=FOBTrainingDialogueRow::StaticStruct();
    if (!TestNotNull(TEXT("Dialogue table can seed flow test"),SourceTable)) return false;
    for (const FName Key : SourceTable->GetRowNames())
    {
        auto Row=*SourceTable->FindRow<FOBTrainingDialogueRow>(Key,TEXT("Accelerated flow"));
        Row.Voice.Reset(); Row.Display_Time=Row.Delay_Time=0;
        FlowTable->AddRow(Key,Row);
    }
    Director->DialogueTable=FlowTable;
    Director->IntroFadeSeconds=0.8f;
    Director->AimHoldSeconds = 0.05f;
    Director->TargetExposureSeconds = 0.15f;
    Director->TargetGapSeconds = 0.05f;
    Director->Result.SessionId = FGuid::NewGuid();
    Director->AccuracyTarget = World->SpawnActor<AOBTrainingTarget>(FVector(3000, 0, 0), FRotator::ZeroRotator);
    for (int32 I = 0; I < 6; ++I)
    {
        AOBTrainingTarget* Target = I == 2 || I >= 4
            ? World->SpawnActor<AOBRailTrainingTarget>(FVector(4000, I * 300, 0), FRotator::ZeroRotator)
            : World->SpawnActor<AOBTrainingTarget>(FVector(4000, I * 300, 0), FRotator::ZeroRotator);
        if (!Target->IsA<AOBRailTrainingTarget>()) Target->RaiseSeconds = Target->LowerSeconds = 0.1f;
        Director->TimedTargets.Add(Target);
    }
    Director->BreachDoor = World->SpawnActor<AOBTrainingDoor>();
    Director->BreachDoor->OnDoorBreached.AddDynamic(Director, &AOBTrainingDirector::HandleDoorBreached);
    PC->OnTriggerPressed.AddUObject(Director, &AOBTrainingDirector::HandleTriggerPressed);
    PC->OnReloadCompleted.AddUObject(Director, &AOBTrainingDirector::HandleReloadCompleted);
    PC->OnShotResolved.AddUObject(Director, &AOBTrainingDirector::HandleShotResolved);
    Spline->OnActionPointReached.AddUObject(Director, &AOBTrainingDirector::HandlePointReached);
    auto Advance = [&](float Seconds) {
        for (int32 I = 0; I < FMath::CeilToInt(Seconds / 0.05f); ++I)
        {
            ++GFrameCounter;
            World->TimeSeconds += 0.05f;
            World->GetTimerManager().Tick(0.05f);
            for (AOBTrainingTarget* Target : Director->TimedTargets)
                if (Target->IsActorTickEnabled()) Target->Tick(0.05f);
            Spline->TickComponent(0.05f, LEVELTICK_All, nullptr);
            Director->Tick(0.05f);
        }
    };
    auto Fire = [&](AOBTrainingTarget* Target) {
        const FVector Aim = Target->BoardMesh->Bounds.Origin;
        PC->FireAlongRay(Aim + Target->GetScoringPlaneNormal() * 100, Aim - Target->GetScoringPlaneNormal() * 100);
    };
    FString Error;
    TestTrue(TEXT("Six prototype stops validate against a real spline"), Director->ValidatePrototype(Error));
    auto* DelayedRow=FlowTable->FindRow<FOBTrainingDialogueRow>(TEXT("2"),TEXT("Delayed narration"));
    DelayedRow->Delay_Time=1.f;
    Director->PlayPrototypeLine(2);
    Advance(.2f);
    TestEqual(TEXT("Dialogue stays hidden during its configured delay"),Director->CurrentDialogueId,0);
    PC->FireAtCursor();
    TestEqual(TEXT("Trigger cannot bypass a scheduled dialogue delay"),Director->CurrentDialogueId,0);
    Director->StopPrototypeNarration();
    Advance(1.1f);
    TestEqual(TEXT("Stopping narration cancels delayed playback"),Director->CurrentDialogueId,0);
    DelayedRow->Delay_Time=0;
    Director->BeginPrototype();
    TestEqual(TEXT("Tutorial starts fully black"),Director->IntroBrightness,0.f);
    TestTrue(TEXT("Initial sign facing is applied instantly under black"),FMath::IsNearlyEqual(Player->GetActorRotation().Yaw,-90.f,.01f));
    TestTrue(TEXT("Controller also faces the sign immediately"),FMath::IsNearlyEqual(PC->GetControlRotation().Yaw,-90.f,.01f));
    TestFalse(TEXT("Intro has no interpolated turn"),Director->bPrototypeTurning);
    TestEqual(TEXT("Intro uses the configured fade duration without doubling"),Director->GetPrototypeIntroDuration(),.8f);
    TestEqual(TEXT("No narration starts before fade"),Director->CurrentDialogueId,0);
    PC->FireAtCursor();
    TestEqual(TEXT("Trigger cannot bypass intro fade"),Director->CurrentDialogueId,0);
    Advance(0.2f);
    TestTrue(TEXT("Brightness gradually increases"),Director->IntroBrightness>0 && Director->IntroBrightness<1);
    TestTrue(TEXT("Subtitle is empty during fade"),Director->GetSubtitleText().IsEmpty());
    TestTrue(TEXT("System message is empty during fade"),Director->GetSystemMessageText().IsEmpty());
    TestEqual(TEXT("Training remains at point zero during fade"),Spline->GetCurrentPointIndex(),0);
    Advance(0.25f);
    TestEqual(TEXT("Partial fade cannot start dialogue"),Director->CurrentDialogueId,0);
    Advance(0.55f);
    TestEqual(TEXT("Narration begins only at brightness one"),Director->IntroBrightness,1.f);
    TestEqual(TEXT("Camera fade is fully cleared"),PC->PlayerCameraManager->FadeAmount,0.f);
    TestEqual(TEXT("Initial automatic lines lead to guide 3"), Director->CurrentDialogueId, 3);
    TestTrue(TEXT("Point zero faces the sign 90 degrees left"),FMath::IsNearlyEqual(Player->GetActorRotation().Yaw,-90.f,0.01f));
    TestNotNull(TEXT("Final dialogue DataTable contains row 22"),Director->FindPrototypeDialogue(22));
    TestEqual(TEXT("Final instructor intro text is used"),Director->PrototypeLineText(2).ToString(),FString(TEXT("반갑습니다. 오늘 전체훈련 진행을 맡은 이중구 교관입니다.")));
    Director->PlayPrototypeLine(3);
    PC->FireAtCursor();
    TestEqual(TEXT("Trigger cannot skip an unfinished explanation"), Director->CurrentDialogueId, 3);
    Director->FinishPrototypeNarration();
    for (int32 Id = 3; Id <= 6; ++Id)
    {
        TestEqual(TEXT("Guide pages advance in order"), Director->CurrentDialogueId, Id);
        if (Id == 6) Director->PrototypeNarrationSeconds = 1.f;
        PC->FireAtCursor();
        if (Id < 6) Director->FinishPrototypeNarration();
    }
    TestEqual(TEXT("Last guide is line 7"),Director->CurrentDialogueId,7);
    PC->FireAtCursor();
    TestEqual(TEXT("Trigger cannot skip final guide narration"),Director->CurrentDialogueId,7);
    Director->FinishPrototypeNarration();
    Advance(2.f);
    TestEqual(TEXT("Final A guide stays on line 7 after narration ends"),Director->CurrentDialogueId,7);
    TestTrue(TEXT("Final A guide waits for trigger without turning or moving"),Director->bGuideAdvanceReady && Director->IsPrototypeGuideVisible() && !Director->bPrototypeTurning && !Spline->IsMoving());
    TestTrue(TEXT("Final A guide preserves sign facing while waiting"),FMath::IsNearlyEqual(Player->GetActorRotation().Yaw,-90.f,0.01f));
    PC->FireAtCursor();
    TestTrue(TEXT("Line 7 trigger closes guide and starts return turn"),Director->bPrototypeTurning && !Director->IsPrototypeGuideVisible() && !Spline->IsMoving());
    PC->FireAtCursor();
    TestEqual(TEXT("Repeated trigger during return turn cannot skip to narration"),Director->CurrentDialogueId,0);
    Advance(0.55f);
    TestTrue(TEXT("Point zero returns right to the route heading"),FMath::IsNearlyEqual(Player->GetActorRotation().Yaw,0.f,0.01f));
    TestEqual(TEXT("Return turn opens line 8 before moving"), Director->CurrentDialogueId, 8);
    Advance(0.25f);
    PC->FireAtCursor();
    TestTrue(TEXT("Line 8 holds at point zero and cannot be skipped"), Spline->GetCurrentPointIndex() == 0 && !Spline->IsMoving());
    Advance(0.85f);
    TestEqual(TEXT("Line 8 ending starts actual movement"), Director->CurrentStep, EOBTrainingStep::Moving);
    Director->PrototypeNarrationSeconds = 0.02f;
    TestEqual(TEXT("Guide advance does not fire ammunition"), PC->GetCurrentAmmo(), 0);
    Advance(2);
    TestEqual(TEXT("A training begins at point 2"), Spline->GetCurrentPointIndex(), 2);
    TestEqual(TEXT("A starts with weapon check"), Director->CurrentStep, EOBTrainingStep::WeaponCheck);
    TestTrue(TEXT("A faces 90 degrees left of arrival"), FMath::IsNearlyEqual(Player->GetActorRotation().Yaw, -90.f, 0.01f));
    TestTrue(TEXT("Camera control yaw follows the training turn"), FMath::IsNearlyEqual(PC->GetControlRotation().Yaw, -90.f, 0.01f));
    Director->PrototypeReminderAt = World->GetTimeSeconds();
    Director->TickPrototype();
    TestEqual(TEXT("No progress reminder does not fail or change the step"), Director->CurrentStep, EOBTrainingStep::WeaponCheck);
    TestEqual(TEXT("Weapon check reminder selects row 9"),Director->PrototypeHintId,9);
    Director->PrototypeDeadline = World->GetTimeSeconds();
    Director->TickPrototype();
    TestTrue(TEXT("A timeout pauses"), Director->bPrototypePaused);
    TestEqual(TEXT("Pause clears reminder subtitle"),Director->PrototypeHintId,0);
    TestNull(TEXT("Pause stops reminder audio"),Director->PrototypeHintAudio.Get());
    TestFalse(TEXT("Pause does not finish the session"), Director->Result.bFinished);
    PC->FireAtCursor();
    TestEqual(TEXT("Paused input cannot complete a step"), Director->CurrentStep, EOBTrainingStep::WeaponCheck);
    auto* PauseClass=LoadClass<UOBTrainingResultWidget>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/Components/WBP_TrainingResult.WBP_TrainingResult_C"));
    if (!TestNotNull(TEXT("Warning uses the Result Blueprint"),PauseClass)) return false;
    auto* Pause=CreateWidget<UOBTrainingResultWidget>(World,PauseClass); Pause->SetTrainingDirector(Director);
    Pause->SetWarning(Director->GetTrainingResult()); Pause->PrepareWarningInteraction(false); Pause->HandleTrigger(true); Pause->HandleTrigger(false);
    TestFalse(TEXT("Short pause trigger resumes the real training Director"),Director->bPrototypePaused);
    const double PreviousDeadline=Director->PrototypeDeadline;
    Advance(40.f);
    PC->FireAtCursor();
    TestTrue(TEXT("Weapon check completion restores the view before starting aim"),Director->bPrototypeWeaponViewReturning);
    TestTrue(TEXT("Weapon check mission is checked during the view transition"),Director->Result.HasPassed(EOBTrainingStep::WeaponCheck));
    TestFalse(TEXT("Aim cannot count while the inspection view is returning"),Director->bStepActive);
    Advance(Director->WeaponCheckCameraBlendSeconds);
    TestEqual(TEXT("Resume permits the same weapon check"), Director->CurrentStep, EOBTrainingStep::AimHold);
    TestTrue(TEXT("Successful step starts a fresh basic inactivity deadline"),Director->PrototypeDeadline>PreviousDeadline+39);
    TestTrue(TEXT("New basic step receives the full two-minute window"),FMath::IsNearlyEqual(Director->GetRemainingSeconds(),Director->PrototypeZoneSeconds,.1f));
    const double RetryDeadline=Director->PrototypeDeadline;
    Director->BeginPrototypeExercise(EOBTrainingStep::AimHold);
    TestEqual(TEXT("Retrying an incomplete basic step does not refresh timeout"),Director->PrototypeDeadline,RetryDeadline);
    for (int32 I = 0; I < Director->AimPositions.Num(); ++I)
        Director->UpdateAimHold(0.1f, Director->GetAimTargetPosition() * FVector2D(1920, 1080), FVector2D(1920, 1080));
    Player->bIsCrouched = true;
    Director->HandleCoverState(true);
    PC->CompleteReload();
    TestEqual(TEXT("Cover then reload lead directly to zeroing"), Director->CurrentStep, EOBTrainingStep::Accuracy);
    for (int32 I = 0; I < 3; ++I) Fire(Director->AccuracyTarget);
    TestEqual(TEXT("A completion plays line 14"), Director->CurrentDialogueId, 14);
    Player->bIsCrouched = false;
    Director->PrototypeNarrationSeconds = 1.f;
    Director->FinishPrototypeNarration();
    Advance(0.1f);
    TestTrue(TEXT("Return turn keeps point 2 until facing is restored"), Spline->GetCurrentPointIndex() == 2 && !Spline->IsMoving());
    TestTrue(TEXT("Return turns right toward the arrival heading"), Player->GetActorRotation().Yaw > -90.f && Player->GetActorRotation().Yaw < 0.f);
    Advance(0.45f);
    TestEqual(TEXT("Return faces original heading before line 15"), Director->CurrentDialogueId, 15);
    TestTrue(TEXT("Line 15 waits at the completed basic stop"), Spline->GetCurrentPointIndex() == 2 && !Spline->IsMoving());
    PC->FireAtCursor();
    TestFalse(TEXT("Trigger cannot skip line 15 narration"), Spline->IsMoving());
    Director->FinishPrototypeNarration();
    Director->PrototypeNarrationSeconds = 0.02f;
    Advance(3);
    TestEqual(TEXT("B sign is at point 4"), Spline->GetCurrentPointIndex(), 4);
    TestTrue(TEXT("Point 4 faces its sign 90 degrees left"), FMath::IsNearlyEqual(Player->GetActorRotation().Yaw, -90.f, 0.01f));
    TestEqual(TEXT("B introduction leads to guide 17"), Director->CurrentDialogueId, 17);
    Director->FinishPrototypeNarration();
    Director->PrototypeNarrationSeconds=1.f;
    PC->FireAtCursor();
    TestEqual(TEXT("B final guide is line 18"),Director->CurrentDialogueId,18);
    PC->FireAtCursor();
    TestEqual(TEXT("Trigger cannot skip line 18"),Director->CurrentDialogueId,18);
    Director->FinishPrototypeNarration();
    Advance(2.f);
    TestEqual(TEXT("Final B guide stays on line 18 after narration ends"),Director->CurrentDialogueId,18);
    TestTrue(TEXT("Final B guide waits for trigger without turning or moving"),Director->bGuideAdvanceReady && Director->IsPrototypeGuideVisible() && !Director->bPrototypeTurning && !Spline->IsMoving());
    TestTrue(TEXT("Final B guide preserves sign facing while waiting"),FMath::IsNearlyEqual(Player->GetActorRotation().Yaw,-90.f,0.01f));
    PC->FireAtCursor();
    TestTrue(TEXT("Line 18 trigger starts return turn"),Director->bPrototypeTurning && !Director->IsPrototypeGuideVisible() && !Spline->IsMoving());
    Advance(0.55f);
    TestEqual(TEXT("After return line 19 plays at point 4"),Director->CurrentDialogueId,19);
    TestTrue(TEXT("Point 4 returns 90 degrees right"),FMath::IsNearlyEqual(Player->GetActorRotation().Yaw,0.f,0.01f));
    TestTrue(TEXT("Line 19 waits at point 4"),Spline->GetCurrentPointIndex()==4 && !Spline->IsMoving());
    PC->FireAtCursor();
    TestFalse(TEXT("Trigger cannot skip line 19"),Spline->IsMoving());
    Director->FinishPrototypeNarration();
    Director->PrototypeNarrationSeconds=0.02f;
    for (int32 I=0;I<100 && !Director->bPrototypeMockStartPending;++I) Advance(.05f);
    TestEqual(TEXT("First shooting group starts at point 7"), Spline->GetCurrentPointIndex(), 7);
    TestTrue(TEXT("Point 7 turns right before firing starts"), FMath::IsNearlyEqual(Player->GetActorRotation().Yaw, 90.f, 0.01f));
    TestEqual(TEXT("Looking at targets enters start-signal wait"),Director->CurrentStep,EOBTrainingStep::Countdown);
    TestEqual(TEXT("Mock clock does not run before siren"),Director->GetRemainingSeconds(),0.f);
    TestFalse(TEXT("Shooting is blocked before start signal"),PC->bTrainingCanShoot);
    TestFalse(TEXT("Rail remains disabled before start signal"),CastChecked<AOBRailTrainingTarget>(Director->TimedTargets[2])->IsMoving());
    Advance(1.8f);
    TestEqual(TEXT("Targets stay inactive during two-second preparation"),Director->CurrentStep,EOBTrainingStep::Countdown);
    Advance(.3f);
    TestEqual(TEXT("Missing siren asset still starts on the same signal schedule"),Director->CurrentStep,EOBTrainingStep::TimedTargets);
    TestTrue(TEXT("Signal starts a full 120-second clock"),Director->GetRemainingSeconds()>119.7f);
    TestTrue(TEXT("First group's rail moves alongside the popup"), CastChecked<AOBRailTrainingTarget>(Director->TimedTargets[2])->IsMoving());
    int32 RaisedEvents = 0, LoweredEvents = 0;
    for (int32 I : {0, 1}) Director->TimedTargets[I]->OnTargetPoseReached.AddLambda([&](AOBTrainingTarget*, bool bRaised) { if (bRaised) ++RaisedEvents; else ++LoweredEvents; });
    Advance(1.5f);
    TestTrue(TEXT("Unhit popups retract and rise repeatedly"), RaisedEvents >= 2 && LoweredEvents >= 2);
    const double BDeadline = Director->PrototypeDeadline;
    Fire(Director->TimedTargets[2]); Fire(Director->TimedTargets[2]);
    TestEqual(TEXT("Repeated hits only complete one target"), Director->Result.UniqueTargetsHit, 1);
    TestFalse(TEXT("Hit rail stops travelling"), CastChecked<AOBRailTrainingTarget>(Director->TimedTargets[2])->IsMoving());
    for (int32 I = 0; I < 30 && Director->Result.UniqueTargetsHit < 3; ++I)
    {
        if (auto* Target = Director->GetActiveTimedTarget()) Fire(Target);
        Advance(0.05f);
    }
    TestEqual(TEXT("First mixed group counts both popups and its rail"), Director->Result.UniqueTargetsHit, 3);
    Advance(3);
    TestEqual(TEXT("Three distinct targets advance to point 12"), Spline->GetCurrentPointIndex(), 12);
    TestTrue(TEXT("Point 12 turns right before firing starts"), FMath::IsNearlyEqual(Player->GetActorRotation().Yaw, 90.f, 0.01f));
    TestTrue(TEXT("Both second-group rails move together"), CastChecked<AOBRailTrainingTarget>(Director->TimedTargets[4])->IsMoving() && CastChecked<AOBRailTrainingTarget>(Director->TimedTargets[5])->IsMoving());
    TestEqual(TEXT("B time includes travel without resetting"), Director->PrototypeDeadline, BDeadline);
    Fire(Director->TimedTargets[0]);
    TestEqual(TEXT("Previous group's target cannot count again"), Director->Result.UniqueTargetsHit, 3);
    Fire(Director->TimedTargets[4]); Fire(Director->TimedTargets[5]);
    for (int32 I = 0; I < 20 && Director->Result.UniqueTargetsHit < 6; ++I)
    {
        if (auto* Target = Director->GetActiveTimedTarget()) Fire(Target);
        Advance(0.05f);
    }
    Advance(3);
    TestEqual(TEXT("All six targets lead to point 17"), Spline->GetCurrentPointIndex(), 17);
    TestFalse(TEXT("Arrival at final stop does not prematurely show results"), Director->Result.bFinished);
    Director->NotifyBreachState(true);
    TestFalse(TEXT("Breach input alone does not finish before door animation"), Director->Result.bFinished);
    static_cast<AActor*>(Director->BreachDoor.Get())->Tick(Director->BreachDoor->BreachDuration + 0.1f);
    TestTrue(TEXT("Door animation reaches its open state"), Director->BreachDoor->IsBreached());
    TestTrue(TEXT("Door completion finishes the route"), Director->Result.bFinished && Director->Result.bScenarioCompleted);
    TestEqual(TEXT("Completed training dims the world to thirty percent"), PC->PlayerCameraManager->FadeAlpha.Y, .3);
    TestFalse(TEXT("Prototype cannot issue a certificate"), Director->Result.bPassed);
    TestEqual(TEXT("Under-limit completion plays the pass narration despite test input"),Director->GetPrototypeDisplayDialogueId(),21);
    // A separate assessment times out while travelling between the B shooting stops.
    Director->Result = FOBTrainingResultData();
    Director->Result.SessionId = FGuid::NewGuid();
    Director->Result.bPrototypeFlow = Director->Result.bUsedTestInputs = true;
    Spline->CurrentPointIndex = 7;
    Spline->CurrentDistance = Rail->GetSpline()->GetDistanceAlongSplineAtSplinePoint(7);
    Spline->RailState = UOBPlayerSplineComponent::EOBRailState::Action;
    Spline->ApplySplineTransform(Spline->CurrentDistance);
    Director->MovePrototypeTo(4);
    Director->PrototypeDeadline = World->GetTimeSeconds() + 0.01;
    Advance(0.05f);
    TestTrue(TEXT("B timeout finishes as an incomplete assessment"), Director->Result.bFinished && !Director->Result.bAborted && !Director->Result.bScenarioCompleted);
    TestEqual(TEXT("Timeout selects the fail narration"),Director->GetPrototypeDisplayDialogueId(),22);
    TestFalse(TEXT("B timeout stops rail movement"), Spline->IsMoving());
    const FVector Halted = Player->GetActorLocation();
    Advance(0.5f);
    TestTrue(TEXT("Result screen cannot continue moving the player"), Player->GetActorLocation().Equals(Halted));
    Director->HandlePointReached(17);
    TestEqual(TEXT("Late arrival cannot restart a completed assessment"), Director->CurrentStep, EOBTrainingStep::Complete);
    // A very short fade must still wait for the sign turn before starting narration.
    Director->Result=FOBTrainingResultData(); Director->Result.SessionId=FGuid::NewGuid();
    Director->PrototypeDeadline=0; Director->IntroFadeSeconds=0.05f;
    Spline->CurrentPointIndex=0; Spline->CurrentDistance=0;
    Spline->RailState=UOBPlayerSplineComponent::EOBRailState::Action; Spline->ApplySplineTransform(0);
    Spline->OnTrainingTurnCompleted.RemoveAll(Director);
    Director->BeginPrototype(); Advance(0.1f);
    TestEqual(TEXT("Short fade reaches full brightness"),Director->IntroBrightness,1.f);
    TestEqual(TEXT("Initial turn is already complete while short fade ends"),Spline->RailState,UOBPlayerSplineComponent::EOBRailState::Action);
    Advance(0.6f);
    TestEqual(TEXT("Narration begins when fade and turn have both completed"),Director->CurrentDialogueId,3);
    Director->Result.bFinished=true;
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBTrainingSystemMessageTest,"OperationBreakpoint.Training.SystemMessage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOBTrainingSystemMessageTest::RunTest(const FString&)
{
    TGuardValue<bool> ScriptGuard(GAllowActorScriptExecutionInEditor,true);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Director=World->SpawnActor<AOBTrainingDirector>();
    ON_SCOPE_EXIT
    {
        Director->EndPlay(EEndPlayReason::Destroyed);
        World->DestroyWorld(false);
        GEngine->DestroyWorldContext(World);
    };
    auto* Class=LoadClass<UOBPlayerOverlayWidget>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/WBP_PlayerOverlay.WBP_PlayerOverlay_C"));
    if (!TestNotNull(TEXT("Saved overlay loads"),Class)) return false;
    auto* Overlay=CreateWidget<UOBPlayerOverlayWidget>(World,Class);
    const auto Slate=Overlay->TakeWidget();
    auto* Message=Cast<UOBSystemMessageWidget>(Overlay->WidgetTree->FindWidget(TEXT("SystemMessage")));
    auto* Subtitle=Overlay->WidgetTree->FindWidget(TEXT("Subtitle"));
    if (!TestNotNull(TEXT("Authored SystemMessage child binds"),Message) || !TestNotNull(TEXT("Subtitle exists"),Subtitle)) return false;
    TestTrue(TEXT("Native binding retains the authored SystemMessage instance"),Overlay->SystemMessage==Message);
    TArray<UTextBlock*> Labels;
    Message->WidgetTree->ForEachWidget([&](UWidget* Widget) { if (auto* Text=Cast<UTextBlock>(Widget)) Labels.Add(Text); });
    if (!TestEqual(TEXT("SystemMessage has only a script TextBlock"),Labels.Num(),1)) return false;
    Director->bUseSplinePrototype=true;
    Director->CurrentStep=EOBTrainingStep::Welcome;
    Director->CurrentDialogueId=1;
    Overlay->SetTrainingDirector(Director);
    TestTrue(TEXT("System intro is visible in its independent panel"),Message->IsVisible());
    TestFalse(TEXT("System intro does not appear in Subtitle"),Subtitle->IsVisible());
    TestTrue(TEXT("System has no subtitle speaker"),Director->GetSubtitleSpeaker().IsEmpty());
    TestEqual(TEXT("System panel receives only script content"),Labels[0]->GetText().ToString(),Director->PrototypeLineText(1).ToString());
    Director->CurrentDialogueId=2;
    Overlay->SetTrainingDirector(Director);
    TestFalse(TEXT("Instructor hides SystemMessage"),Message->IsVisible());
    TestTrue(TEXT("Instructor uses Subtitle"),Subtitle->IsVisible());
    TestEqual(TEXT("Instructor keeps its speaker label"),Director->GetSubtitleSpeaker().ToString(),FString(TEXT("교관")));
    Director->CurrentDialogueId=0;
    Overlay->SetTrainingDirector(Director);
    TestFalse(TEXT("Configured delay does not retain a system message"),Message->IsVisible());
    TestFalse(TEXT("Configured delay does not retain subtitles"),Subtitle->IsVisible());
    auto* Guide=Cast<UOBHudPanelWidget>(Overlay->WidgetTree->FindWidget(TEXT("Guide")));
    auto* GuideBody=Cast<UTextBlock>(Guide->WidgetTree->FindWidget(TEXT("BodyText")));
    const FText Placeholder=GuideBody->GetText();
    Director->CurrentDialogueId=3;
    Overlay->SetTrainingDirector(Director);
    TestFalse(TEXT("Guide data replaces the Designer placeholder"),GuideBody->GetText().EqualTo(Placeholder));
    TestFalse(TEXT("Guide body is independent from the narration script"),GuideBody->GetText().EqualTo(Director->GetPrototypeDialogueText()));
    Director->CurrentStep=EOBTrainingStep::WeaponCheck;
    Director->CurrentDialogueId=0;
    Director->PrototypeRouteIndex=1;
    Director->PrototypeDeadline=World->GetTimeSeconds()+120;
    Overlay->SetTrainingDirector(Director);
    TestTrue(TEXT("Basic inactivity deadline exists internally"),Director->GetRemainingSeconds()>0);
    TestFalse(TEXT("Basic inactivity timer is hidden in PlayerOverlay"),Overlay->WidgetTree->FindWidget(TEXT("Timer"))->IsVisible());
    Director->PrototypeRouteIndex=3;
    Director->CurrentStep=EOBTrainingStep::TimedTargets;
    Overlay->SetTrainingDirector(Director);
    TestTrue(TEXT("Mock training retains its visible two-minute timer"),Overlay->WidgetTree->FindWidget(TEXT("Timer"))->IsVisible());
    Director->bPrototypePaused=true;
    Overlay->SetTrainingDirector(Director);
    TestFalse(TEXT("Warning does not duplicate content in SystemMessage"),Message->IsVisible());
    TestEqual(TEXT("Paused training uses warning UI 3008"),Overlay->GetTrainingResultWidget()->CurrentUIId,3008);
    TestFalse(TEXT("Pause system text does not duplicate in Subtitle"),Subtitle->IsVisible());
    Director->bPrototypePaused=false;
    Director->Result.bFinished=true;
    for (bool bPass : {true,false})
    {
        Director->Result.bPrototypeFlow=true;
        Director->Result.bUsedTestInputs=true;
        Director->Result.bPassed=false;
        Director->Result.bScenarioCompleted=bPass;
        Director->Result.RecordedFireLimitSeconds=120;
        Director->Result.RecordedFireSeconds=bPass ? 75 : 120;
        Overlay->SetTrainingDirector(Director);
        Overlay->ShowTrainingResult(Director->Result);
        TestFalse(TEXT("Result does not duplicate content in SystemMessage"),Message->IsVisible());
        TestEqual(TEXT("Result uses separate pass/fail UI row"),Overlay->GetTrainingResultWidget()->CurrentUIId,bPass ? 3009 : 3010);
        TestFalse(TEXT("Result does not duplicate system text in subtitles"),Subtitle->IsVisible());
    }
    Overlay->SetSystemMessageEnabled(false);
    TestFalse(TEXT("Overlay can disable SystemMessage independently"),Message->IsVisible());
    Overlay->SetSystemMessageEnabled(true);
    TestFalse(TEXT("Result remains the exclusive terminal UI when SystemMessage is enabled"),Message->IsVisible());
    Overlay->SetStageMode();
    TestFalse(TEXT("Leaving training clears the training system message"),Message->IsVisible());
    // UI routing follows Layer for an instructor line moved to Main as well.
    auto* Table=NewObject<UDataTable>(Director);
    Table->RowStruct=FOBTrainingDialogueRow::StaticStruct();
    auto Row=*Director->FindPrototypeDialogue(2); Row.Layer=EOBTrainingDialogueLayer::Main;
    Table->AddRow(TEXT("2"),Row); Director->DialogueTable=Table;
    Director->Result=FOBTrainingResultData(); Director->CurrentDialogueId=2; Director->CurrentStep=EOBTrainingStep::Welcome;
    Overlay->SetTrainingDirector(Director);
    TestTrue(TEXT("Main layer chooses independent message panel"),Message->IsVisible());
    TestFalse(TEXT("Main layer suppresses bottom subtitle"),Subtitle->IsVisible());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBTrainingReminderVoiceTest,"OperationBreakpoint.Training.ReminderVoice",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOBTrainingReminderVoiceTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->bAllowAudioPlayback=true;
    if (auto* AudioManager=GEngine->GetAudioDeviceManager())
    {
        FAudioDeviceParams Params;
        Params.AssociatedWorld=World;
        Params.Scope=EAudioDeviceScope::Shared;
        World->SetAudioDevice(AudioManager->RequestAudioDevice(Params));
    }
    auto* Director=World->SpawnActor<AOBTrainingDirector>();
    ON_SCOPE_EXIT
    {
        Director->EndPlay(EEndPlayReason::Destroyed);
        World->DestroyWorld(false);
        GEngine->DestroyWorldContext(World);
    };
    if (!TestTrue(TEXT("Reminder voice regression requires an enabled audio device"),GEngine->UseSound() && World->GetAudioDevice().IsValid())) return false;
    Director->bUseSplinePrototype=true;
    Director->PrototypeRouteIndex=1;
    Director->bStepActive=true;
    World->TimeSeconds=20;
    const EOBTrainingStep Steps[]={EOBTrainingStep::WeaponCheck,EOBTrainingStep::AimHold,EOBTrainingStep::Cover,EOBTrainingStep::Reload,EOBTrainingStep::Accuracy};
    for (int32 Index=0;Index<UE_ARRAY_COUNT(Steps);++Index)
    {
        Director->CurrentStep=Steps[Index];
        Director->PrototypeReminderAt=World->GetTimeSeconds();
        Director->TickPrototype();
        const auto* Row=Director->FindPrototypeDialogue(9+Index);
        auto* Audio=Director->PrototypeHintAudio.Get();
        if (!TestNotNull(TEXT("20-second reminder creates audio component"),Audio) || !Row) continue;
        TestTrue(TEXT("Reminder starts playing the assigned voice"),Audio->IsPlaying() && Audio->Sound==Row->Voice.Get());
        TestEqual(TEXT("Reminder leaves training exercise active"),Director->CurrentStep,Steps[Index]);
        TestTrue(TEXT("Reminder permits ongoing exercise input"),Director->bStepActive);
        TestEqual(TEXT("Reminder does not enter guide narration"),Director->CurrentDialogueId,0);
        TestEqual(TEXT("Reminder selects corresponding script row"),Director->PrototypeHintId,9+Index);
        TestEqual(TEXT("Subtitle matches the voiced reminder"),Director->GetSubtitleText().ToString(),Row->Script.ToString());
        TestTrue(TEXT("Subtitle covers the full voice duration"),Director->PrototypeHintUntil-World->GetTimeSeconds()>=Audio->Sound->GetDuration());
        const double Until=Director->PrototypeHintUntil;
        Director->PlayPrototypeHint(9+Index);
        TestTrue(TEXT("Same reminder cannot overlap or restart"),Director->PrototypeHintAudio==Audio && Director->PrototypeHintUntil==Until);
    }
    auto* ZeroingAudio=Director->PrototypeHintAudio.Get();
    Director->PlayPrototypeHint(13);
    TestTrue(TEXT("Immediate repeated zeroing failure does not overlap"),Director->PrototypeHintAudio==ZeroingAudio);
    Director->StopPrototypeHint();
    TestNull(TEXT("Cleanup releases reminder audio"),Director->PrototypeHintAudio.Get());
    TestTrue(TEXT("Cleanup clears reminder subtitle"),Director->GetSubtitleText().IsEmpty());
    Director->bPrototypePaused=true;
    Director->PlayPrototypeHint(9);
    TestNull(TEXT("Paused training cannot play reminders"),Director->PrototypeHintAudio.Get());
    Director->bPrototypePaused=false;
    Director->Result.bFinished=true;
    Director->PlayPrototypeHint(9);
    TestNull(TEXT("Finished training cannot play reminders"),Director->PrototypeHintAudio.Get());
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBTrainingDialogueAnimationTest,"OperationBreakpoint.Training.DialogueAnimations",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOBTrainingDialogueAnimationTest::RunTest(const FString&)
{
    TGuardValue<bool> ScriptGuard(GAllowActorScriptExecutionInEditor,true);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    if (auto* Manager=GEngine->GetAudioDeviceManager())
    {
        FAudioDeviceParams Params; Params.AssociatedWorld=World; Params.Scope=EAudioDeviceScope::Shared;
        World->SetAudioDevice(Manager->RequestAudioDevice(Params));
    }
    auto* Director=World->SpawnActor<AOBTrainingDirector>();
    auto* Player=World->SpawnActor<AOBPlayerCharacter>();
    auto* PC=World->SpawnActor<AOBPlayerController>(); PC->Possess(Player);
    auto* Class=LoadClass<UOBPlayerOverlayWidget>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/WBP_PlayerOverlay.WBP_PlayerOverlay_C"));
    auto* Overlay=CreateWidget<UOBPlayerOverlayWidget>(World,Class);
    ON_SCOPE_EXIT
    {
        Director->EndPlay(EEndPlayReason::Destroyed);
        if (Overlay) Overlay->CancelTrainingDialogue();
        World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    };
    if (!TestNotNull(TEXT("Authored overlay instantiates"),Overlay) ||
        !TestTrue(TEXT("Voice lifecycle test requires audio"),GEngine->UseSound() && World->GetAudioDevice().IsValid())) return false;
    Overlay->TakeWidget();
    Director->Overlay=Overlay; Director->Controller=PC;
    Director->Action=Player->GetActionComponent(); Director->Spline=Player->GetPlayerSplineComponent();
    Director->bUseSplinePrototype=true;
    Overlay->SetTrainingDirector(Director); Overlay->SetTrainingDialogueAnimationControl(true);
    Overlay->OnSystemMessageShown.AddDynamic(Director,&AOBTrainingDirector::HandlePrototypeDialogueShown);
    Overlay->OnSubtitleShown.AddDynamic(Director,&AOBTrainingDirector::HandlePrototypeDialogueShown);
    Overlay->OnSystemMessageFinished.AddDynamic(Director,&AOBTrainingDirector::HandlePrototypeDialogueHidden);
    Overlay->OnSubtitleFinished.AddDynamic(Director,&AOBTrainingDirector::HandlePrototypeDialogueHidden);
    if (!TestNotNull(TEXT("SystemMessageFadeIn bound"),Overlay->SystemMessageFadeIn.Get()) ||
        !TestNotNull(TEXT("SystemMessageFadeOut bound"),Overlay->SystemMessageFadeOut.Get()) ||
        !TestNotNull(TEXT("SubtitleMessageFadeIn bound"),Overlay->SubtitleMessageFadeIn.Get()) ||
        !TestNotNull(TEXT("SubtitleMessageFadeOut bound"),Overlay->SubtitleMessageFadeOut.Get())) return false;
    auto TickTimers=[&](float Seconds) { ++GFrameCounter; World->TimeSeconds+=Seconds; World->GetTimerManager().Tick(Seconds); Overlay->RefreshPanelVisibility(); };
    Director->PlayPrototypeLine(1);
    TestNull(TEXT("System voice cannot start before fade-in"),Director->PrototypeNarrationAudio.Get());
    TickTimers(8);
    TestEqual(TEXT("Waiting for fade-in never advances dialogue"),Director->CurrentDialogueId,1);
    Overlay->HandleSystemMessageFadeInFinished();
    auto* Audio=Director->PrototypeNarrationAudio.Get();
    if (TestNotNull(TEXT("System fade-in completion starts voice"),Audio)) TestTrue(TEXT("System voice plays"),Audio->IsPlaying());
    Director->FinishPrototypeNarration();
    TickTimers(8);
    TestEqual(TEXT("Voice completion waits for system fade-out"),Director->CurrentDialogueId,1);
    Overlay->HandleSystemMessageFadeOutFinished();
    TestEqual(TEXT("Next row waits its data-table delay after fade-out"),Director->CurrentDialogueId,0);
    TickTimers(.6f);
    TestEqual(TEXT("After delay, row 2 starts subtitle fade-in"),Director->CurrentDialogueId,2);
    TestNull(TEXT("Instructor voice waits for subtitle fade-in"),Director->PrototypeNarrationAudio.Get());
    Overlay->HandleSubtitleFadeInFinished();
    TestNotNull(TEXT("Subtitle fade-in starts instructor voice"),Director->PrototypeNarrationAudio.Get());
    Director->FinishPrototypeNarration(); TickTimers(8);
    TestEqual(TEXT("Instructor ending waits for subtitle fade-out"),Director->CurrentDialogueId,2);
    Overlay->HandleSubtitleFadeOutFinished(); TickTimers(.6f);
    TestEqual(TEXT("Fade-out leads to first guide"),Director->CurrentDialogueId,3);
    auto* Guide=Overlay->GetTrainingGuideWidget();
    if(!TestNotNull(TEXT("Authored guide"),Guide) || !TestNotNull(TEXT("GuideFadeIn bound"),Guide->GuideFadeIn.Get()) || !TestNotNull(TEXT("GuideFadeOut bound"),Guide->GuideFadeOut.Get()))return false;
    TestEqual(TEXT("Guide waits for narration start"),Guide->GetRenderOpacity(),0.f);
    Overlay->HandleSubtitleFadeInFinished();
    TestTrue(TEXT("Narration start plays the swapped show animation GuideFadeOut"),Guide->IsAnimationPlaying(Guide->GuideFadeOut));
    Director->FinishPrototypeNarration(); TickTimers(8);
    TestTrue(TEXT("Guide speech completion retains trigger requirement"),Director->bGuideAdvanceReady);
    Director->HandleTriggerPressed(); TickTimers(8);
    TestEqual(TEXT("Guide trigger also waits for authored fade-out"),Director->CurrentDialogueId,3);
    TestFalse(TEXT("Repeated trigger cannot bypass guide fade-out"),Director->bGuideAdvanceReady);
    TestTrue(TEXT("Trigger plays the swapped hide animation GuideFadeIn"),Guide->IsAnimationPlaying(Guide->GuideFadeIn));
    Overlay->HandleSubtitleFadeOutFinished();
    TestEqual(TEXT("Subtitle finishing first cannot replace the guide page"),Director->CurrentDialogueId,3);
    Guide->HandleGuideFadeOutFinished();
    TestEqual(TEXT("Guide fade-out moves to next page"),Director->CurrentDialogueId,4);
    Overlay->HandleSubtitleFadeInFinished();Director->FinishPrototypeNarration();TickTimers(8);
    Director->HandleTriggerPressed();Guide->HandleGuideFadeOutFinished();
    TestEqual(TEXT("Guide finishing first advances at the guide's completion"),Director->CurrentDialogueId,5);
    Overlay->HandleSubtitleFadeOutFinished();
    TestEqual(TEXT("Old subtitle completion cannot advance the next page"),Director->CurrentDialogueId,5);
    Director->StopPrototypeNarration();
    Guide->HandleGuideFadeOutFinished();
    TestEqual(TEXT("Cancelled guide completion cannot advance a later page"),Director->CurrentDialogueId,5);
    Director->CurrentDialogueId=0; Director->CurrentStep=EOBTrainingStep::WeaponCheck; Director->bStepActive=true;
    Director->PlayPrototypeHint(9);
    TestNull(TEXT("Reminder voice also waits for fade-in"),Director->PrototypeHintAudio.Get());
    Overlay->HandleSubtitleFadeInFinished();
    TestNotNull(TEXT("Reminder fade-in starts assigned audio"),Director->PrototypeHintAudio.Get());
    Director->FinishPrototypeHint(); TickTimers(8);
    TestEqual(TEXT("Reminder stays until fade-out completion"),Director->PrototypeHintId,9);
    Overlay->HandleSubtitleFadeOutFinished();
    TestEqual(TEXT("Reminder clears after fade-out"),Director->PrototypeHintId,0);
    TestEqual(TEXT("Reminder preserves active exercise"),Director->CurrentStep,EOBTrainingStep::WeaponCheck);
    Director->PlayPrototypeHint(9); Director->StopPrototypeHint();
    Overlay->HandleSubtitleFadeInFinished();
    TestNull(TEXT("Cancelled reminder cannot start stale voice"),Director->PrototypeHintAudio.Get());
    // Tick the authored animation tracks as well as testing explicit callback ordering above.
    auto Animate=[&](float Seconds)
    {
        for (float Elapsed=0;Elapsed<Seconds;Elapsed+=.05f)
        {
            ++GFrameCounter;
            Overlay->TickActionsAndAnimation(.05f);
            Overlay->FlushAnimations();
            Overlay->RefreshPanelVisibility();
        }
    };
    Director->PlayPrototypeHint(9);
    Overlay->TickActionsAndAnimation(0); Overlay->FlushAnimations();
    TestNull(TEXT("Authored animation begins without voice"),Director->PrototypeHintAudio.Get());
    Animate(Overlay->SubtitleMessageFadeIn->GetEndTime()-Overlay->SubtitleMessageFadeIn->GetStartTime()+.2f);
    TestNotNull(TEXT("Ticked authored fade-in starts reminder voice"),Director->PrototypeHintAudio.Get());
    Director->FinishPrototypeHint();
    Animate(Overlay->SubtitleMessageFadeOut->GetEndTime()-Overlay->SubtitleMessageFadeOut->GetStartTime()+.2f);
    TestEqual(TEXT("Ticked authored fade-out clears reminder"),Director->PrototypeHintId,0);
    return !HasAnyErrors();
}
#endif

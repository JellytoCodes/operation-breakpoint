#include "Training/OBTrainingDirector.h"
#include "Training/OBTrainingTarget.h"
#include "Training/OBTrainingDoor.h"
#include "Core/OBPlayerController.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Character/OBActionComponent.h"
#include "MediaPipe/OBMediaPipeInputComponent.h"
#include "Widget/OBPlayerOverlayWidget.h"
#include "World/OBRailPath.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraComponent.h"
#include "Components/SplineComponent.h"
#include "TimerManager.h"

TArray<FName> AOBTrainingDirector::GetQACheckpoints() const
{
    TArray<FName> Names={TEXT("Intro"),TEXT("WeaponCheck"),TEXT("AimHold"),TEXT("Cover"),TEXT("Reload")};
    if (!bUseSplinePrototype) Names.Add(TEXT("Calibration"));
    Names.Append({TEXT("Accuracy"),TEXT("Countdown"),TEXT("Shooting1")});
    if (bUseSplinePrototype) Names.Add(TEXT("Shooting2"));
    Names.Append({TEXT("Breach"),TEXT("Result")});
    return Names;
}

FName AOBTrainingDirector::GetQACheckpoint() const
{
    if (Result.bFinished || CurrentStep==EOBTrainingStep::Complete) return TEXT("Result");
    if (CurrentStep==EOBTrainingStep::TimedTargets)
        return bUseSplinePrototype && PrototypeRouteIndex==4 ? TEXT("Shooting2") : TEXT("Shooting1");
    if (CurrentStep==EOBTrainingStep::Idle || CurrentStep==EOBTrainingStep::Welcome) return TEXT("Intro");
    if (CurrentStep==EOBTrainingStep::Moving && bUseSplinePrototype)
    {
        if (PrototypeRouteIndex==1) return TEXT("WeaponCheck");
        if (PrototypeRouteIndex==2 || PrototypeRouteIndex==3) return TEXT("Countdown");
        if (PrototypeRouteIndex==4) return TEXT("Shooting2");
        if (PrototypeRouteIndex==5) return TEXT("Breach");
    }
    return FName(*StaticEnum<EOBTrainingStep>()->GetNameStringByValue(static_cast<int64>(CurrentStep)));
}

FString AOBTrainingDirector::GetQAStatus() const
{
    return FString::Printf(TEXT("Checkpoint=%s Step=%s Point=%d Route=%d Dialogue=%d Ammo=%d Remaining=%.2f Active=%d Transition=%d QA=%d"),
        *GetQACheckpoint().ToString(),*StaticEnum<EOBTrainingStep>()->GetNameStringByValue(static_cast<int64>(CurrentStep)),
        Spline ? Spline->GetCurrentPointIndex() : INDEX_NONE,PrototypeRouteIndex,CurrentDialogueId,
        Controller ? Controller->GetCurrentAmmo() : 0,GetRemainingSeconds(),bStepActive,bTransitionPending,bQASession);
}

bool AOBTrainingDirector::EnterQACheckpoint(FName Checkpoint, FString& Error)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    // Validate everything before cancelling a running exercise.
    if (!GetQACheckpoints().Contains(Checkpoint))
    { Error=TEXT("Unknown checkpoint for this course. Use QA List; prototype calibration is part of Accuracy."); return false; }
    auto* Player=Controller ? Cast<AOBPlayerCharacter>(Controller->GetPawn()) : nullptr;
    auto* Rail=Spline ? Spline->GetRailPath() : nullptr;
    if (!bInitialized || !IsValid(Player) || !IsValid(Action) || !IsValid(Spline) || Spline->GetOwner()!=Player ||
        !IsValid(Player->GetPlayerCamera()) || !IsValid(Rail) || !Rail->bExternallyControlled || Rail->bPreviewEntireRoute ||
        !IsValid(AccuracyTarget) || !IsValid(BreachDoor))
    { Error=TEXT("Training is not ready. Wait until the player, external rail and TrainingDirector finish initializing."); return false; }

    const bool bIntro=Checkpoint==TEXT("Intro"), bResult=Checkpoint==TEXT("Result");
    EOBTrainingStep Step=EOBTrainingStep::Idle;
    if (Checkpoint==TEXT("Shooting1") || Checkpoint==TEXT("Shooting2")) Step=EOBTrainingStep::TimedTargets;
    else if (!bIntro && !bResult)
    {
        const int64 Value=StaticEnum<EOBTrainingStep>()->GetValueByName(Checkpoint);
        if (Value==INDEX_NONE) { Error=TEXT("Checkpoint has no training exercise."); return false; }
        Step=static_cast<EOBTrainingStep>(Value);
    }
    int32 Route=0,Point=0,TargetZone=0,TargetStage=0;
    float YawOffset=0;
    if (bUseSplinePrototype)
    {
        Route=bIntro ? 0 : (bResult || Step==EOBTrainingStep::Breach) ? 5 :
            Checkpoint==TEXT("Shooting2") ? 4 : (Step==EOBTrainingStep::TimedTargets || Step==EOBTrainingStep::Countdown) ? 3 : 1;
        if (!PrototypeRoutePoints.IsValidIndex(Route)) { Error=TEXT("Course route checkpoint is missing."); return false; }
        Point=PrototypeRoutePoints[Route];
        TargetZone=Route<=1 ? 0 : 1;
        YawOffset=Route==1 ? -90.f : Route==3 || Route==4 ? 90.f : 0.f;
    }
    else
    {
        bool bFound=bIntro || bResult;
        TargetZone=bResult ? Zones.Num()-1 : 0;
        for (int32 Z=0; !bFound && Z<Zones.Num(); ++Z)
        {
            const int32 S=Zones[Z].Steps.IndexOfByKey(Step);
            if (S!=INDEX_NONE) { TargetZone=Z; TargetStage=S; bFound=true; }
        }
        if (!bFound || !Zones.IsValidIndex(TargetZone)) { Error=TEXT("Exercise is not configured in this course."); return false; }
        Point=Zones[TargetZone].RailPointIndex;
    }
    if (Point<0 || Point>=Rail->GetSpline()->GetNumberOfSplinePoints() ||
        (Point!=0 && !Rail->FindStopSettings(Point)))
    { Error=TEXT("Checkpoint does not reference a configured rail stop."); return false; }
    if (bUseSplinePrototype && TimedTargets.Num()!=6)
    { Error=TEXT("Prototype requires six training targets."); return false; }
    for (AOBTrainingTarget* Target : TimedTargets)
        if (!IsValid(Target)) { Error=TEXT("A configured training target is no longer valid."); return false; }

    // Stop callbacks and input first so previous dialogue/door/target completions cannot advance the new exercise.
    bInitialSkipOpen=false;
    bStepActive=bTransitionPending=bPrototypeTurning=bPrototypeIntroPending=false;
    GetWorldTimerManager().ClearAllTimersForObject(this);
    StopTrainingAudio(false);
    StopPrototypeNarration();
    StopPrototypeTargets();
    BreachDoor->ResetDoor();
    AccuracyTarget->ClearBulletHoles();
    for (AOBTrainingTarget* Target : TimedTargets) if (Target) Target->ClearBulletHoles();
    Controller->SetTrainingControls(true);
    Controller->ResetQAInputState();
    Player->UnCrouch();
    if (!Spline->JumpToTrainingPointForQA(Point,YawOffset,Error)) return false;

    bQASession=true;
    ApplyLearnerProfile();
    Result=FOBTrainingResultData();
    Result.SessionId=FGuid::NewGuid(); Result.StartedAtUtc=FDateTime::UtcNow();
    Result.LearnerName=LearnerName; Result.LearnerId=LearnerId;
    Result.bUsedTestInputs=true; Result.bPrototypeFlow=bUseSplinePrototype;
    Result.TotalTargets=TimedTargets.Num();
    Result.PassingTargetCount=bUseSplinePrototype ? 6 : PassingTargetCount;
    Result.RecordedFireLimitSeconds=bUseSplinePrototype ? PrototypeZoneSeconds : TimedTargetSeconds;
    SessionStartedAt=StepStartedAt=GetWorld()->GetTimeSeconds();
    for (auto Name : GetQACheckpoints())
    {
        const auto Value=StaticEnum<EOBTrainingStep>()->GetValueByName(Name);
        if (Value!=INDEX_NONE) StepResult(static_cast<EOBTrainingStep>(Value));
    }
    StepResult(EOBTrainingStep::TimedTargets);
    HitTargets.Reset(); ResetAccuracyMeasurement();
    Shots=Hits=Attempt=AimIndex=0; HoldSeconds=0;
    ZoneIndex=TargetZone; StageIndex=TargetStage; PrototypeRouteIndex=Route;
    Deadline=PrototypeDeadline=PrototypeReminderAt=PrototypeHintUntil=0;
    PendingStep=EOBTrainingStep::Idle;
    bRetryMessage=bBreachAccepted=bLastBreachState=bGuideAdvanceReady=false;
    bPrototypePaused=bPrototypeReturnTurn=bPrototypeMockStartPending=bPrototypeWeaponViewReturning=false;
    CurrentDialogueId=PrototypeAfterTurnDialogueId=0;
    PrototypeBasicArrivalYaw=Rail->GetSpline()->GetDirectionAtSplinePoint(Point,ESplineCoordinateSpace::World).Rotation().Yaw;
    PrototypeSignArrivalYaw=PrototypeBasicArrivalYaw;
    CurrentStep=EOBTrainingStep::Idle;
    IntroBrightness=1;
    Controller->SetTrainingAmmo(Step==EOBTrainingStep::TimedTargets ? 20 : 0,20,Step==EOBTrainingStep::TimedTargets ? 20 : 3);
    if (Controller->PlayerCameraManager)
    {
        Controller->PlayerCameraManager->StopCameraFade();
        Controller->PlayerCameraManager->SetManualCameraFade(0,FLinearColor::Black,false);
    }
    if (Overlay)
    {
        Overlay->CancelResultTrigger();
        Overlay->SetTrainingDirector(this);
        Overlay->SetTrainingDialogueAnimationControl(bUseSplinePrototype);
        Overlay->SetVisibility(ESlateVisibility::HitTestInvisible);
    }
    if (bIntro)
    {
        if (bUseSplinePrototype)
        {
            CurrentStep=EOBTrainingStep::Intro;
            OnStepChanged.Broadcast(CurrentStep);
            Spline->OnTrainingTurnCompleted.RemoveAll(this);
            BeginPrototype();
        }
        else BeginStep(EOBTrainingStep::Intro);
    }
    else if (bResult)
    {
        CurrentStep=EOBTrainingStep::Complete;
        FinishTraining(false,TEXT("QA result preview"));
    }
    else if (bUseSplinePrototype)
    {
        PrototypeDeadline=GetWorld()->GetTimeSeconds()+PrototypeZoneSeconds;
        if (Step==EOBTrainingStep::Countdown) BeginPrototypeMockCountdown();
        else BeginPrototypeExercise(Step);
    }
    else
    {
        if (const auto* Stop=Rail->FindStopSettings(Point); Stop && Stop->bOverrideFacingYaw) Spline->SnapTrainingToYaw(Stop->FacingYaw);
        if (Step==EOBTrainingStep::Accuracy) AccuracyTarget->SetTargetEnabled(true,true);
        BeginStep(Step);
    }
    if (!bResult) StartTrainingAudio();
    return true;
#else
    Error=TEXT("QA is disabled in this build."); return false;
#endif
}

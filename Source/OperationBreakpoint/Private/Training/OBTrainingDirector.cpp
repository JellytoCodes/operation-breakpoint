#include "Training/OBTrainingDirector.h"
#include "Core/OBGameInstance.h"
#include "Training/OBTrainingTarget.h"
#include "Training/OBTrainingDoor.h"
#include "Training/OBTrainingProgressSubsystem.h"
#include "Training/OBTrainingStatusWidget.h"
#include "Core/OBPlayerController.h"
#include "Character/OBPlayerCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Character/OBActionComponent.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Widget/OBHUD.h"
#include "Widget/OBPlayerOverlayWidget.h"
#include "World/OBRailPath.h"
#include "MediaPipe/OBMediaPipeOSCSubsystem.h"
#include "MediaPipe/OBMediaPipeInputComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InputComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Misc/App.h"
DEFINE_LOG_CATEGORY_STATIC(LogOBTraining, Log, All);

AOBTrainingDirector::AOBTrainingDirector()
{
    PrimaryActorTick.bCanEverTick = true;
    MockStartSiren=TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Assets/FX/SFX/PlaySound/Start_Siren.Start_Siren")));
    TrainingMusic=TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Assets/FX/SFX/PlaySound/Training_Ground_BGM.Training_Ground_BGM")));
    WalkingSound=TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Assets/FX/SFX/PlaySound/Footsteps_Walk.Footsteps_Walk")));
    MissionEndSiren=TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Assets/FX/SFX/PlaySound/Siren_mission_end.Siren_mission_end")));
    FOBTrainingZoneConfig A, B, C;
    A.Zone = EOBTrainingZone::A;
    A.RailPointIndex = 0;
    A.Steps = {EOBTrainingStep::WeaponCheck, EOBTrainingStep::Reload, EOBTrainingStep::AimHold,
               EOBTrainingStep::Calibration, EOBTrainingStep::Accuracy};
    B.Zone = EOBTrainingZone::B;
    B.RailPointIndex = 1;
    B.Steps = {EOBTrainingStep::Cover, EOBTrainingStep::Countdown, EOBTrainingStep::TimedTargets};
    C.Zone = EOBTrainingZone::C;
    C.RailPointIndex = 2;
    C.Steps = {EOBTrainingStep::Breach};
    Zones = {A, B, C};
    AimPositions = {FVector2D(0.5, 0.45), FVector2D(0.3, 0.4), FVector2D(0.7, 0.4), FVector2D(0.5, 0.65)};
    Instructions.Add(EOBTrainingStep::Welcome, NSLOCTEXT("OBTraining", "Welcome", "훈련장에 오신 것을 환영합니다."));
    Instructions.Add(EOBTrainingStep::WeaponCheck,
                     NSLOCTEXT("OBTraining", "Weapon", "총을 올바르게 들고 방아쇠 버튼을 누르세요."));
    Instructions.Add(EOBTrainingStep::AimHold,
                     NSLOCTEXT("OBTraining", "Aim", "표시된 원 안에 조준점을 유지하세요. 2초 유지 × 4회."));
    Instructions.Add(EOBTrainingStep::Reload, NSLOCTEXT("OBTraining", "Reload", "장전 버튼을 눌러 탄약을 장전하세요."));
    Instructions.Add(EOBTrainingStep::Calibration,
                     NSLOCTEXT("OBTraining", "Calibration", "표적을 조준하고 총기 센서의 캘리브레이션을 완료하세요."));
    Instructions.Add(EOBTrainingStep::Accuracy,
                     NSLOCTEXT("OBTraining", "Accuracy",
                               "표적에 3발을 쏘세요. 두 번째·세 번째 탄착은 첫 탄착에서 허용 반경 이내여야 합니다."));
    Instructions.Add(EOBTrainingStep::Cover, NSLOCTEXT("OBTraining", "Cover", "몸을 낮춰 엄폐 자세를 취하세요."));
    Instructions.Add(
        EOBTrainingStep::Countdown,
        NSLOCTEXT("OBTraining", "Countdown", "기록 사격을 준비하세요. 준비 시간이 끝나면 사격을 시작합니다."));
    Instructions.Add(EOBTrainingStep::TimedTargets,
                     NSLOCTEXT("OBTraining", "Targets",
                               "30초 동안 10개 표적에 사격하세요. 서로 다른 표적 6개 이상 명중하면 통과합니다."));
    Instructions.Add(EOBTrainingStep::Breach,
                     NSLOCTEXT("OBTraining", "Breach", "새 브리칭 동작으로 문을 강행돌파하세요."));
}
void AOBTrainingDirector::BeginPlay()
{
    Super::BeginPlay();
    if (bAutoStart)
        StartTraining();
}
void AOBTrainingDirector::StartTraining()
{
    if (CurrentStep != EOBTrainingStep::Idle)
        return;
    ApplyLearnerProfile();
    Result = FOBTrainingResultData();
    Result.SessionId = FGuid::NewGuid();
    Result.StartedAtUtc = FDateTime::UtcNow();
    Result.LearnerName = LearnerName;
    Result.LearnerId = LearnerId;
    Result.bUsedTestInputs = bEnableKeyboardTestInputs;
    SessionStartedAt = GetWorld()->GetTimeSeconds();
    CurrentStep = EOBTrainingStep::Intro;
    TryInitialize();
}
void AOBTrainingDirector::ApplyLearnerProfile()
{
    if (const auto* Instance = GetGameInstance<UOBGameInstance>(); Instance && Instance->HasLearnerProfile())
    {
        LearnerName = Instance->GetLearnerName();
        LearnerId = Instance->GetLearnerId();
    }
}

bool AOBTrainingDirector::ValidateConfiguration(FString &Error) const
{
    if (Zones.Num() != 3)
    {
        Error = TEXT("Configure three ordered zones A/B/C.");
        return false;
    }
    const AOBRailPath *Rail = Spline->GetRailPath();
    if (!Rail->bExternallyControlled || !Spline->IsAtActionPoint() ||
        Spline->GetCurrentPointIndex() != Zones[0].RailPointIndex)
    {
        Error = TEXT("Training needs an external rail starting at zone A.");
        return false;
    }
    TSet<EOBTrainingStep> Seen;
    TArray<EOBTrainingStep> Ordered;
    for (int32 I = 0; I < Zones.Num(); ++I)
    {
        const auto &Zone = Zones[I];
        if (static_cast<int32>(Zone.Zone) != I || Zone.Steps.IsEmpty() || Zone.RailPointIndex < 0 ||
            Zone.RailPointIndex >= Rail->GetSpline()->GetNumberOfSplinePoints() ||
            (I > 0 && Rail->GetNextStopPointIndex(Zones[I - 1].RailPointIndex) != Zone.RailPointIndex))
        {
            Error = TEXT("Zone points must be consecutive stops with at least one exercise each.");
            return false;
        }
        for (EOBTrainingStep Step : Zone.Steps)
        {
            const bool bExercise = Step == EOBTrainingStep::WeaponCheck || Step == EOBTrainingStep::AimHold ||
                                   Step == EOBTrainingStep::Reload || Step == EOBTrainingStep::Calibration ||
                                   Step == EOBTrainingStep::Accuracy || Step == EOBTrainingStep::Cover ||
                                   Step == EOBTrainingStep::Countdown || Step == EOBTrainingStep::TimedTargets ||
                                   Step == EOBTrainingStep::Breach;
            if (!bExercise || Seen.Contains(Step))
            {
                Error = TEXT("Exercises must be unique; movement and completion are automatic.");
                return false;
            }
            Seen.Add(Step);
            Ordered.Add(Step);
        }
    }
    if (Seen.Num() != 9 || Ordered.Find(EOBTrainingStep::WeaponCheck) > Ordered.Find(EOBTrainingStep::AimHold) ||
        Ordered.Find(EOBTrainingStep::Calibration) + 1 != Ordered.Find(EOBTrainingStep::Accuracy) ||
        Ordered.Find(EOBTrainingStep::Countdown) + 1 != Ordered.Find(EOBTrainingStep::TimedTargets) ||
        Ordered.Find(EOBTrainingStep::Cover) > Ordered.Find(EOBTrainingStep::TimedTargets) ||
        Ordered.Last() != EOBTrainingStep::Breach)
    {
        Error = TEXT("Include nine exercises; calibration/accuracy and countdown/recorded fire must be adjacent; "
                     "breach must be last.");
        return false;
    }
    // Paired exercises must share a zone so their retries cannot move the rail.
    for (const auto &Zone : Zones)
        if (Zone.Steps.Contains(EOBTrainingStep::Calibration) != Zone.Steps.Contains(EOBTrainingStep::Accuracy) ||
            Zone.Steps.Contains(EOBTrainingStep::Countdown) != Zone.Steps.Contains(EOBTrainingStep::TimedTargets))
        {
            Error = TEXT("Keep calibration/accuracy and countdown/recorded fire together in their zones.");
            return false;
        }
    TSet<AOBTrainingTarget *> Unique;
    for (AOBTrainingTarget *Target : TimedTargets)
        if (IsValid(Target) && Target != AccuracyTarget)
            Unique.Add(Target);
    if (!IsValid(AccuracyTarget) || !IsValid(BreachDoor) || RequiredTargetCount < 1 || PassingTargetCount < 1 ||
        PassingTargetCount > RequiredTargetCount || Unique.Num() != RequiredTargetCount ||
        TimedTargets.Num() != RequiredTargetCount || AimPositions.Num() != 4 ||
        AimPositions.ContainsByPredicate([](FVector2D P) {
            return !FMath::IsFinite(P.X) || !FMath::IsFinite(P.Y) || P.X <= 0 || P.X >= 1 || P.Y <= 0 || P.Y >= 1;
        }))
    {
        Error = TEXT("Assign a zeroing target, door, the configured number of distinct recorded-fire targets and four normalized "
                     "aim positions.");
        return false;
    }
    return true;
}
void AOBTrainingDirector::TryInitialize()
{
    Controller = Cast<AOBPlayerController>(UGameplayStatics::GetPlayerController(this, 0));
    if (Controller)
    {
        Controller->SetTrainingControls(true);
        if (Controller->PlayerCameraManager)
            Controller->PlayerCameraManager->SetManualCameraFade(1, FLinearColor::Black, false);
        if (auto *Player = Cast<AOBPlayerCharacter>(Controller->GetPawn()))
        {
            Action = Player->GetActionComponent();
            Spline = Player->GetPlayerSplineComponent();
        }
        if (auto *HUD = Controller->GetOBHUD())
        {
            HUD->ShowPlayerOverlayWidget();
            Overlay = HUD->GetPlayerOverlayWidget();
            if (Overlay && CurrentStep==EOBTrainingStep::Intro) Overlay->SetVisibility(ESlateVisibility::Collapsed);
        }
    }
    if (!Controller || !Action || !Spline || !Spline->GetRailPath())
    {
        if (++StartupAttempts >= 100)
        {
            ConfigurationError(TEXT("Player/controller/external rail not ready."));
            return;
        }
        GetWorldTimerManager().SetTimer(StartupTimer, this, &ThisClass::TryInitialize, 0.1f, false);
        return;
    }
    if (Zones.Num() == 3)
    {
        if (Zones[0].RailPointIndex == 0 && ZoneAPoint != 0)
            Zones[0].RailPointIndex = ZoneAPoint;
        if (Zones[1].RailPointIndex == 1 && ZoneBPoint != 1)
            Zones[1].RailPointIndex = ZoneBPoint;
        if (Zones[2].RailPointIndex == 2 && ZoneCPoint != 2)
            Zones[2].RailPointIndex = ZoneCPoint;
    }
    FString Error;
    if (!(bUseSplinePrototype ? ValidatePrototype(Error) : ValidateConfiguration(Error)))
    {
        ConfigurationError(Error);
        return;
    }
    if (bUseSplinePrototype)
        for (auto Step : {EOBTrainingStep::WeaponCheck, EOBTrainingStep::AimHold, EOBTrainingStep::Cover, EOBTrainingStep::Reload, EOBTrainingStep::Accuracy, EOBTrainingStep::TimedTargets, EOBTrainingStep::Breach}) StepResult(Step);
    else
        for (const auto &Zone : Zones)
            for (EOBTrainingStep Step : Zone.Steps) StepResult(Step);
    Result.TotalTargets = TimedTargets.Num();
    Result.PassingTargetCount = PassingTargetCount;
    Result.RecordedFireLimitSeconds = FMath::Max(0.1f, TimedTargetSeconds);
    bInitialized = true;
    bSavedAutoAdvance = Action->bAutoAdvanceOnEnemyClear;
    Action->bAutoAdvanceOnEnemyClear = false;
    Action->SetExternalActionState(false);
    Spline->OnActionPointReached.AddUObject(this, &ThisClass::HandlePointReached);
    Controller->OnTriggerPressed.AddUObject(this, &ThisClass::HandleTriggerPressed);
    Controller->OnReloadCompleted.AddUObject(this, &ThisClass::HandleReloadCompleted);
    Controller->OnAimCalibrationCompleted.AddUObject(this, &ThisClass::HandleCalibrationCompleted);
    Controller->OnShotResolved.AddUObject(this, &ThisClass::HandleShotResolved);
    // Legacy step messages are separate from the spline dialogue animation lifecycle.
    bUseOverlayMessages = false;
    if (bUseOverlayMessages)
    {
        Overlay->OnSystemMessageShown.AddDynamic(this, &ThisClass::HandleMessageShown);
        Overlay->OnSystemMessageFinished.AddDynamic(this, &ThisClass::HandleMessageFinished);
    }
    BreachDoor->OnDoorBreached.AddDynamic(this, &ThisClass::HandleDoorBreached);
    if (auto *Instance = GetGameInstance())
    {
        Motion = Instance->GetSubsystem<UOBMediaPipeOSCSubsystem>();
        if (Motion)
        {
            Motion->OnBreachStateChanged.AddUObject(this, &ThisClass::NotifyBreachState);
            // Prototype validates the actual character posture in Tick, after movement applies it.
            if (!bEnableKeyboardTestInputs && !bUseSplinePrototype)
                Motion->OnCrouchStateChanged.AddUObject(this, &ThisClass::HandleCoverState);
            bLastBreachState = Motion->IsBreachRecognized();
        }
    }
    PoseInput = Controller->GetPawn()->FindComponentByClass<UOBMediaPipeInputComponent>();
    if (PoseInput)
    {
        bSavedPoseInputEnabled = PoseInput->IsPoseInputEnabled();
        // Legacy keyboard-only test maps retain their isolation. Tutorial permits mixed input.
        if (bEnableKeyboardTestInputs && !bUseSplinePrototype)
            PoseInput->SetPoseInputEnabled(false);
        UE_LOG(LogOBTraining,Display,TEXT("TRAINING_POSE input_enabled=%d keyboard=%d prototype=%d"),
            PoseInput->IsPoseInputEnabled(),bEnableKeyboardTestInputs,bUseSplinePrototype);
    }
    if (Overlay) Overlay->SetTrainingDirector(this);
    StatusWidget = Overlay ? Overlay->GetTrainingMarkers() : nullptr;
    if (StatusWidget)
    {
        StatusWidget->SetDirector(this);
        if (FApp::CanEverRender() && (!Overlay || !Overlay->IsInViewport()))
        {
            ConfigurationError(TEXT("Training status UI could not be mounted; aim target cannot be shown."));
            return;
        }
    }
    else if (FApp::CanEverRender())
    {
        ConfigurationError(TEXT("Training status UI could not be created; aim target cannot be shown."));
        return;
    }
    StartTrainingAudio();
    AccuracyTarget->SetTargetEnabled(false);
    for (AOBTrainingTarget *Target : TimedTargets)
        Target->SetTargetEnabled(false);
    Controller->SetTrainingAmmo(0, 20, 3);
#if !UE_BUILD_SHIPPING
    if (bEnableKeyboardTestInputs)
    {
        EnableInput(Controller);
        InputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &ThisClass::HandleTriggerPressed);
        InputComponent->BindKey(EKeys::C, IE_Pressed, this, &ThisClass::TestCalibration);
        InputComponent->BindKey(EKeys::B, IE_Pressed, this, &ThisClass::TestBreach);
        InputComponent->BindKey(EKeys::V, IE_Pressed, this, &ThisClass::TestCover);
    }
#endif
    if (bUseSplinePrototype) BeginPrototype();
    else BeginStep(EOBTrainingStep::Intro);
}
FOBTrainingStepResult &AOBTrainingDirector::StepResult(EOBTrainingStep Step)
{
    if (auto *Found = Result.Steps.FindByPredicate([Step](const auto &Item) { return Item.Step == Step; }))
        return *Found;
    FOBTrainingStepResult Item;
    Item.Step = Step;
    return Result.Steps.Add_GetRef(Item);
}
void AOBTrainingDirector::BeginZone(int32 Index)
{
    ZoneIndex = Index;
    StageIndex = 0;
    if (!Zones.IsValidIndex(Index))
    {
        ConfigurationError(TEXT("Invalid zone index."));
        return;
    }
    BeginStep(Zones[Index].Steps[0]);
}
void AOBTrainingDirector::BeginStep(EOBTrainingStep Step)
{
    if (Result.bFinished)
        return;
    CurrentStep = Step;
    ++Attempt;
    bStepActive = bTransitionPending = bBreachAccepted = false;
    Deadline = 0;
    UE_LOG(LogOBTraining, Display, TEXT("Training step=%s zone=%d attempt=%d"),
           *StaticEnum<EOBTrainingStep>()->GetNameStringByValue(static_cast<int64>(Step)), ZoneIndex, Attempt);
    GetWorldTimerManager().ClearTimer(StepTimer);
    Controller->SetTrainingControls(true);
    OnStepChanged.Broadcast(Step);
    if (Step == EOBTrainingStep::Complete)
    {
        FinishTraining(false);
        return;
    }
    if (Step == EOBTrainingStep::MovingToB || Step == EOBTrainingStep::MovingToC || Step == EOBTrainingStep::Moving)
    {
        if (Spline->GetRailPath()->GetNextStopPointIndex(Spline->GetCurrentPointIndex()) !=
            Zones[ZoneIndex].RailPointIndex)
        {
            ConfigurationError(TEXT("Next rail stop differs from configured zone."));
            return;
        }
        AccuracyTarget->SetTargetEnabled(false);
        for (AOBTrainingTarget *Target : TimedTargets)
            Target->SetTargetEnabled(false);
        // CompleteAction owns the Action -> Prepare transition; input is still disabled.
        Action->SetExternalActionState(true);
        Action->CompleteAction();
        return;
    }
    Action->SetExternalActionState(false);
    if (Step == EOBTrainingStep::Intro)
    {
        if (Overlay)
            Overlay->SetVisibility(ESlateVisibility::Collapsed);
        if (Controller->PlayerCameraManager)
            Controller->PlayerCameraManager->StartCameraFade(1, 0, FMath::Max(0.01f, IntroFadeSeconds),
                                                             FLinearColor::Black, false, false);
        PendingStep = EOBTrainingStep::Welcome;
        GetWorldTimerManager().SetTimer(TransitionTimer, this, &ThisClass::AdvancePendingStep,
                                        FMath::Max(0.01f, IntroFadeSeconds), false);
        return;
    }
    if (Overlay)
        Overlay->SetVisibility(ESlateVisibility::HitTestInvisible);
    if (Step == EOBTrainingStep::AimHold)
    {
        AimIndex = 0;
        HoldSeconds = 0;
    }
    if (Step == EOBTrainingStep::Reload)
        Controller->SetTrainingAmmo(0, 20, 3);
    if (Step == EOBTrainingStep::Calibration)
    {
        Controller->SetTrainingAmmo(0, 20, 3);
        AccuracyTarget->SetTargetEnabled(true);
    }
    if (Step == EOBTrainingStep::Accuracy)
    {
        ResetAccuracyMeasurement();
        Controller->SetTrainingAmmo(3, 20, 3);
        OnProgressChanged.Broadcast(0, 3);
    }
    if (Step == EOBTrainingStep::Cover)
        Controller->SetCrouchMode(true);
    if (Step == EOBTrainingStep::TimedTargets)
    {
        HitTargets.Reset();
        Result.ShotsFired = Result.ValidHits = Result.UniqueTargetsHit = 0;
        Controller->SetTrainingAmmo(20, 20);
        for (AOBTrainingTarget *Target : TimedTargets)
            Target->SetTargetEnabled(!bUsePopUpTargets, true);
        OnProgressChanged.Broadcast(0, PassingTargetCount);
    }
    if (Step == EOBTrainingStep::Breach)
    {
        Controller->SetCrouchMode(false);
        BreachDoor->ResetDoor();
        if (Motion)
            bLastBreachState = Motion->IsBreachRecognized();
    }
    if (bUseOverlayMessages && Overlay && Overlay->IsSystemMessageReady())
    {
        FText Message = GetInstructionText(Step);
        if (bRetryMessage)
            Message = FText::Format(NSLOCTEXT("OBTraining", "RetryPrompt", "다시 시도하세요.\n{0}"), Message);
        bRetryMessage = false;
        if (Step == EOBTrainingStep::Welcome)
            Overlay->ShowSystemMessage(Message, FMath::Max(0.01f, WelcomeSeconds));
        else
            Overlay->ShowPersistentSystemMessage(Message);
    }
    else if (Step == EOBTrainingStep::Welcome)
        GetWorldTimerManager().SetTimer(TransitionTimer, this, &ThisClass::HandleMessageFinished,
                                        FMath::Max(0.01f, WelcomeSeconds), false);
    else
        HandleMessageShown();
}
float AOBTrainingDirector::GetTimeLimit() const
{
    switch (CurrentStep)
    {
    case EOBTrainingStep::WeaponCheck:
        return FMath::Max(0.1f, WeaponCheckSeconds);
    case EOBTrainingStep::AimHold:
        return FMath::Max(0.1f, AimAttemptSeconds);
    case EOBTrainingStep::Reload:
        return FMath::Max(0.1f, ReloadSeconds);
    case EOBTrainingStep::Cover:
        return FMath::Max(0.1f, CoverSeconds);
    case EOBTrainingStep::Countdown:
        return FMath::Max(0.1f, CountdownSeconds);
    case EOBTrainingStep::TimedTargets:
        return FMath::Max(0.1f, TimedTargetSeconds);
    case EOBTrainingStep::Breach:
        return FMath::Max(0.1f, BreachSeconds);
    default:
        return 0;
    }
}
void AOBTrainingDirector::HandleMessageShown()
{
    if (CurrentStep == EOBTrainingStep::Welcome || Result.bFinished || bTransitionPending || bStepActive)
        return;
    bStepActive = true;
    StepStartedAt = GetWorld()->GetTimeSeconds();
    ++StepResult(CurrentStep).Attempts;
    Action->SetExternalActionState(true);
    const float Limit = GetTimeLimit();
    Deadline = Limit > 0 ? StepStartedAt + Limit : 0;
    if (Limit > 0)
        GetWorldTimerManager().SetTimer(StepTimer, this, &ThisClass::HandleTimeout, Limit, false);
    Controller->SetTrainingControls(
        true, CurrentStep == EOBTrainingStep::Accuracy || CurrentStep == EOBTrainingStep::TimedTargets,
        CurrentStep == EOBTrainingStep::Reload || CurrentStep == EOBTrainingStep::TimedTargets);
    if (CurrentStep == EOBTrainingStep::TimedTargets && bUsePopUpTargets)
        StartPopUpTargets();
    // Reapply a held posture after Prepare discarded the earlier OSC edge.
    if (!bEnableKeyboardTestInputs && Motion && Motion->IsBridgeConnected())
    {
        if (auto *Player = Cast<AOBPlayerCharacter>(Controller->GetPawn()))
        {
            if (Motion->IsCrouching())
                Player->Crouch();
            else
                Player->UnCrouch();
        }
        if (CurrentStep == EOBTrainingStep::Cover)
            HandleCoverState(Motion->IsCrouching());
    }
}
bool AOBTrainingDirector::AcceptsInput(EOBTrainingStep Step) const
{
    if (bUseSplinePrototype && (bPrototypePaused || (PrototypeDeadline > 0 && GetWorld()->GetTimeSeconds() >= PrototypeDeadline))) return false;
    return CurrentStep == Step && bStepActive && !bTransitionPending && !Result.bFinished &&
           (Deadline <= 0 || GetWorld()->GetTimeSeconds() < Deadline);
}
float AOBTrainingDirector::GetRemainingSeconds() const
{
    if (bUseSplinePrototype) return PrototypeDeadline > 0 && !Result.bFinished ? FMath::Max(0.0, PrototypeDeadline - GetWorld()->GetTimeSeconds()) : 0;
    return bStepActive && Deadline > 0 ? FMath::Max(0.0, Deadline - GetWorld()->GetTimeSeconds()) : 0;
}
FVector2D AOBTrainingDirector::GetAimTargetPosition() const
{
    return AimPositions.IsValidIndex(AimIndex) ? AimPositions[AimIndex] : FVector2D(0.5, 0.5);
}
float AOBTrainingDirector::GetAimHoldProgress() const
{
    return FMath::Clamp(HoldSeconds / FMath::Max(0.1f, AimHoldSeconds), 0.f, 1.f);
}
FText AOBTrainingDirector::GetInstructionText(EOBTrainingStep Step) const
{
    if (bUseSplinePrototype && Step == EOBTrainingStep::AimHold) return FText::FromString(TEXT("빨간 점을 각 위치에서 2초 유지하세요."));
    if (bUseSplinePrototype && Step == EOBTrainingStep::Countdown) return FText::FromString(TEXT("시작 신호를 기다리세요."));
    FText Instruction = Instructions.FindRef(Step);
    if (Step == EOBTrainingStep::Accuracy)
        Instruction = FText::Format(NSLOCTEXT("OBTraining", "AccuracyRadius",
            "표적에 3발을 쏘세요. 두 번째·세 번째 탄착은 첫 탄착에서 {0}cm 이내여야 합니다."),
            FText::AsNumber(FMath::Max(0.1f, GroupRadiusCm)));
    if (Step == EOBTrainingStep::TimedTargets && bUsePopUpTargets)
        Instruction = FText::Format(NSLOCTEXT("OBTraining", "PopUpTargets",
            "{0}초 동안 하나씩 올라오는 표적을 사격하세요. {1}개 중 서로 다른 표적 {2}개 이상 명중하면 통과합니다.\n표적은 완전히 올라온 뒤 {3}초 후 내려갑니다."),
            FText::AsNumber(TimedTargetSeconds), FText::AsNumber(TimedTargets.Num()),
            FText::AsNumber(PassingTargetCount), FText::AsNumber(TargetExposureSeconds));
    if (Step == EOBTrainingStep::Calibration)
    {
        if (bEnableKeyboardTestInputs)
            return FText::Format(NSLOCTEXT("OBTraining", "CalibrationTestHint",
                "{0}\n테스트 입력: 표적을 조준하고 C 키를 누르세요.\n보정 완료 후 방아쇠(좌클릭)로 3발 사격하세요."), Instruction);
        return FText::Format(NSLOCTEXT("OBTraining", "CalibrationWaitHint",
            "{0}\n보정 완료 후 탄약 3발이 장전되며 영점 사격을 시작합니다."), Instruction);
    }
    if (bEnableKeyboardTestInputs)
    {
        switch (Step)
        {
        case EOBTrainingStep::Reload:
            return FText::Format(NSLOCTEXT("OBTraining", "ReloadTestHint", "{0}\n마우스 우클릭 또는 건컨트롤러 재장전 버튼을 누르세요."), Instruction);
        case EOBTrainingStep::Cover:
            return FText::Format(NSLOCTEXT("OBTraining", "CoverTestHint", "{0}\n테스트 입력: V 키로 엄폐 자세를 취하세요. V 키를 다시 누르면 일어납니다."), Instruction);
        case EOBTrainingStep::Countdown:
            return FText::Format(NSLOCTEXT("OBTraining", "CountdownTestHint", "{0}\n엄폐를 유지하세요. 시작 후 마우스로 조준, 좌클릭으로 사격, 우클릭으로 재장전합니다."), Instruction);
        case EOBTrainingStep::TimedTargets:
            return FText::Format(NSLOCTEXT("OBTraining", "TargetsTestHint", "{0}\n마우스 조준 · 좌클릭 사격 · 우클릭 재장전 · V 엄폐/해제. 엄폐 자세에서 사격하세요."), Instruction);
        case EOBTrainingStep::Breach:
            return FText::Format(NSLOCTEXT("OBTraining", "BreachTestHint", "{0}\n테스트 입력: B 키를 누르면 문을 열고 훈련 결과를 표시합니다."), Instruction);
        default:
            break;
        }
    }
    return Instruction;
}
FText AOBTrainingDirector::GetStatusText() const
{
    if (bUseSplinePrototype)
    {
        if (Result.bFinished) return FText::GetEmpty();
        if (bPrototypePaused) return FText::FromString(TEXT("기초 훈련 일시중단\n2분 동안 훈련을 완료하지 못했습니다. 입력을 확인한 뒤 계속하기를 누르세요."));
        if (CurrentDialogueId > 0 && (CurrentStep == EOBTrainingStep::Welcome || CurrentStep == EOBTrainingStep::Moving)) return GetPrototypeDialogueText();
        FString Text = FString::Printf(TEXT("%s | 남은 시간 %.1f초\n"), PrototypeRouteIndex == 1 ? TEXT("기초 훈련") : TEXT("모의 훈련"), GetRemainingSeconds());
        if (CurrentStep == EOBTrainingStep::Moving) Text += TEXT("다음 훈련 지점으로 이동 중");
        else if (CurrentStep == EOBTrainingStep::TimedTargets) Text += FString::Printf(TEXT("현재 구역 표적 %d/3 | 전체 %d/6 | 남은 탄약 %d\n마우스 조준 · 좌클릭 사격 · 우클릭 재장전"), GetPrototypeGroupHits(), HitTargets.Num(), Controller ? Controller->GetCurrentAmmo() : 0);
        else if (CurrentStep == EOBTrainingStep::Breach)
        {
            Text += TEXT("양손 손목을 화면에 표시된 영역에 넣어주세요.");
            if (bEnableKeyboardTestInputs) Text += TEXT(" 테스트 입력: B");
        }
        else
        {
            Text += GetInstructionText(CurrentStep).ToString();
            if (CurrentStep == EOBTrainingStep::AimHold) Text += FString::Printf(TEXT("\n조준 완료 %d/%d"), AimIndex, AimPositions.Num());
            if (CurrentStep == EOBTrainingStep::Accuracy) Text += TEXT("\n") + GetAccuracyMeasurementText() + TEXT(" | 엄폐 자세에서 사격");
            if (PrototypeHintId > 0 && GetWorld()->GetTimeSeconds() < PrototypeHintUntil)
            {
                Text += TEXT("\n교관: ") + PrototypeLineText(PrototypeHintId).ToString();
                if (PrototypeHintId == 13 && !AccuracyRetryDetails.IsEmpty()) Text += TEXT("\n") + AccuracyRetryDetails;
            }
        }
        return FText::FromString(Text);
    }
    if (CurrentStep == EOBTrainingStep::Intro || Result.bFinished)
        return FText::GetEmpty();
    FString Text =
        FString::Printf(TEXT("%c구역 | %s"), TCHAR('A' + ZoneIndex), *GetInstructionText(CurrentStep).ToString());
    if (Deadline > 0 && bStepActive)
        Text += FString::Printf(TEXT("\n남은 시간 %.1f초"), GetRemainingSeconds());
    if (CurrentStep == EOBTrainingStep::AimHold)
        Text += FString::Printf(TEXT(" | 완료 %d/4 | 유지 %.1f/%.1f초"), AimIndex, HoldSeconds, AimHoldSeconds);
    if (CurrentStep == EOBTrainingStep::Accuracy)
        Text += FString::Printf(TEXT("\n영점 사격 %d/3 | 유효 탄착 %d | 남은 탄약 %d"), Shots, Hits,
                               Controller ? Controller->GetCurrentAmmo() : 0);
    if (CurrentStep == EOBTrainingStep::TimedTargets)
        Text +=
            FString::Printf(TEXT(" | 표적 %d/%d | 발사 %d | 남은 탄약 %d"), HitTargets.Num(), TimedTargets.Num(),
                            Result.ShotsFired, Controller ? Controller->GetCurrentAmmo() : 0);
    if (bEnableKeyboardTestInputs && (CurrentStep == EOBTrainingStep::Cover ||
        CurrentStep == EOBTrainingStep::Countdown || CurrentStep == EOBTrainingStep::TimedTargets))
    {
        const auto *Player = Controller ? Cast<AOBPlayerCharacter>(Controller->GetPawn()) : nullptr;
        Text += Player && Player->bIsCrouched ? TEXT("\n현재 자세: 엄폐") : TEXT("\n현재 자세: 서 있음 (V 키로 엄폐)");
    }
    if (Result.bUsedTestInputs)
        Text += TEXT("\n개발 테스트 모드: 이수증을 발급하지 않습니다.");
    return FText::FromString(Text);
}
void AOBTrainingDirector::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (bUseSplinePrototype) TickPrototype();
    UpdateTrainingAudio(DeltaSeconds);
    // OSC and keyboard both request movement. Pass only after the character is actually crouched.
    if ((bEnableKeyboardTestInputs || bUseSplinePrototype) && AcceptsInput(EOBTrainingStep::Cover) && Controller && Action &&
        Action->GetActionState() == EOBActionState::Action)
        if (auto *Player = Cast<AOBPlayerCharacter>(Controller->GetPawn()); Player && Player->bIsCrouched)
            HandleCoverState(true);
    if (!AcceptsInput(EOBTrainingStep::AimHold) || !Controller)
        return;
    const ULocalPlayer *LocalPlayer = Controller->GetLocalPlayer();
    const FViewport *Viewport =
        LocalPlayer && LocalPlayer->ViewportClient ? LocalPlayer->ViewportClient->Viewport : nullptr;
    if (!Viewport || !Viewport->HasFocus() || !Viewport->IsForegroundWindow())
    {
        HoldSeconds = 0;
        return;
    }
    float X, Y;
    int32 W, H;
    Controller->GetViewportSize(W, H);
    if (!Controller->GetMousePosition(X, Y) || W <= 0 || H <= 0)
    {
        HoldSeconds = 0;
        return;
    }
    UpdateAimHold(DeltaSeconds, FVector2D(X, Y), FVector2D(W, H));
}
void AOBTrainingDirector::UpdateAimHold(float Seconds, const FVector2D &Cursor, const FVector2D &ViewportSize)
{
    if (!AcceptsInput(EOBTrainingStep::AimHold))
        return;
    if (FVector2D::Distance(Cursor, GetAimTargetPosition() * ViewportSize) > FMath::Max(1.f, AimRadiusPixels))
    {
        HoldSeconds = 0;
        return;
    }
    HoldSeconds += FMath::Max(0.f, Seconds);
    if (HoldSeconds < FMath::Max(0.1f, AimHoldSeconds))
        return;
    HoldSeconds = 0;
    ++AimIndex;
    if (bUseSplinePrototype) PrototypeReminderAt = GetWorld()->GetTimeSeconds() + PrototypeReminderSeconds;
    OnProgressChanged.Broadcast(AimIndex, AimPositions.Num());
    if (AimIndex >= AimPositions.Num())
        CompleteStep(EOBTrainingOutcome::Passed);
    else if (!bUseSplinePrototype)
    {
        Deadline = GetWorld()->GetTimeSeconds() + FMath::Max(0.1f, AimAttemptSeconds);
        GetWorldTimerManager().SetTimer(StepTimer, this, &ThisClass::HandleTimeout, FMath::Max(0.1f, AimAttemptSeconds),
                                        false);
    }
}
void AOBTrainingDirector::TransitionTo(EOBTrainingStep Step, bool bRetry)
{
    if (bTransitionPending || Result.bFinished)
        return;
    if (CurrentStep == EOBTrainingStep::TimedTargets)
        StopPopUpTargets(false);
    PendingStep = Step;
    bRetryMessage = bRetry;
    bTransitionPending = true;
    bStepActive = false;
    Controller->SetTrainingControls(true);
    GetWorldTimerManager().ClearTimer(StepTimer);
    Action->SetExternalActionState(false);
    if (bUseOverlayMessages && Overlay && Overlay->IsSystemMessageReady())
        Overlay->DismissSystemMessage();
    else
        HandleMessageFinished();
}
void AOBTrainingDirector::HandleMessageFinished()
{
    if (Result.bFinished)
        return;
    if (CurrentStep == EOBTrainingStep::Welcome && !bTransitionPending)
    {
        ZoneIndex = StageIndex = 0;
        PendingStep = Zones[0].Steps[0];
        bTransitionPending = true;
    }
    if (bTransitionPending)
        GetWorldTimerManager().SetTimer(TransitionTimer, this, &ThisClass::AdvancePendingStep,
                                        FMath::Max(0.01f, StepDelaySeconds), false);
}
void AOBTrainingDirector::AdvancePendingStep()
{
    BeginStep(PendingStep);
}
void AOBTrainingDirector::CompleteStep(EOBTrainingOutcome Outcome)
{
    if (!bStepActive || bTransitionPending || Result.bFinished)
        return;
    if (bUseSplinePrototype) { CompletePrototypeExercise(Outcome); return; }
    auto &Record = StepResult(CurrentStep);
    Record.Outcome = Outcome;
    Record.ElapsedSeconds += FMath::Max(0.0, GetWorld()->GetTimeSeconds() - StepStartedAt);
    if (CurrentStep == EOBTrainingStep::Calibration && Zones[ZoneIndex].Steps[StageIndex] == EOBTrainingStep::Accuracy)
    {
        TransitionTo(EOBTrainingStep::Accuracy);
        return;
    }
    if (++StageIndex < Zones[ZoneIndex].Steps.Num())
    {
        TransitionTo(Zones[ZoneIndex].Steps[StageIndex]);
        return;
    }
    if (++ZoneIndex < Zones.Num())
    {
        StageIndex = 0;
        TransitionTo(ZoneIndex == 1 ? EOBTrainingStep::MovingToB : EOBTrainingStep::MovingToC);
    }
    else
        TransitionTo(EOBTrainingStep::Complete);
}
void AOBTrainingDirector::RetryPrerequisite()
{
    auto &Record = StepResult(CurrentStep);
    if (Record.Attempts >= FMath::Max(1, MaxPrerequisiteAttempts))
    {
        AbortTraining(TEXT("기초 훈련의 재시도 횟수를 초과했습니다. 입력과 자세를 확인하세요."));
        return;
    }
    Record.ElapsedSeconds += FMath::Max(0.0, GetWorld()->GetTimeSeconds() - StepStartedAt);
    OnRetry.Broadcast(
        NSLOCTEXT("OBTraining", "Timeout", "제한 시간이 초과되었습니다. 입력과 자세를 확인하고 다시 시도하세요."));
    TransitionTo(CurrentStep, true);
}
void AOBTrainingDirector::HandleTriggerPressed()
{
    if (bUseSplinePrototype && IsPrototypeGuideVisible() && bGuideAdvanceReady)
    {
        DismissPrototypeDialogue();
        return;
    }
    if (AcceptsInput(EOBTrainingStep::WeaponCheck))
        CompleteStep(EOBTrainingOutcome::Passed);
}
void AOBTrainingDirector::HandleReloadCompleted()
{
    if (AcceptsInput(EOBTrainingStep::Reload) && Controller->GetCurrentAmmo() > 0)
        CompleteStep(EOBTrainingOutcome::Passed);
}
void AOBTrainingDirector::HandleCalibrationCompleted()
{
    if (AcceptsInput(EOBTrainingStep::Calibration))
        CompleteStep(EOBTrainingOutcome::Passed);
}
void AOBTrainingDirector::HandleCoverState(bool bCrouching)
{
    if (bCrouching && AcceptsInput(EOBTrainingStep::Cover))
        CompleteStep(EOBTrainingOutcome::Passed);
}
void AOBTrainingDirector::HandleShotResolved(const FHitResult &Hit)
{
    if (auto* HitTarget=Cast<AOBTrainingTarget>(Hit.GetActor()); HitTarget && Hit.bBlockingHit && Hit.GetComponent()==HitTarget->BoardMesh)
        HitTarget->SpawnBulletHole(Hit);
    if (AcceptsInput(EOBTrainingStep::Accuracy))
    {
        ++Shots;
        bAccuracyLastShotHit = AccuracyTarget->IsScoringHit(Hit);
        LatestAccuracyErrorCm = -1;
        if (bAccuracyLastShotHit)
        {
            if (Shots == 1)
            {
                FirstImpact = Hit.ImpactPoint;
                bAccuracyHasReference = true;
            }
            if (bAccuracyHasReference)
            {
                LatestAccuracyErrorCm = FVector::VectorPlaneProject(
                    Hit.ImpactPoint - FirstImpact, AccuracyTarget->GetScoringPlaneNormal()).Size();
                MaxAccuracyErrorCm = FMath::Max(MaxAccuracyErrorCm, LatestAccuracyErrorCm);
                if (LatestAccuracyErrorCm <= FMath::Max(0.1f, GroupRadiusCm)) ++Hits;
            }
            AccuracyTarget->HandleScoringHit(Hit.ImpactPoint);
        }
        UE_LOG(LogOBTraining, Display, TEXT("Zeroing shot=%d boardHit=%d reference=%d offset=%.4fcm max=%.4fcm limit=%.2fcm valid=%d"),
            Shots, bAccuracyLastShotHit, bAccuracyHasReference, LatestAccuracyErrorCm, MaxAccuracyErrorCm, FMath::Max(0.1f, GroupRadiusCm), Hits);
        OnProgressChanged.Broadcast(Hits, 3);
        if (Shots < 3)
            return;
        if (Hits == 3)
            CompleteStep(EOBTrainingOutcome::Passed);
        else
        {
            if (bUseSplinePrototype)
            {
                const FString Details = FString::Printf(TEXT("이전 3발: 유효 %d/3 | 최대 오차 %.2fcm / 허용 %.2fcm%s"),
                    Hits, MaxAccuracyErrorCm, FMath::Max(0.1f, GroupRadiusCm), bAccuracyHasReference ? TEXT("") : TEXT(" | 첫 탄 미명중"));
                StepResult(CurrentStep).ElapsedSeconds += FMath::Max(0.0, GetWorld()->GetTimeSeconds() - StepStartedAt);
                AccuracyTarget->ClearBulletHoles();
                StepResult(EOBTrainingStep::Accuracy).Outcome = EOBTrainingOutcome::NotRun;
                StepResult(EOBTrainingStep::Reload).Outcome = EOBTrainingOutcome::NotRun;
                ResetAccuracyMeasurement();
                // Failure is not progress: keep the existing inactivity deadline until reload succeeds.
                const double InactivityDeadline = PrototypeDeadline;
                BeginPrototypeExercise(EOBTrainingStep::Reload);
                PrototypeDeadline = InactivityDeadline;
                AccuracyRetryDetails = Details;
                PlayPrototypeHint(13);
                return;
            }
            auto &Record = StepResult(CurrentStep);
            if (Record.Attempts >= FMath::Max(1, MaxAccuracyAttempts))
            {
                AbortTraining(TEXT("영점 사격이 재시도 기준을 초과했습니다."));
                return;
            }
            Record.ElapsedSeconds += FMath::Max(0.0, GetWorld()->GetTimeSeconds() - StepStartedAt);
            OnRetry.Broadcast(NSLOCTEXT("OBTraining", "GroupFailed",
                                        "탄착군 기준을 충족하지 못했습니다. 재캘리브레이션 후 다시 시도하세요."));
            TransitionTo(EOBTrainingStep::Calibration, true);
        }
    }
    else if (AcceptsInput(EOBTrainingStep::TimedTargets))
    {
        ++Result.ShotsFired;
        auto *Target = Cast<AOBTrainingTarget>(Hit.GetActor());
        if (bUseSplinePrototype)
        {
            if (Target && IsPrototypeGroupTarget(Target) && !HitTargets.Contains(Target) && Target->IsScoringHit(Hit))
            {
                ++Result.ValidHits;
                HitTargets.Add(Target);
                Result.UniqueTargetsHit = HitTargets.Num();
                Target->HandleScoringHit(Hit.ImpactPoint);
                OnProgressChanged.Broadcast(GetPrototypeGroupHits(), 3);
                if (Target == ActiveTimedTarget)
                {
                    GetWorldTimerManager().ClearTimer(PopUpTimer);
                    LowerActiveTarget();
                }
                else Target->SetTargetEnabled(false, false, true);
                if (GetPrototypeGroupHits() == 3) CompleteStep(EOBTrainingOutcome::Passed);
            }
            return;
        }
        if (Target && TimedTargets.Contains(Target) && Target->IsScoringHit(Hit) &&
            (!bUsePopUpTargets || Target == ActiveTimedTarget))
        {
            ++Result.ValidHits;
            HitTargets.Add(Target);
            Result.UniqueTargetsHit = HitTargets.Num();
            Target->HandleScoringHit(Hit.ImpactPoint);
            OnProgressChanged.Broadcast(HitTargets.Num(), PassingTargetCount);
        }
    }
}
void AOBTrainingDirector::ResetAccuracyMeasurement()
{
    Shots = Hits = 0;
    FirstImpact = FVector::ZeroVector;
    LatestAccuracyErrorCm = -1;
    MaxAccuracyErrorCm = 0;
    bAccuracyHasReference = bAccuracyLastShotHit = false;
    AccuracyRetryDetails.Reset();
}

FString AOBTrainingDirector::GetAccuracyMeasurementText() const
{
    FString Text = FString::Printf(TEXT("영점 사격 %d/3 | 유효 탄착 %d | 첫 탄착 기준 반경 %.2fcm"), Shots, Hits, FMath::Max(0.1f, GroupRadiusCm));
    if (LatestAccuracyErrorCm >= 0)
        Text += FString::Printf(TEXT("\n이번 오차 %.2fcm | 최대 %.2fcm"), LatestAccuracyErrorCm, MaxAccuracyErrorCm);
    else if (Shots > 0)
        Text += bAccuracyLastShotHit ? TEXT("\n첫 탄착 기준이 없습니다") : TEXT("\n이번 탄은 표적에 명중하지 않았습니다");
    return Text;
}
AOBTrainingTarget* AOBTrainingDirector::GetActiveTimedTarget() const
{
    return bPopUpRunning && IsValid(ActiveTimedTarget) && ActiveTimedTarget->IsTargetRaised()
        ? ActiveTimedTarget.Get() : nullptr;
}
void AOBTrainingDirector::StartPopUpTargets()
{
    StopPopUpTargets();
    PopUpOrder.Reset();
    PopUpCursor = 0;
    LastPopUpIndex = INDEX_NONE;
    bPopUpRunning = true;
    for (AOBTrainingTarget* Target : TimedTargets)
        if (IsValid(Target) && (!bUseSplinePrototype || IsPrototypePopUpCandidate(Target)))
            Target->OnTargetPoseReached.AddUObject(this, &ThisClass::HandleTargetPoseReached);
    RaiseNextTarget();
}
void AOBTrainingDirector::StopPopUpTargets(bool bImmediate)
{
    bPopUpRunning = false;
    ActiveTimedTarget = nullptr;
    GetWorldTimerManager().ClearTimer(PopUpTimer);
    for (AOBTrainingTarget* Target : TimedTargets)
        if (IsValid(Target))
        {
            Target->OnTargetPoseReached.RemoveAll(this);
            Target->SetTargetEnabled(false, bImmediate);
        }
}
void AOBTrainingDirector::RaiseNextTarget()
{
    if (!bPopUpRunning || !AcceptsInput(EOBTrainingStep::TimedTargets)) return;
    if (PopUpCursor >= PopUpOrder.Num())
    {
        PopUpOrder.Reset();
        for (int32 I = 0; I < TimedTargets.Num(); ++I)
            if (IsValid(TimedTargets[I]) && (!bUseSplinePrototype || IsPrototypePopUpCandidate(TimedTargets[I]))) PopUpOrder.Add(I);
        for (int32 I = PopUpOrder.Num() - 1; I > 0; --I)
            PopUpOrder.Swap(I, FMath::RandRange(0, I));
        // Each stand appears once per round; the boundary also avoids consecutive repeats.
        if (PopUpOrder.Num() > 1 && PopUpOrder[0] == LastPopUpIndex)
            PopUpOrder.Swap(0, 1);
        PopUpCursor = 0;
    }
    if (PopUpOrder.IsEmpty()) { ActiveTimedTarget = nullptr; return; }
    LastPopUpIndex = PopUpOrder[PopUpCursor++];
    ActiveTimedTarget = TimedTargets[LastPopUpIndex];
    if (!IsValid(ActiveTimedTarget) || (bUseSplinePrototype && !IsPrototypePopUpCandidate(ActiveTimedTarget))) { RaiseNextTarget(); return; }
    ActiveTimedTarget->SetTargetEnabled(true);
}
void AOBTrainingDirector::HandleTargetPoseReached(AOBTrainingTarget* Target, bool bRaised)
{
    if (!bPopUpRunning || Target != ActiveTimedTarget || !AcceptsInput(EOBTrainingStep::TimedTargets)) return;
    // Exposure starts after the raise animation, and the next raise waits for retraction to finish.
    if (bRaised)
        GetWorldTimerManager().SetTimer(PopUpTimer, this, &ThisClass::LowerActiveTarget,
                                       FMath::Max(0.1f, TargetExposureSeconds), false);
    else if (TargetGapSeconds > 0)
        GetWorldTimerManager().SetTimer(PopUpTimer, this, &ThisClass::RaiseNextTarget, TargetGapSeconds, false);
    else
        PopUpTimer = GetWorldTimerManager().SetTimerForNextTick(this, &ThisClass::RaiseNextTarget);
}
void AOBTrainingDirector::LowerActiveTarget()
{
    if (bPopUpRunning && AcceptsInput(EOBTrainingStep::TimedTargets) && IsValid(ActiveTimedTarget))
        ActiveTimedTarget->SetTargetEnabled(false, false, true);
}
void AOBTrainingDirector::EvaluateTimedTargets()
{
    Result.RecordedFireSeconds = FMath::Min(FMath::Max(0.0, GetWorld()->GetTimeSeconds() - StepStartedAt),
                                            double(FMath::Max(0.1f, TimedTargetSeconds)));
    CompleteStep(HitTargets.Num() >= PassingTargetCount ? EOBTrainingOutcome::Passed : EOBTrainingOutcome::Failed);
}
void AOBTrainingDirector::HandleTimeout()
{
    if (!bStepActive || bTransitionPending || Result.bFinished)
        return;
    if (CurrentStep == EOBTrainingStep::Countdown)
        CompleteStep(EOBTrainingOutcome::Passed);
    else if (CurrentStep == EOBTrainingStep::TimedTargets)
        EvaluateTimedTargets();
    else if (CurrentStep == EOBTrainingStep::Breach)
    {
        if (!bBreachAccepted)
            CompleteStep(EOBTrainingOutcome::Failed);
    }
    else
        RetryPrerequisite();
}
void AOBTrainingDirector::HandlePointReached(int32 Point)
{
    if (bUseSplinePrototype)
    {
        if (!Result.bFinished && CurrentStep == EOBTrainingStep::Moving && PrototypeRoutePoints.IsValidIndex(PrototypeRouteIndex) && Point == PrototypeRoutePoints[PrototypeRouteIndex]) ArrivePrototype(PrototypeRouteIndex);
        return;
    }
    if ((CurrentStep == EOBTrainingStep::MovingToB || CurrentStep == EOBTrainingStep::MovingToC ||
         CurrentStep == EOBTrainingStep::Moving) &&
        Zones.IsValidIndex(ZoneIndex) && Point == Zones[ZoneIndex].RailPointIndex)
        BeginZone(ZoneIndex);
}
void AOBTrainingDirector::NotifyBreachState(bool bRecognized)
{
    const bool bNewMotion = bRecognized && !bLastBreachState;
    bLastBreachState = bRecognized;
    if (bNewMotion && !bBreachAccepted && AcceptsInput(EOBTrainingStep::Breach))
    {
        bBreachAccepted = true;
        Deadline = 0;
        GetWorldTimerManager().ClearTimer(StepTimer);
        BreachDoor->TryBreach(Attempt);
    }
}
void AOBTrainingDirector::HandleDoorBreached(AOBTrainingDoor *Door, int32 AttemptId)
{
    if (Door == BreachDoor && AttemptId == Attempt && Door->IsBreached() && bBreachAccepted &&
        AcceptsInput(EOBTrainingStep::Breach))
        CompleteStep(EOBTrainingOutcome::Passed);
}
void AOBTrainingDirector::FinishTraining(bool bAborted, const FString &Reason, bool bShowResult)
{
    if (Result.bFinished || !Result.SessionId.IsValid())
        return;
    StopTrainingAudio(true);
    // A basic-training pause/abort is not the end of the timed scenario.
    if (bShowResult && !bAborted && (!bUseSplinePrototype || PrototypeRouteIndex>=3))
        PlayTrainingSignal(MissionEndSiren,TEXT("MissionEnd"));
    StopPopUpTargets(false);
    StopPrototypeNarration();
    GetWorldTimerManager().ClearTimer(PrototypeIntroTimer);
    GetWorldTimerManager().ClearTimer(PrototypeMockStartTimer);
    bPrototypeMockStartPending=false;
    bPrototypeWeaponViewReturning=false;
    if (Spline) Spline->ClearTrainingCameraFocus(true);
    bPrototypeIntroPending = false;
    if (bUseSplinePrototype && Spline) Spline->StopTrainingMovement();
    Result.bFinished = true;
    Result.bAborted = bAborted;
    Result.AbortReason = Reason;
    Result.CompletedAtUtc = FDateTime::UtcNow();
    Result.TotalSeconds = FMath::Max(0.0, GetWorld()->GetTimeSeconds() - SessionStartedAt);
    Result.bPassed = Result.MeetsCompletionRequirements();
    bStepActive = bTransitionPending = false;
    Deadline = 0;
    GetWorldTimerManager().ClearTimer(StartupTimer);
    GetWorldTimerManager().ClearTimer(StepTimer);
    GetWorldTimerManager().ClearTimer(TransitionTimer);
    if (Action)
        Action->SetExternalActionState(false);
    if (Controller)
    {
        Controller->SetTrainingControls(true);
        if (Controller->PlayerCameraManager)
            Controller->PlayerCameraManager->StopCameraFade();
    }
    if (AccuracyTarget)
        AccuracyTarget->SetTargetEnabled(false);
    for (AOBTrainingTarget *Target : TimedTargets)
        if (Target)
            Target->SetTargetEnabled(false);
    // PlayerOverlay switches child visibility when it opens the result.
    if (Overlay)
        Overlay->DismissSystemMessage();
    if (auto *Instance = GetGameInstance(); Instance && !bQASession)
        if (auto *Progress = Instance->GetSubsystem<UOBTrainingProgressSubsystem>())
        {
            const bool bSaved = Progress->CommitTrainingResult(Result);
            if (bSaved && bAutoExportPdf && Result.bPassed)
                Progress->RequestPdfExport(Result.SessionId);
        }
    OnTrainingFinished.Broadcast(Result);
    if (Result.bPassed)
        OnTrainingCompleted.Broadcast();
    // Dim only the world behind the completed-training panel; keep the UI opaque.
    if (bShowResult && Result.IsScenarioPassed() && Controller && Controller->PlayerCameraManager)
    {
        Controller->PlayerCameraManager->StartCameraFade(0.f, .3f, .35f, FLinearColor::Black, false, true);
        UE_LOG(LogOBTraining, Display, TEXT("TRAINING_COMPLETE background_fade=0.30 hold=1"));
    }
    if (bShowResult && Controller)
        if (auto *HUD = Controller->GetOBHUD())
            HUD->ShowTrainingResult(Result);
    if (bUseSplinePrototype && bShowResult && Overlay)
        PresentPrototypeDialogue(bAborted ? 20 : Result.IsScenarioPassed() ? 21 : 22,false,true);
}
void AOBTrainingDirector::AbortTraining(const FString &Reason)
{
    if (Result.bFinished)
        return;
    if (bStepActive)
    {
        auto &Record = StepResult(CurrentStep);
        Record.Outcome = EOBTrainingOutcome::Aborted;
        Record.ElapsedSeconds += FMath::Max(0.0, GetWorld()->GetTimeSeconds() - StepStartedAt);
    }
    CurrentStep = EOBTrainingStep::Error;
    OnStepChanged.Broadcast(CurrentStep);
    FinishTraining(true, Reason);
}
void AOBTrainingDirector::ConfigurationError(const FString &Reason)
{
    UE_LOG(LogOBTraining, Error, TEXT("Training setup: %s"), *Reason);
    AbortTraining(Reason);
}
void AOBTrainingDirector::TestReload()
{
    if (Controller)
    {
        Result.bUsedTestInputs = true;
        Controller->CompleteReload();
    }
}
void AOBTrainingDirector::TestCalibration()
{
    if (Controller)
    {
        Result.bUsedTestInputs = true;
        Controller->NotifyAimCalibrationCompleted();
    }
}
void AOBTrainingDirector::TestBreach()
{
    if (!bEnableKeyboardTestInputs || !AcceptsInput(EOBTrainingStep::Breach) || !Action ||
        Action->GetActionState() != EOBActionState::Action)
        return;
    Result.bUsedTestInputs = true;
    NotifyBreachState(false);
    NotifyBreachState(true);
}
void AOBTrainingDirector::TestCover()
{
    if (!bEnableKeyboardTestInputs || !Controller || !Action ||
        Action->GetActionState() != EOBActionState::Action ||
        !(AcceptsInput(EOBTrainingStep::Cover) || AcceptsInput(EOBTrainingStep::Countdown) ||
          AcceptsInput(EOBTrainingStep::TimedTargets) || (bUseSplinePrototype && (AcceptsInput(EOBTrainingStep::Reload) || AcceptsInput(EOBTrainingStep::Accuracy)))))
        return;
    Result.bUsedTestInputs = true;
    if (auto *Player = Cast<AOBPlayerCharacter>(Controller->GetPawn()))
    {
        // Use the requested pose so rapid toggles also work before the next movement tick.
        if (Player->GetCharacterMovement()->bWantsToCrouch)
            Player->UnCrouch();
        else
            Player->Crouch();
    }
}
void AOBTrainingDirector::EndPlay(const EEndPlayReason::Type Reason)
{
    StopTrainingAudio(false);
    StopPopUpTargets();
    if (Result.SessionId.IsValid() && !Result.bFinished)
        FinishTraining(true, TEXT("훈련이 종료 전에 중단되었습니다."), false);
    GetWorldTimerManager().ClearTimer(StartupTimer);
    GetWorldTimerManager().ClearTimer(StepTimer);
    GetWorldTimerManager().ClearTimer(TransitionTimer);
    StopPrototypeNarration();
    GetWorldTimerManager().ClearTimer(PrototypeIntroTimer);
    GetWorldTimerManager().ClearTimer(PrototypeMockStartTimer);
    bPrototypeMockStartPending=false;
    if (Controller)
    {
        Controller->OnTriggerPressed.RemoveAll(this);
        Controller->OnReloadCompleted.RemoveAll(this);
        Controller->OnAimCalibrationCompleted.RemoveAll(this);
        Controller->OnShotResolved.RemoveAll(this);
        Controller->SetTrainingControls(false);
        if (Controller->PlayerCameraManager)
            Controller->PlayerCameraManager->StopCameraFade();
        DisableInput(Controller);
    }
    if (PoseInput)
        PoseInput->SetPoseInputEnabled(bSavedPoseInputEnabled);
    if (Action && bInitialized)
        Action->bAutoAdvanceOnEnemyClear = bSavedAutoAdvance;
    if (Spline)
    {
        Spline->OnActionPointReached.RemoveAll(this);
        Spline->OnTrainingTurnCompleted.RemoveAll(this);
    }
    if (Motion)
    {
        Motion->OnBreachStateChanged.RemoveAll(this);
        Motion->OnCrouchStateChanged.RemoveAll(this);
    }
    if (BreachDoor)
        BreachDoor->OnDoorBreached.RemoveAll(this);
    if (Overlay)
    {
        Overlay->OnSystemMessageShown.RemoveAll(this);
        Overlay->OnSystemMessageFinished.RemoveAll(this);
        Overlay->OnSubtitleShown.RemoveAll(this);
        Overlay->OnSubtitleFinished.RemoveAll(this);
    }
    if (Overlay && !Result.bFinished) Overlay->SetStageMode();
    Super::EndPlay(Reason);
}

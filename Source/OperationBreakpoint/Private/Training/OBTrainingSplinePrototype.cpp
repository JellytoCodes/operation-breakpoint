#include "Training/OBTrainingDirector.h"
#include "Training/OBTrainingTarget.h"
#include "Training/OBRailTrainingTarget.h"
#include "Training/OBTrainingDoor.h"
#include "Character/OBActionComponent.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Core/OBPlayerController.h"
#include "World/OBRailPath.h"
#include "Components/SplineComponent.h"
#include "Engine/World.h"
#include "Camera/PlayerCameraManager.h"
#include "TimerManager.h"
#include "MediaPipe/OBMediaPipeOSCSubsystem.h"
#include "MediaPipe/OBMediaPipeInputComponent.h"
#include "Widget/OBPlayerOverlayWidget.h"
#include "Widget/OBHudPanelWidget.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/SavePackage.h"
#include "UObject/Package.h"
#endif

namespace
{
FOBTrainingDialogueRow DefaultPrototypeRow(int32 Id)
{
    static const TCHAR* Lines[] = {
        TEXT(""), TEXT("기초 훈련을 시작합니다. 교관의 명령에 따라 훈련을 진행합니다."),
        TEXT("반갑습니다. 오늘 전체훈련 진행을 맡은 이중구 교관입니다."),
        TEXT("이곳 훈련의 핵심은 총기 상태 확인, 조준, 엄폐, 탄창 결합, 영점 조절 등을 배우겠습니다. 훈련병은 앞에 안내판을 확인하도록합니다."),
        TEXT("먼저 훈련장 진입 시 앞에 있는 총기를 들고 총기 상태를 확인할 수 있도록 합니다."),
        TEXT("총기 확인이 됐으면 빨간 점을 향해 2초간 조준을 실시합니다."),
        TEXT("훈련병은 엄폐를 통해 안전한 상태에서 장전을 할 수 있도록 합니다."),
        TEXT("장전이 됐으면 앞에 있는 표적지를 향해 3회 사격 실시합니다."),
        TEXT("훈련병은 이제 실제 훈련 구역으로 이동할 수 있도록 합니다."),
        TEXT("훈련병 총을 들고 총기 상태 확인합니다!"), TEXT("훈련병! 빨간 점 목표를 다시 조준합니다!"),
        TEXT("훈련병! 자세를 낮춰 엄폐합니다."), TEXT("훈련병! 탄알집 결합!"),
        TEXT("영점 불합격! 표적 중앙을 향해 3회 사격 실시!"),
        TEXT("영점 합격! 이것으로 기초훈련을 종료하겠습니다."),
        TEXT("훈련병은 다음 훈련구역으로 이동하겠습니다."),
        TEXT("이곳은 모의훈련장입니다. 훈련병은 안내판을 확인하도록합니다."),
        TEXT("이곳 훈련의 핵심은 모의 사격, 진입 훈련이며 2가지 과정을 동시에 진행합니다."),
        TEXT("모의 훈련은 사이렌 소리에 맞춰 시작하며, 2분 안에 6개 이상 표적 명중 후 진입훈련 폭약 설치까지 합격입니다."),
        TEXT("훈련병은 이제 실제 모의 훈련 구역으로 이동할 수 있도록 합니다."),
        TEXT("2분간 훈련을 완수 및 장비 입력을 확인하지 못해 훈련을 중단합니다."),
        TEXT("모든 훈련을 이수하였습니다. 미션 진입 또는 재훈련을 선택하십시오."),
        TEXT("모든 훈련을 이수하였습니다. 불합격 훈련이 있습니다. 미션 진입 또는 재훈련을 권장합니다.")};
    FOBTrainingDialogueRow Row;
    if (Id <= 0 || Id >= UE_ARRAY_COUNT(Lines)) return Row;
    Row.Script=FText::FromString(Lines[Id]);
    Row.Character=Id==1 || Id>=20 ? EOBTrainingDialogueCharacter::System : EOBTrainingDialogueCharacter::Instructor;
    Row.Location=Id<=15 || Id==20 ? EOBTrainingDialogueLocation::Zone_A : EOBTrainingDialogueLocation::Zone_B;
    Row.Layer=Id==1 || Id>=20 ? EOBTrainingDialogueLayer::Main : EOBTrainingDialogueLayer::Sub;
    Row.bUIOpen=(Id>=3 && Id<=7) || Id==17 || Id==18 || Id>=20;
    Row.Event=Id<=8 ? EOBTrainingDialogueEvent::A_Guide : Id<=14 || Id==20 ? EOBTrainingDialogueEvent::A_Training :
        Id<=19 ? EOBTrainingDialogueEvent::B_Guide : EOBTrainingDialogueEvent::B_Training;
    Row.Condition=(Id>=9 && Id<=13) || Id==20 || Id==22 ? EOBTrainingDialogueCondition::Fail :
        Id==14 || Id==21 ? EOBTrainingDialogueCondition::Pass : EOBTrainingDialogueCondition::None;
    Row.Prev_ID=(Id>=2 && Id<=8) || Id==15 || (Id>=17 && Id<=19) ? Id-1 : 0;
    Row.Delay_Time=Id==2 || Id==3 || Id==17 ? 0.5f : 0.f;
    static const float Durations[]={0,3,4,0,0,0,0,0,4,4,3,3,3,5,5,4,4,0,0,4,4,0,0};
    Row.Display_Time=Durations[Id];
    static const TCHAR* Voices[]={TEXT(""),TEXT("VO_Sys_Intro"),TEXT("VO_Train_Intro"),
        TEXT("VO_Train_A_Guide_0"),TEXT("VO_Train_A_Guide_1"),TEXT("VO_Train_A_Guide_2"),TEXT("VO_Train_A_Guide_3"),TEXT("VO_Train_A_Guide_4"),
        TEXT("VO_Train_A_Move"),TEXT("VO_Train_Trigger_Fail"),TEXT("VO_Train_Aim_Fail"),TEXT("VO_Train_Cover_Fail"),TEXT("VO_Train_Reload_Fail"),
        TEXT("VO_Train_Zero_Fail"),TEXT("VO_Train_A_Pass"),TEXT("VO_Train_A_Move_B"),TEXT("VO_Train_B_Intro"),
        TEXT("VO_Train_B_Guide_0"),TEXT("VO_Train_B_Guide_1"),TEXT("VO_Train_B_Move"),
        TEXT("VO_Sys_Train_A_Fail"),TEXT("VO_Sys_Train_B_Success"),TEXT("VO_Sys_Train_B_Fail")};
    Row.Voice=TSoftObjectPtr<USoundBase>(FSoftObjectPath(FString::Printf(TEXT("/Game/Assets/FX/SFX/TrainingScriptSound/%s.%s"),Voices[Id],Voices[Id])));
#if WITH_EDITORONLY_DATA
    static const TCHAR* Notes[]={TEXT(""),
        TEXT("기초 훈련 안내판 앞에서 시작 (스폰 이동 없음). 가운데 시스템 문구만, UI 없음"),
        TEXT("1번 종료 0.5초 후 자동 재생"),
        TEXT("2번 종료 후 음성과 동시에 훈련장 안내 UI(알림창 UI 1) 열림. Display 0 = 방아쇠 입력까지 유지 → 4번 (B구역 16→17과 같은 방식)"),
        TEXT("안내 UI 1/4 (격발 확인) = 알림창 UI 2. 방아쇠 입력 시 다음 장"),
        TEXT("안내 UI 2/4 (조준) = 알림창 UI 3"),
        TEXT("안내 UI 3/4 (엄폐+장전) = 알림창 UI 4"),
        TEXT("안내 UI 4/4 (영점) = 알림창 UI 5. 방아쇠 입력 시 Move_Next"),
        TEXT("마지막 안내 UI 닫힘 → 훈련 지점으로 스플라인 이동. 도착 시 2분 타이머 시작 + 통과 기준 패널(23) 표시 + 격발 확인 자동 시작 (음성 없음)"),
        TEXT("5초 내 미입력 → 음성 후 재시도. 재시도 한도(목업 임시값 2회) 초과 시 Fail 기록 후 다음 훈련 (진짜 실패 아님)"),
        TEXT("30초 내 4회 미달성 또는 유지 이탈(2-5). 연속 재생 쿨다운 필요"),
        TEXT("10초 내 엄폐 미판정 (2-6 인식값 0.5 미만)"),
        TEXT("엄폐 상태에서 15초 내 탄창 미결합"),
        TEXT("첫 탄착점 기준 5cm 초과 → 재사격"),
        TEXT("A구역 Pass 음성 (구역당 1회). 2분 타이머 정지, 통과 기준 패널 닫힘"),
        TEXT("14번 종료 후 B구역 표지판으로 스플라인 이동 → 도착 시 Sign_Enter(Zone_B)"),
        TEXT("B 표지판 도착. 음성만 (UI 없음)"),
        TEXT("안내 UI 1/2 (B 개요) = 알림창 UI 10. 16번 음성 종료 후 열림. Display 0 = 방아쇠 입력까지"),
        TEXT("안내 UI 2/2 (모의 훈련) = 알림창 UI 6. 방아쇠 입력 시 Move_Next"),
        TEXT("모의 훈련 지점으로 스플라인 이동. 도착 시 사이렌 SFX + 2분 타이머 + 통과 기준 패널(27). 훈련 중 대사 없음. 2분 초과 = 중단 아님 → 훈련 종료 UI Fail(22)"),
        TEXT("알림창 UI 7 (유일한 진짜 실패, 기초 훈련장 전용). 발생: 기초 훈련 4단계를 2분 내 미완수 또는 장비 미인식. 4초 후 메인 메뉴"),
        TEXT("훈련 종료 UI (알림창 UI 8). 모의 훈련 2분 내 폭약 설치 완료 + 기초 훈련 전부 Pass. Stamp(이수 도장) 표시. 버튼: 재훈련 / 미션 이동"),
        TEXT("훈련 종료 UI (알림창 UI 9). 모의 훈련 2분 초과(중단 아님) 또는 기초 훈련 Fail 기록. Stamp 미표시, 미션 이동 가능")};
    Row.Note=Notes[Id];
#endif
    return Row;
}
}

bool AOBTrainingDirector::ValidatePrototype(FString& Error) const
{
    const AOBRailPath* Rail = Spline->GetRailPath();
    if (PrototypeRoutePoints.Num() != 6 || PrototypeRoutePoints[0] != 0 || !Rail->bExternallyControlled ||
        !Spline->IsAtActionPoint() || Spline->GetCurrentPointIndex() != 0)
    { Error = TEXT("Prototype needs six stops and an externally controlled rail starting at point 0."); return false; }
    for (int32 I = 1; I < PrototypeRoutePoints.Num(); ++I)
        if (PrototypeRoutePoints[I] >= Rail->GetSpline()->GetNumberOfSplinePoints() ||
            PrototypeRoutePoints[I] <= PrototypeRoutePoints[I-1] ||
            Rail->GetNextStopPointIndex(PrototypeRoutePoints[I-1]) != PrototypeRoutePoints[I])
        { Error = TEXT("Register the prototype's consecutive rail stops: 0,2,4,7,12,17."); return false; }
    TSet<AOBTrainingTarget*> Unique;
    for (AOBTrainingTarget* Target : TimedTargets) if (IsValid(Target)) Unique.Add(Target);
    if (!IsValid(AccuracyTarget) || !IsValid(BreachDoor) || TimedTargets.Num() != 6 || Unique.Num() != 6 || AimPositions.IsEmpty())
    { Error = TEXT("Prototype needs a zeroing target, six distinct grouped targets, a door and aim positions."); return false; }
    return true;
}

bool AOBTrainingDirector::IsPrototypeGuideVisible() const
{
    if (!bUseSplinePrototype || bPrototypePaused || Result.bFinished || CurrentStep!=EOBTrainingStep::Welcome || CurrentDialogueId<=0) return false;
    const auto* Saved=FindPrototypeDialogue(CurrentDialogueId);
    const auto Row=Saved ? *Saved : DefaultPrototypeRow(CurrentDialogueId);
    return Row.bUIOpen && (Row.Event==EOBTrainingDialogueEvent::A_Guide || Row.Event==EOBTrainingDialogueEvent::B_Guide);
}
bool AOBTrainingDirector::IsGuideSectionActive() const
{
    if (!bUseSplinePrototype || Result.bFinished || bPrototypePaused || CurrentStep!=EOBTrainingStep::Welcome) return false;
    // PlayPrototypeLine clears CurrentDialogueId during the next page's delay. The presented
    // line remains the owner of that interval; widget visibility and fade opacity do not end it.
    const int32 SectionLine=CurrentDialogueId>0 ? CurrentDialogueId : PrototypePresentedId;
    return (SectionLine>=3 && SectionLine<=7) || SectionLine==17 || SectionLine==18;
}

int32 AOBTrainingDirector::GetGuideUIId() const
{
    if (!IsPrototypeGuideVisible()) return 0;
    if (CurrentDialogueId>=3 && CurrentDialogueId<=7) return 3001+CurrentDialogueId-3;
    if (CurrentDialogueId==17) return 3006;
    if (CurrentDialogueId==18) return 3007;
    return 0;
}

const FOBTrainingDialogueRow* AOBTrainingDirector::FindPrototypeDialogue(int32 Id) const
{
    auto* Table=DialogueTable.LoadSynchronous();
    return Table && Table->GetRowStruct()==FOBTrainingDialogueRow::StaticStruct() ?
        Table->FindRow<FOBTrainingDialogueRow>(FName(*FString::FromInt(Id)),TEXT("Training dialogue"),false) : nullptr;
}
FText AOBTrainingDirector::GetPrototypeDialogueText() const { return PrototypeLineText(CurrentDialogueId); }
int32 AOBTrainingDirector::GetPrototypeDisplayDialogueId() const
{
    if (bPrototypePaused) return 20;
    if (Result.bFinished) return Result.bAborted ? 20 : Result.IsScenarioPassed() ? 21 : 22;
    if (GetWorld() && GetWorld()->GetTimeSeconds()<PrototypeHintUntil) return PrototypeHintId;
    return CurrentStep==EOBTrainingStep::Welcome ? CurrentDialogueId : 0;
}
FText AOBTrainingDirector::GetSubtitleText() const
{
    if (!bUseSplinePrototype) return GetInstructionText(CurrentStep);
    const int32 Id=GetPrototypeDisplayDialogueId();
    if (Id<=0) return FText::GetEmpty();
    const auto* Saved=FindPrototypeDialogue(Id);
    const auto Row=Saved ? *Saved : DefaultPrototypeRow(Id);
    return Row.Character==EOBTrainingDialogueCharacter::System || Row.Layer==EOBTrainingDialogueLayer::Main ? FText::GetEmpty() : Row.Script;
}
FText AOBTrainingDirector::GetSystemMessageText() const
{
    if (!bUseSplinePrototype) return FText::GetEmpty();
    const int32 Id=GetPrototypeDisplayDialogueId();
    if (Id<=0) return FText::GetEmpty();
    const auto* Saved=FindPrototypeDialogue(Id);
    const auto Row=Saved ? *Saved : DefaultPrototypeRow(Id);
    return Row.Character==EOBTrainingDialogueCharacter::System || Row.Layer==EOBTrainingDialogueLayer::Main ? Row.Script : FText::GetEmpty();
}
FText AOBTrainingDirector::GetSubtitleSpeaker() const
{
    if (GetSubtitleText().IsEmpty()) return FText::GetEmpty();
    if (!bUseSplinePrototype) return FText::FromString(TEXT("교관"));
    const int32 Id=GetPrototypeDisplayDialogueId();
    const auto* Row=FindPrototypeDialogue(Id);
    const auto Character=Row ? Row->Character : DefaultPrototypeRow(Id).Character;
    return FText::FromString(Character==EOBTrainingDialogueCharacter::System ? TEXT("시스템") : TEXT("교관"));
}
FText AOBTrainingDirector::PrototypeLineText(int32 Id) const
{
    if (Id<=0) return FText::GetEmpty();
    if (const auto* Row=FindPrototypeDialogue(Id)) return Row->Script;
    return DefaultPrototypeRow(Id).Script;
}

void AOBTrainingDirector::StopPrototypeTargets()
{
    StopPopUpTargets();
    if (AccuracyTarget) AccuracyTarget->SetTargetEnabled(false, true);
    for (AOBTrainingTarget* Target : TimedTargets) if (Target) Target->SetTargetEnabled(false, true);
}

void AOBTrainingDirector::BeginPrototype()
{
    bInitialSkipOpen=true;
    Controller->SetTrainingControls(true);
    Controller->SetTrainingAmmo(0,20,3);
    if (AccuracyTarget) AccuracyTarget->ClearBulletHoles();
    for (AOBTrainingTarget* Target : TimedTargets) if (Target) Target->ClearBulletHoles();
    if (Overlay)
    {
        Overlay->SetTrainingDialogueAnimationControl(true);
        Overlay->OnSystemMessageShown.AddUniqueDynamic(this,&ThisClass::HandlePrototypeDialogueShown);
        Overlay->OnSubtitleShown.AddUniqueDynamic(this,&ThisClass::HandlePrototypeDialogueShown);
        Overlay->OnSystemMessageFinished.AddUniqueDynamic(this,&ThisClass::HandlePrototypeDialogueHidden);
        Overlay->OnSubtitleFinished.AddUniqueDynamic(this,&ThisClass::HandlePrototypeDialogueHidden);
    }
    Result.bPrototypeFlow = Result.bUsedTestInputs = true;
    Result.TotalTargets = Result.PassingTargetCount = 6;
    Result.RecordedFireLimitSeconds = PrototypeZoneSeconds;
    PrototypeRouteIndex = 0;
    bPrototypeWeaponViewReturning = false;
    Spline->ClearTrainingCameraFocus(true);
    PrototypeDeadline = 0;
    Spline->OnTrainingTurnCompleted.AddUObject(this, &ThisClass::HandlePrototypeTurnCompleted);
    CurrentDialogueId=0;
    bStepActive=bGuideAdvanceReady=false;
    bPrototypeIntroPending=true;
    IntroBrightness=0;
    PrototypeIntroStartedAt=GetWorld()->GetTimeSeconds();
    PrototypeSignArrivalYaw=Controller->GetPawn()->GetActorRotation().Yaw;
    if (Overlay) Overlay->SetVisibility(ESlateVisibility::Collapsed);
    if (Controller->PlayerCameraManager)
        Controller->PlayerCameraManager->SetManualCameraFade(1,FLinearColor::Black,false);
    // Initial placement is hidden by full black. Later route turns remain interpolated.
    if (!Spline->SnapTrainingToYaw(PrototypeSignArrivalYaw-90.f))
    { ConfigurationError(TEXT("Could not face the initial training sign.")); return; }
    bPrototypeTurning=false;
    if (Controller->PlayerCameraManager)
        Controller->PlayerCameraManager->StartCameraFade(1,0,GetPrototypeIntroDuration(),FLinearColor::Black,false,true);
    GetWorldTimerManager().SetTimer(PrototypeIntroTimer,this,&ThisClass::FinishPrototypeIntro,GetPrototypeIntroDuration(),false);
}

void AOBTrainingDirector::FinishPrototypeIntro()
{
    if (!bPrototypeIntroPending || Result.bFinished) return;
    IntroBrightness=1;
    if (Controller->PlayerCameraManager)
    {
        Controller->PlayerCameraManager->SetManualCameraFade(0,FLinearColor::Black,false);
        Controller->PlayerCameraManager->StopCameraFade();
    }
    StartPrototypeDialogueIfReady();
}
void AOBTrainingDirector::StartPrototypeDialogueIfReady()
{
    if (!bPrototypeIntroPending || IntroBrightness<1 || bPrototypeTurning || Result.bFinished) return;
    bPrototypeIntroPending=false;
    PrototypeAfterTurnDialogueId=0;
    UE_LOG(LogTemp,Display,TEXT("TRAINING_INTRO ready brightness=%.1f point=%d"),IntroBrightness,Spline->GetCurrentPointIndex());
    PlayPrototypeLine(2);
}
bool AOBTrainingDirector::IsInitialSkipAvailable() const
{
    return bUseSplinePrototype && bInitialized && bInitialSkipOpen && !bPrototypeIntroPending && !bPrototypePaused && !Result.bFinished;
}
bool AOBTrainingDirector::RequestInitialSkip()
{
    if (!IsInitialSkipAvailable() || SkipDestination.IsNull()) return false;
    bInitialSkipOpen=false;
    GetWorldTimerManager().ClearAllTimersForObject(this);
    StopPrototypeNarration(); StopPrototypeTargets();
    StopTrainingAudio(true);
    if (Overlay) Overlay->RefreshInitialTrainingSkip();
    UE_LOG(LogTemp,Display,TEXT("TRAINING_SKIP accepted; entering Stage_01 without a training qualification"));
    UGameplayStatics::OpenLevelBySoftObjectPtr(this,SkipDestination);
    return true;
}
void AOBTrainingDirector::StopPrototypeNarration()
{
    GetWorldTimerManager().ClearTimer(GuideReadyTimer);
    bGuideAdvanceReady=false;
    if (Overlay) Overlay->RefreshTrainingGuide();
    StopPrototypeHint();
    PrototypeMessage=EPrototypeMessage::None;
    bPrototypeMessageFadingOut=bPrototypeMessageShown=false;
    if (Overlay)
    {
        if (auto* Guide=Overlay->GetTrainingGuideWidget()) Guide->CancelGuideTransitions();
        Overlay->CancelTrainingDialogue();
    }
    GetWorldTimerManager().ClearTimer(PrototypeNarrationTimer);
    bPrototypeNarrationActive=false;
    if (IsValid(PrototypeNarrationAudio))
    {
        PrototypeNarrationAudio->OnAudioFinished.RemoveDynamic(this,&ThisClass::FinishPrototypeNarration);
        PrototypeNarrationAudio->Stop();
    }
    PrototypeNarrationAudio=nullptr;
}
void AOBTrainingDirector::StopPrototypeHint()
{
    GetWorldTimerManager().ClearTimer(PrototypeHintTimer);
    if (PrototypeMessage==EPrototypeMessage::Hint)
    {
        PrototypeMessage=EPrototypeMessage::None;
        bPrototypeMessageFadingOut=bPrototypeMessageShown=false;
        if (Overlay) Overlay->CancelTrainingDialogue();
    }
    if (IsValid(PrototypeHintAudio))
    {
        PrototypeHintAudio->OnAudioFinished.RemoveDynamic(this,&ThisClass::FinishPrototypeHint);
        PrototypeHintAudio->Stop();
    }
    PrototypeHintAudio=nullptr;
    PrototypeHintId=0;
    PrototypeHintUntil=0;
}
void AOBTrainingDirector::PlayPrototypeHint(int32 Id)
{
    if (!bUseSplinePrototype || bPrototypePaused || Result.bFinished || !bStepActive) return;
    const double Now=GetWorld()->GetTimeSeconds();
    // Repeated failures must not restart the same reminder while it is visible/playing.
    if (PrototypeHintId==Id && Now<PrototypeHintUntil) return;
    StopPrototypeHint();
    PrototypeHintId=Id;
    // Remains active through both fades; a repeated failure cannot replace its animation.
    PrototypeHintUntil=TNumericLimits<double>::Max();
    PresentPrototypeDialogue(Id,true);
    OnRetry.Broadcast(PrototypeLineText(Id));
}
void AOBTrainingDirector::PlayPrototypeLine(int32 Id)
{
    StopPrototypeNarration();
    const auto* Row=FindPrototypeDialogue(Id);
    if (Row && Row->Delay_Time>0)
    {
        CurrentDialogueId=0;
        CurrentStep=EOBTrainingStep::Welcome;
        bGuideAdvanceReady=bStepActive=bTransitionPending=false;
        Controller->SetTrainingControls(true);
        Action->SetExternalActionState(true);
        OnStepChanged.Broadcast(CurrentStep);
        GetWorldTimerManager().SetTimer(PrototypeNarrationTimer,FTimerDelegate::CreateUObject(this,&ThisClass::BeginPrototypeNarration,Id),Row->Delay_Time,false);
        return;
    }
    BeginPrototypeNarration(Id);
}
void AOBTrainingDirector::BeginPrototypeNarration(int32 Id)
{
    CurrentDialogueId = Id;
    const APawn* Pawn=Controller ? Controller->GetPawn().Get() : nullptr;
    UE_LOG(LogTemp,Display,TEXT("TRAINING_DIALOGUE id=%d point=%d yaw=%.1f"),Id,Spline ? Spline->GetCurrentPointIndex() : INDEX_NONE,Pawn ? Pawn->GetActorRotation().Yaw : 0.f);
    CurrentStep = EOBTrainingStep::Welcome;
    bGuideAdvanceReady = bStepActive = bTransitionPending = false;
    Controller->SetTrainingControls(true);
    Action->SetExternalActionState(true);
    if (Overlay) Overlay->SetVisibility(ESlateVisibility::HitTestInvisible);
    OnStepChanged.Broadcast(CurrentStep);
    PresentPrototypeDialogue(Id);
}

void AOBTrainingDirector::PresentPrototypeDialogue(int32 Id,bool bHint,bool bTerminal)
{
    if (Overlay) Overlay->RefreshTrainingGuide();
    PrototypePresentedId=Id;
    PrototypeMessage=bHint ? EPrototypeMessage::Hint : bTerminal ? EPrototypeMessage::Terminal : EPrototypeMessage::Narration;
    bPrototypeMessageFadingOut=bPrototypeMessageShown=false;
    const auto* Row=FindPrototypeDialogue(Id);
    bPrototypeMessageUsesSystem=Row && (Row->Character==EOBTrainingDialogueCharacter::System || Row->Layer==EOBTrainingDialogueLayer::Main);
    if (Overlay)
    {
        if (bTerminal && Id==20) { HandlePrototypeDialogueShown(); return; }
        if (bPrototypeMessageUsesSystem && Overlay->IsSystemMessageReady())
        { Overlay->ShowPersistentSystemMessage(PrototypeLineText(Id)); return; }
        if (!bPrototypeMessageUsesSystem && Overlay->IsSubtitleReady())
        { Overlay->ShowPersistentSubtitle(FText::FromString(TEXT("교관")),PrototypeLineText(Id)); return; }
    }
    HandlePrototypeDialogueShown();
}

void AOBTrainingDirector::HandlePrototypeDialogueShown()
{
    if (PrototypeMessage==EPrototypeMessage::None || bPrototypeMessageShown || bPrototypeMessageFadingOut) return;
    bPrototypeMessageShown=true;
    if (IsPrototypeGuideVisible() && Overlay)
        if (auto* Guide=Overlay->GetTrainingGuideWidget()) Guide->PlayGuideFadeIn();
    const bool bHint=PrototypeMessage==EPrototypeMessage::Hint;
    bPrototypeNarrationActive=!bHint;
    UE_LOG(LogTemp,Display,TEXT("TRAINING_MESSAGE shown id=%d voice starts after fade-in"),PrototypePresentedId);
    const auto* Row=FindPrototypeDialogue(PrototypePresentedId);
    auto& Audio=bHint ? PrototypeHintAudio : PrototypeNarrationAudio;
    if (auto* Voice=Row ? Row->Voice.LoadSynchronous() : nullptr)
    {
        Audio=UGameplayStatics::CreateSound2D(this,Voice);
        if (Audio)
        {
            if (bHint) Audio->OnAudioFinished.AddDynamic(this,&ThisClass::FinishPrototypeHint);
            else Audio->OnAudioFinished.AddDynamic(this,&ThisClass::FinishPrototypeNarration);
            Audio->Play();
            return;
        }
    }
    const float Duration=FMath::Max(0.01f, Row && Row->Display_Time>0 ? Row->Display_Time : PrototypeNarrationSeconds);
    if (bHint) GetWorldTimerManager().SetTimer(PrototypeHintTimer,this,&ThisClass::FinishPrototypeHint,Duration,false);
    else GetWorldTimerManager().SetTimer(PrototypeNarrationTimer,this,&ThisClass::FinishPrototypeNarration,Duration,false);
}

void AOBTrainingDirector::FinishPrototypeNarration()
{
    if (!bUseSplinePrototype || !bPrototypeNarrationActive || PrototypeMessage==EPrototypeMessage::None || PrototypeMessage==EPrototypeMessage::Hint) return;
    GetWorldTimerManager().ClearTimer(PrototypeNarrationTimer);
    bPrototypeNarrationActive=false;
    if (PrototypeNarrationAudio)
    {
        PrototypeNarrationAudio->OnAudioFinished.RemoveDynamic(this,&ThisClass::FinishPrototypeNarration);
        PrototypeNarrationAudio->Stop();
        PrototypeNarrationAudio=nullptr;
    }
    if (IsPrototypeGuideVisible())
    {
        const int32 Page=CurrentDialogueId;
        const float Delay=FMath::Max(0.f,GuideTriggerDelaySeconds);
        auto Ready=[this,Page]() {
            if (CurrentDialogueId!=Page || !IsPrototypeGuideVisible() || bPrototypeNarrationActive) return;
            if (Page==3) bInitialSkipOpen=false;
            bGuideAdvanceReady=true;
            if (Overlay) { Overlay->RefreshTrainingGuide(); Overlay->RefreshInitialTrainingSkip(); }
        };
        if (Delay<=0) Ready();
        else GetWorldTimerManager().SetTimer(GuideReadyTimer,FTimerDelegate::CreateWeakLambda(this,Ready),Delay,false);
        return;
    }
    DismissPrototypeDialogue();
}

void AOBTrainingDirector::FinishPrototypeHint()
{
    if (PrototypeMessage!=EPrototypeMessage::Hint || !bPrototypeMessageShown) return;
    GetWorldTimerManager().ClearTimer(PrototypeHintTimer);
    DismissPrototypeDialogue();
}
void AOBTrainingDirector::DismissPrototypeDialogue()
{
    GetWorldTimerManager().ClearTimer(GuideReadyTimer);
    if (PrototypeMessage==EPrototypeMessage::None || bPrototypeMessageFadingOut) return;
    bGuideAdvanceReady=false;
    if (Overlay) Overlay->RefreshTrainingGuide();
    bPrototypeMessageFadingOut=true;
    auto* Guide=IsPrototypeGuideVisible() && Overlay ? Overlay->GetTrainingGuideWidget() : nullptr;
    bPrototypeDialogueLayerHidden=false; bPrototypeGuideHidden=!Guide;
    if (Guide) Guide->OnGuideHidden.AddUniqueDynamic(this,&ThisClass::HandlePrototypeGuideHidden);
    if (Overlay)
    {
        if (bPrototypeMessageUsesSystem && Overlay->IsSystemMessageReady()) Overlay->DismissSystemMessage();
        else if (!bPrototypeMessageUsesSystem && Overlay->IsSubtitleReady()) Overlay->DismissSubtitle();
        else HandlePrototypeDialogueHidden();
    }
    else HandlePrototypeDialogueHidden();
    if (Guide && bPrototypeMessageFadingOut) Guide->DismissGuide();
}
void AOBTrainingDirector::HandlePrototypeDialogueHidden()
{
    bPrototypeDialogueLayerHidden=true;
    TryFinishPrototypeDialogueFade();
}
void AOBTrainingDirector::HandlePrototypeGuideHidden()
{
    if (!bPrototypeMessageFadingOut) return;
    bPrototypeGuideHidden=true;
    // Completion of the guide's hide animation defines the page-change moment. A longer subtitle animation
    // is cancelled here so it cannot delay or overlap the next guide/narration.
    if (!bPrototypeDialogueLayerHidden && Overlay) Overlay->CancelTrainingDialogue();
    bPrototypeDialogueLayerHidden=true;
    TryFinishPrototypeDialogueFade();
}
void AOBTrainingDirector::TryFinishPrototypeDialogueFade()
{
    if (!bPrototypeMessageFadingOut || PrototypeMessage==EPrototypeMessage::None) return;
    if (!bPrototypeDialogueLayerHidden || !bPrototypeGuideHidden) return;
    const auto FinishedMessage=PrototypeMessage;
    PrototypeMessage=EPrototypeMessage::None;
    bPrototypeMessageFadingOut=bPrototypeMessageShown=false;
    UE_LOG(LogTemp,Display,TEXT("TRAINING_MESSAGE hidden id=%d continue after fade-out"),PrototypePresentedId);
    if (FinishedMessage==EPrototypeMessage::Hint) { StopPrototypeHint(); return; }
    if (FinishedMessage==EPrototypeMessage::Terminal) return;
    AdvancePrototypeDialogue();
}
void AOBTrainingDirector::AdvancePrototypeDialogue()
{
    switch (CurrentDialogueId)
    {
    case 1: PlayPrototypeLine(2); break;
    case 2: PlayPrototypeLine(3); break;
    case 8: MovePrototypeTo(1); break;
    case 14: TurnPrototypeAtBasicStop(true); break;
    case 15: MovePrototypeTo(2); break;
    case 16: PlayPrototypeLine(17); break;
    case 19: MovePrototypeTo(3); break;
    case 3: case 4: case 5: case 6: case 17: PlayPrototypeLine(CurrentDialogueId+1); break;
    case 7: case 18: TurnPrototypeForDialogue(PrototypeSignArrivalYaw,CurrentDialogueId==7 ? 8 : 19); break;
    default: break;
    }
}

void AOBTrainingDirector::MovePrototypeTo(int32 RouteIndex, int32 DialogueId)
{
    if (Result.bFinished || !PrototypeRoutePoints.IsValidIndex(RouteIndex)) return;
    if (Spline->GetRailPath()->GetNextStopPointIndex(Spline->GetCurrentPointIndex()) != PrototypeRoutePoints[RouteIndex])
    { ConfigurationError(TEXT("Prototype next rail stop does not match its route.")); return; }
    StopPrototypeTargets();
    StopPrototypeHint();
    GetWorldTimerManager().ClearTimer(PrototypeNarrationTimer);
    PrototypeRouteIndex = RouteIndex;
    if (DialogueId > 0) PlayPrototypeLine(DialogueId);
    else CurrentDialogueId = 0;
    CurrentStep = EOBTrainingStep::Moving;
    bGuideAdvanceReady = bStepActive = false;
    Controller->SetTrainingControls(true);
    Action->SetExternalActionState(true);
    OnStepChanged.Broadcast(CurrentStep);
    Action->CompleteAction();
}

void AOBTrainingDirector::ArrivePrototype(int32 RouteIndex)
{
    GetWorldTimerManager().ClearTimer(PrototypeNarrationTimer);
    CurrentDialogueId = 0;
    switch (RouteIndex)
    {
    case 1:
        ZoneIndex = 0;
        PrototypeBasicArrivalYaw = Controller->GetPawn()->GetActorRotation().Yaw;
        TurnPrototypeAtBasicStop(false); break;
    case 2:
        ZoneIndex = 1;
        PrototypeSignArrivalYaw=Controller->GetPawn()->GetActorRotation().Yaw;
        TurnPrototypeForDialogue(PrototypeSignArrivalYaw-90.f,16); break;
    case 3:
        HitTargets.Reset();
        Result.ShotsFired = Result.ValidHits = Result.UniqueTargetsHit = 0;
        StepResult(EOBTrainingStep::TimedTargets).ElapsedSeconds = 0;
        PrototypeDeadline = 0; // The two-minute clock begins at the start signal after the turn.
        TurnPrototypeAtShootingStop(90.f); break;
    case 4: TurnPrototypeAtShootingStop(90.f); break;
    case 5: BeginPrototypeExercise(EOBTrainingStep::Breach); break;
    default: ConfigurationError(TEXT("Unknown prototype route stop.")); break;
    }
}

void AOBTrainingDirector::TurnPrototypeAtBasicStop(bool bReturnToRoute)
{
    bPrototypeTurning = true;
    bPrototypeReturnTurn = bReturnToRoute;
    PrototypeAfterTurnDialogueId=bReturnToRoute ? 15 : 0;
    bStepActive = bGuideAdvanceReady = false;
    CurrentStep = EOBTrainingStep::Moving;
    Controller->SetTrainingControls(true);
    Controller->SetCrouchMode(false);
    if (auto* Player = Cast<AOBPlayerCharacter>(Controller->GetPawn())) Player->UnCrouch();
    Action->SetExternalActionState(false);
    OnStepChanged.Broadcast(CurrentStep);
    if (!Spline->TurnTrainingToYaw(PrototypeBasicArrivalYaw + (bReturnToRoute ? 0.f : -90.f)))
    {
        bPrototypeTurning = false;
        ConfigurationError(TEXT("Could not turn at the basic training stop."));
    }
}

void AOBTrainingDirector::HandlePrototypeTurnCompleted()
{
    if (!bPrototypeTurning || Result.bFinished) return;
    bPrototypeTurning = false;
    if (bPrototypeIntroPending) { StartPrototypeDialogueIfReady(); return; }
    if (PrototypeAfterTurnDialogueId>0)
    {
        const int32 Id=PrototypeAfterTurnDialogueId; PrototypeAfterTurnDialogueId=0;
        PlayPrototypeLine(Id); return;
    }
    if (PrototypeRouteIndex == 3) BeginPrototypeMockCountdown();
    else if (PrototypeRouteIndex == 4) BeginPrototypeExercise(EOBTrainingStep::TimedTargets);
    else if (bPrototypeReturnTurn) PlayPrototypeLine(15);
    else
    {
        PrototypeDeadline = GetWorld()->GetTimeSeconds() + PrototypeZoneSeconds;
        BeginPrototypeExercise(EOBTrainingStep::WeaponCheck);
    }
}

void AOBTrainingDirector::BeginPrototypeMockCountdown()
{
    PrototypeDeadline=0;
    bPrototypeMockStartPending=true;
    bStepActive=bGuideAdvanceReady=false;
    CurrentStep=EOBTrainingStep::Countdown;
    Controller->SetTrainingControls(true);
    Action->SetExternalActionState(false);
    StopPrototypeTargets();
    OnStepChanged.Broadcast(CurrentStep);
    GetWorldTimerManager().SetTimer(PrototypeMockStartTimer,this,&ThisClass::StartPrototypeMockTraining,
        FMath::Max(.01f,MockStartDelaySeconds),false);
}

void AOBTrainingDirector::StartPrototypeMockTraining()
{
    if (!bPrototypeMockStartPending || Result.bFinished || bPrototypePaused || PrototypeRouteIndex!=3 || CurrentStep!=EOBTrainingStep::Countdown) return;
    bPrototypeMockStartPending=false;
    PlayTrainingSignal(MockStartSiren,TEXT("MockStart"));
    PrototypeDeadline=GetWorld()->GetTimeSeconds()+PrototypeZoneSeconds;
    UE_LOG(LogTemp,Display,TEXT("TRAINING_MOCK start_signal siren=%s deadline=%.3f"),*MockStartSiren.GetAssetName(),PrototypeDeadline);
    BeginPrototypeExercise(EOBTrainingStep::TimedTargets);
}

void AOBTrainingDirector::TurnPrototypeAtShootingStop(float YawOffset)
{
    bPrototypeTurning = true;
    bPrototypeReturnTurn = false;
    PrototypeAfterTurnDialogueId=0;
    bStepActive = bGuideAdvanceReady = false;
    CurrentStep = EOBTrainingStep::Moving;
    Controller->SetTrainingControls(true);
    Controller->SetCrouchMode(false);
    Action->SetExternalActionState(false);
    OnStepChanged.Broadcast(CurrentStep);
    if (!Spline->TurnTrainingToYaw(Controller->GetPawn()->GetActorRotation().Yaw + YawOffset))
    {
        bPrototypeTurning = false;
        ConfigurationError(TEXT("Could not turn at the shooting stop."));
    }
}

void AOBTrainingDirector::TurnPrototypeForDialogue(float WorldYaw, int32 NextDialogueId)
{
    StopPrototypeNarration();
    bPrototypeTurning=true;
    bPrototypeReturnTurn=false;
    PrototypeAfterTurnDialogueId=NextDialogueId;
    CurrentDialogueId=0;
    bStepActive=bGuideAdvanceReady=false;
    CurrentStep=bPrototypeIntroPending ? EOBTrainingStep::Intro : EOBTrainingStep::Moving;
    Controller->SetTrainingControls(true);
    Controller->SetCrouchMode(false);
    Action->SetExternalActionState(false);
    OnStepChanged.Broadcast(CurrentStep);
    if (!Spline->TurnTrainingToYaw(WorldYaw))
    {
        bPrototypeTurning=false;
        ConfigurationError(TEXT("Could not turn toward the training sign or route."));
    }
}

bool AOBTrainingDirector::IsPrototypePopUpCandidate(AOBTrainingTarget* Target) const
{
    return IsValid(Target) && IsPrototypeGroupTarget(Target) && !Target->IsA<AOBRailTrainingTarget>() && !HitTargets.Contains(Target);
}

bool AOBTrainingDirector::IsPrototypeGroupTarget(AOBTrainingTarget* Target) const
{
    const int32 Start = PrototypeRouteIndex == 3 ? 0 : PrototypeRouteIndex == 4 ? 3 : INDEX_NONE;
    return Start != INDEX_NONE && TimedTargets.IsValidIndex(Start + 2) &&
        (Target == TimedTargets[Start] || Target == TimedTargets[Start+1] || Target == TimedTargets[Start+2]);
}

int32 AOBTrainingDirector::GetPrototypeGroupHits() const
{
    int32 Count = 0;
    for (AOBTrainingTarget* Target : TimedTargets) if (IsPrototypeGroupTarget(Target) && HitTargets.Contains(Target)) ++Count;
    return Count;
}

void AOBTrainingDirector::BeginPrototypeExercise(EOBTrainingStep Step)
{
    bPrototypeWeaponViewReturning = false;
    if (Spline)
    {
        if (Step == EOBTrainingStep::WeaponCheck && PrototypeRouteIndex == 1 && bFocusWeaponCheckCamera)
            Spline->FocusTrainingCamera(WeaponCheckCameraPitch, WeaponCheckCameraFOV, WeaponCheckCameraBlendSeconds, WeaponCheckCameraYaw);
        else Spline->ClearTrainingCameraFocus();
    }
    // Basic timeout measures lack of progress in one exercise, not the whole A zone.
    if (PrototypeRouteIndex==1 && CurrentStep!=Step)
        PrototypeDeadline=GetWorld()->GetTimeSeconds()+PrototypeZoneSeconds;
    // Keep a reminder playing during an immediate retry of the same exercise.
    if (CurrentStep!=Step) StopPrototypeHint();
    CurrentStep = Step;
    CurrentDialogueId = 0;
    bGuideAdvanceReady = bTransitionPending = bBreachAccepted = false;
    bStepActive = true;
    Deadline = 0; // Module-wide deadline remains independent of per-exercise reminders.
    StepStartedAt = GetWorld()->GetTimeSeconds();
    PrototypeReminderAt = StepStartedAt + PrototypeReminderSeconds;
    ++Attempt;
    ++StepResult(Step).Attempts;
    Action->SetExternalActionState(true);
    const bool bShooting = Step == EOBTrainingStep::Accuracy || Step == EOBTrainingStep::TimedTargets;
    // Cover/reload validates posture; zeroing accepts either stable firing posture.
    Controller->SetTrainingControls(true, bShooting, Step == EOBTrainingStep::Reload || Step == EOBTrainingStep::TimedTargets, Step!=EOBTrainingStep::Accuracy);
    Controller->SetCrouchMode(Step == EOBTrainingStep::Cover || Step == EOBTrainingStep::Reload || Step == EOBTrainingStep::Accuracy);
    if (Step == EOBTrainingStep::AimHold) { AimIndex = 0; HoldSeconds = 0; }
    if (Step == EOBTrainingStep::Reload) Controller->SetTrainingAmmo(0, 20, 3);
    if (Step == EOBTrainingStep::Accuracy)
    {
        ResetAccuracyMeasurement();
        Controller->SetTrainingAmmo(3, 20, 3);
        AccuracyTarget->SetTargetEnabled(true, true);
    }
    if (Step == EOBTrainingStep::TimedTargets)
    {
        Controller->SetTrainingAmmo(PrototypeRouteIndex==3 ? 20 : Controller->GetCurrentAmmo(), 20);
        StartPopUpTargets();
        for (AOBTrainingTarget* Target : TimedTargets)
            if (auto* RailTarget = Cast<AOBRailTrainingTarget>(Target))
            {
                const bool bEnabled = IsPrototypeGroupTarget(Target) && !HitTargets.Contains(Target);
                RailTarget->SetTargetEnabled(bEnabled, true);
                if (bEnabled) RailTarget->StartMoving();
                else RailTarget->StopMoving();
            }
        OnProgressChanged.Broadcast(GetPrototypeGroupHits(), 3);
    }
    if (Step == EOBTrainingStep::Breach)
    {
        BreachDoor->ResetDoor();
        bLastBreachState = Motion ? Motion->IsBreachRecognized() : false;
    }
    OnStepChanged.Broadcast(Step);
    if (PoseInput) PoseInput->RefreshPoseState();
}

void AOBTrainingDirector::CompletePrototypeExercise(EOBTrainingOutcome Outcome)
{
    auto& Record = StepResult(CurrentStep);
    Record.Outcome = Outcome;
    Record.ElapsedSeconds += FMath::Max(0.0, GetWorld()->GetTimeSeconds() - StepStartedAt);
    bStepActive = false;
    switch (CurrentStep)
    {
    case EOBTrainingStep::WeaponCheck:
        if (Spline && Spline->IsTrainingCameraFocusActive())
        {
            bPrototypeWeaponViewReturning = true;
            Spline->ClearTrainingCameraFocus();
            Controller->SetTrainingControls(true);
            Action->SetExternalActionState(false);
        }
        else BeginPrototypeExercise(EOBTrainingStep::AimHold);
        break;
    case EOBTrainingStep::AimHold: BeginPrototypeExercise(EOBTrainingStep::Cover); break;
    case EOBTrainingStep::Cover: BeginPrototypeExercise(EOBTrainingStep::Reload); break;
    case EOBTrainingStep::Reload: BeginPrototypeExercise(EOBTrainingStep::Accuracy); break;
    case EOBTrainingStep::Accuracy:
        PrototypeDeadline = 0;
        StopPrototypeTargets();
        PlayPrototypeLine(14); break;
    case EOBTrainingStep::TimedTargets: MovePrototypeTo(PrototypeRouteIndex == 3 ? 4 : 5); break;
    case EOBTrainingStep::Breach:
        Result.RecordedFireSeconds = FMath::Max(0.0, PrototypeZoneSeconds - (PrototypeDeadline - GetWorld()->GetTimeSeconds()));
        Result.bScenarioCompleted = Outcome == EOBTrainingOutcome::Passed;
        CurrentStep = EOBTrainingStep::Complete;
        FinishTraining(false); break;
    default: break;
    }
}

void AOBTrainingDirector::TickPrototype()
{
    const double Now = GetWorld()->GetTimeSeconds();
    if (Result.bFinished || bPrototypePaused) return;
    if (bPrototypeWeaponViewReturning)
    {
        if (!Spline || !Spline->IsTrainingCameraFocusActive()) BeginPrototypeExercise(EOBTrainingStep::AimHold);
        return;
    }
    if (bPrototypeIntroPending)
    {
        IntroBrightness=FMath::Clamp(static_cast<float>((Now-PrototypeIntroStartedAt)/GetPrototypeIntroDuration()),0.f,1.f);
        return;
    }
    if (PrototypeDeadline > 0 && Now >= PrototypeDeadline)
    {
        StopPrototypeHint();
        if (PrototypeRouteIndex == 1)
        {
            bPrototypePaused = true;
            StopTrainingFootsteps();
            bStepActive = false;
            HoldSeconds = 0;
            StopPrototypeTargets();
            Controller->SetTrainingControls(true);
            Action->SetExternalActionState(false);
            OnStepChanged.Broadcast(CurrentStep);
            PresentPrototypeDialogue(20,false,true);
        }
        else
        {
            Spline->StopTrainingMovement();
            if (bStepActive) StepResult(CurrentStep).Outcome = EOBTrainingOutcome::Failed;
            Result.RecordedFireSeconds = PrototypeZoneSeconds;
            CurrentStep = EOBTrainingStep::Complete;
            FinishTraining(false, TEXT("모의 훈련의 2분 제한을 초과했습니다."));
        }
        return;
    }
    if (PrototypeRouteIndex == 1 && bStepActive && Now >= PrototypeReminderAt)
    {
        const int32 HintId = CurrentStep == EOBTrainingStep::WeaponCheck ? 9 : CurrentStep == EOBTrainingStep::AimHold ? 10 :
            CurrentStep == EOBTrainingStep::Cover ? 11 : CurrentStep == EOBTrainingStep::Reload ? 12 : 13;
        PrototypeReminderAt = Now + PrototypeReminderSeconds;
        PlayPrototypeHint(HintId);
    }
}

void AOBTrainingDirector::ResumePrototype()
{
    if (!bUseSplinePrototype || !bPrototypePaused || Result.bFinished) return;
    // Prototype policy: retain completed exercises and give the current exercise a new 2-minute window.
    bPrototypePaused = false;
    StopPrototypeNarration();
    bStepActive = true;
    PrototypeDeadline = GetWorld()->GetTimeSeconds() + PrototypeZoneSeconds;
    PrototypeReminderAt = GetWorld()->GetTimeSeconds() + PrototypeReminderSeconds;
    Action->SetExternalActionState(true);
    const bool bShooting = CurrentStep == EOBTrainingStep::Accuracy;
    Controller->SetTrainingControls(true, bShooting, CurrentStep == EOBTrainingStep::Reload, CurrentStep!=EOBTrainingStep::Accuracy);
    if (bShooting) AccuracyTarget->SetTargetEnabled(true, true);
    if (PoseInput) PoseInput->RefreshPoseState();
    OnStepChanged.Broadcast(CurrentStep);
}

#if WITH_EDITOR
namespace
{
void SaveFinalTrainingDialogue()
{
    const FString PackageName=TEXT("/Game/Blueprints/Training/Data/DT_TrainingDialogue");
    auto* Table=LoadObject<UDataTable>(nullptr,*(PackageName+TEXT(".DT_TrainingDialogue")));
    const bool bNew=!Table;
    if (!Table)
    {
        Table=NewObject<UDataTable>(CreatePackage(*PackageName),TEXT("DT_TrainingDialogue"),RF_Public|RF_Standalone);
        Table->RowStruct=FOBTrainingDialogueRow::StaticStruct();
        FAssetRegistryModule::AssetCreated(Table);
    }
    bool bSuccess=Table->RowStruct==FOBTrainingDialogueRow::StaticStruct();
    TMap<FName,FOBTrainingDialogueRow> UpdatedRows;
    for (int32 Id=1;Id<=22 && bSuccess;++Id)
    {
        auto Row=DefaultPrototypeRow(Id);
        auto* Voice=Row.Voice.LoadSynchronous();
        bSuccess=Voice!=nullptr;
        UE_LOG(LogTemp,Display,TEXT("TRAINING_DIALOGUE row=%d voice=%s duration=%.2f status=%s"),Id,
            *Row.Voice.ToSoftObjectPath().ToString(),Voice ? Voice->GetDuration() : 0.f,bSuccess ? TEXT("PASS") : TEXT("FAIL"));
        UpdatedRows.Add(FName(*FString::FromInt(Id)),MoveTemp(Row));
    }
    if (bSuccess)
    {
        const FString Filename=FPackageName::LongPackageNameToFilename(PackageName,FPackageName::GetAssetPackageExtension());
        auto& Files=FPlatformFileManager::Get().GetPlatformFile();
        if (!bNew)
        {
            const FString Backup=FPaths::ProjectSavedDir()/TEXT("Backups/TrainingDialogue")/FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))/TEXT("DT_TrainingDialogue.uasset");
            Files.CreateDirectoryTree(*FPaths::GetPath(Backup));
            if (!Files.FileExists(*Backup)) bSuccess=Files.CopyFile(*Backup,*Filename);
        }
        if (bSuccess)
        {
            Table->Modify();
            Table->EmptyTable();
            for (const auto& Pair : UpdatedRows) Table->AddRow(Pair.Key,Pair.Value);
            Files.CreateDirectoryTree(*FPaths::GetPath(Filename));
            FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone; Args.SaveFlags=SAVE_NoError;
            bSuccess=UPackage::SavePackage(Table->GetPackage(),Table,*Filename,Args);
        }
    }
    UE_LOG(LogTemp,Display,TEXT("TRAINING_DIALOGUE setup=%s rows=%d"),bSuccess ? TEXT("PASS") : TEXT("FAIL"),Table->GetRowNames().Num());
    if (FParse::Param(FCommandLine::Get(),TEXT("TrainingDialogueSetup"))) FPlatformMisc::RequestExitWithStatus(false,bSuccess ? 0 : 1);
}
}
static FAutoConsoleCommand GOBSaveFinalTrainingDialogue(TEXT("OB.Training.UpdateDialogue"),TEXT("Update training dialogue rows 1-22 and validate all TrainingScriptSound voice references before saving."),FConsoleCommandDelegate::CreateStatic(&SaveFinalTrainingDialogue));
#endif

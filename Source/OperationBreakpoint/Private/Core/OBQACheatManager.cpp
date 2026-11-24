#include "Core/OBQACheatManager.h"
#include "Core/OBPlayerController.h"
#include "Training/OBTrainingDirector.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Widget/OBHUD.h"
#include "Widget/OBPlayerOverlayWidget.h"
#include "Components/AudioComponent.h"
#include "UObject/UObjectIterator.h"
#include "String/LexFromString.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Stage/OBStageFlowComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Console.h"
#include "InputCoreTypes.h"

DEFINE_LOG_CATEGORY_STATIC(LogOBQA, Log, All);

namespace
{
FString DescribeQACheckpoint(FName Name)
{
    static const TMap<FName,FString> Descriptions={
        {TEXT("Intro"),TEXT("인트로")},{TEXT("WeaponCheck"),TEXT("총기 확인")},
        {TEXT("AimHold"),TEXT("조준 유지")},{TEXT("Cover"),TEXT("엄폐")},
        {TEXT("Reload"),TEXT("재장전")},{TEXT("Calibration"),TEXT("영점 조절 (구형 코스)")},
        {TEXT("Accuracy"),TEXT("영점 조절 / 명중 검사")},{TEXT("Countdown"),TEXT("사격 시작 대기")},
        {TEXT("Shooting1"),TEXT("첫 번째 사격 구간")},{TEXT("Shooting2"),TEXT("두 번째 사격 구간")},
        {TEXT("Breach"),TEXT("돌파")},{TEXT("Result"),TEXT("결과 화면 미리보기")}
    };
    const FString* Description=Descriptions.Find(Name);
    return Description ? *Description : Name.ToString();
}

FString BuildQACheckpointList(UWorld* World)
{
    TArray<FName> Names={TEXT("Intro"),TEXT("WeaponCheck"),TEXT("AimHold"),TEXT("Cover"),TEXT("Reload"),
        TEXT("Accuracy"),TEXT("Countdown"),TEXT("Shooting1"),TEXT("Shooting2"),TEXT("Breach"),TEXT("Result")};
    if (UOBQACheatManager::IsTrainingMap(World))
        for (TActorIterator<AOBTrainingDirector> It(World); It; ++It) { Names=It->GetQACheckpoints(); break; }
    FString List=TEXT("[TutorialMap 전용 단계: QA Goto <단계>]");
    for (FName Name : Names)
        List+=FString::Printf(TEXT("\n  QA Goto %s  - %s"),*Name.ToString(),*DescribeQACheckpoint(Name));
    if (!Names.Contains(TEXT("Calibration")))
        List+=TEXT("\n  영점 조절은 Accuracy에 포함됩니다. Calibration은 별도 단계가 아닙니다.");
    return List;
}
FString BuildQAUIList()
{
    return TEXT("[TutorialMap UI 미리보기: QA UI <ID>]\n")
        TEXT("  QA UI 3001 - 훈련장 안내\n  QA UI 3002 - 격발 확인\n  QA UI 3003 - 조준 훈련\n")
        TEXT("  QA UI 3004 - 엄폐 / 재장전 훈련\n  QA UI 3005 - 영점 조절 사격\n")
        TEXT("  QA UI 3006 - 모의 훈련 안내 1\n  QA UI 3007 - 모의 훈련 안내 2\n")
        TEXT("  QA UI 3008 - 훈련 중단\n  QA UI 3009 - 훈련 통과\n  QA UI 3010 - 훈련 실패\n")
        TEXT("  QA UI Close - 미리보기 종료 후 원래 화면과 진행으로 복귀\n")
        TEXT("  미리보기 중 진행은 일시정지되며, 가이드의 TriggerImage는 즉시 표시됩니다.");
}
FString BuildQACameraHelp()
{
    return TEXT("[Stage_01 카메라 조정]\n  QA Zone <1~6> - 해당 Zone으로 이동하고 적을 새로 스폰 (조정은 아직 꺼짐)\n")
        TEXT("  QA Camera Start - 조정 활성화\n  1/2: FOV +/-, 3/4: Pitch +/-, 5/6: Yaw +/-, 7/8: Roll +/- (각 1도)\n")
        TEXT("  QA Camera - 현재 값 출력 / QA Camera End - 최종 값 출력 후 해당 구역 적 Destroy 및 종료\n")
        TEXT("  출력값을 Stop Point Settings에 수동 반영하세요. 맵에는 자동 저장하지 않습니다.");
}
FString BuildQABreachHelp()
{
    return TEXT("[Stage_01 브리칭 테스트]\n")
        TEXT("  QA Breach / QA Breach 85 - 문을 복구하고 85번 브리칭 대기부터 시작\n")
        TEXT("  QA Breach 83 - 문을 복구하고 83번 인질/인질범 대사부터 시작\n")
        TEXT("  QA Breach Trigger - 85번 대기 중 브리칭 입력을 테스트로 전달 (카메라 불필요)\n")
        TEXT("  맵을 새로 로드하고 오프닝/이전 전투를 건너뜁니다. 이후 86번 전투/엔딩은 정상 진행합니다.\n")
        TEXT("  같은 명령으로 재시험 / QA Reset으로 일반 오프닝부터 시작");
}
void ShowCameraHint(const FString& Status)
{
    if (GEngine) GEngine->AddOnScreenDebugMessage(0x4f425141,30.f,FColor::Cyan,
        Status+TEXT("\nQA Camera Start | 1/2 FOV  3/4 Pitch  5/6 Yaw  7/8 Roll | QA Camera End: Destroy enemies"));
}
}

bool UOBQACheatManager::ShowUIPreview(int32 UIId,FString& Reply)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    auto* PC=Cast<AOBPlayerController>(GetOuterAPlayerController());
    if (!PC || !PC->IsLocalController() || !GetWorld()->GetGameViewport())
    { Reply=TEXT("QA UI requires a local player viewport."); return false; }
    if (!UIPreview)
    {
        auto* Class=LoadClass<UOBPlayerOverlayWidget>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/WBP_PlayerOverlay.WBP_PlayerOverlay_C"));
        auto* Preview=Class ? CreateWidget<UOBPlayerOverlayWidget>(PC,Class) : nullptr;
        if (!Preview || !Preview->SetQAUIPreview(UIId) || !Preview->AddToPlayerScreen(1000))
        { Reply=TEXT("QA UI: failed to create the training UI preview."); return false; }
        const bool bAlreadyPaused=UGameplayStatics::IsGamePaused(this);
        if (!bAlreadyPaused && !PC->SetPause(true))
        { Preview->RemoveFromParent(); Reply=TEXT("QA UI: could not pause gameplay; preview cancelled."); return false; }
        UIPreview=Preview; bPreviewOwnsPause=!bAlreadyPaused;
        if (const auto* HUD=PC->GetOBHUD()) HiddenOverlay=HUD->GetPlayerOverlayWidget();
        if (HiddenOverlay.IsValid())
        {
            PreviousOverlayVisibility=static_cast<uint8>(HiddenOverlay->GetVisibility());
            HiddenOverlay->SetVisibility(ESlateVisibility::Hidden);
        }
        // UI narration can continue during an engine pause; suspend only sounds we own restoring.
        for (TObjectIterator<UAudioComponent> It;It;++It)
            if (It->GetWorld()==GetWorld() && It->IsPlaying() && !It->bIsPaused)
            { PausedAudio.Add(*It); It->SetPaused(true); }
        PC->ResetQAInputState(false);
        PC->OnEndPlay.AddUniqueDynamic(this,&ThisClass::HandlePreviewOwnerEndPlay);
    }
    else if (!UIPreview->SetQAUIPreview(UIId))
    { Reply=TEXT("QA UI: requested page could not be displayed."); return false; }
    Reply=FString::Printf(TEXT("QA UI %d | 미리보기 중 (진행 일시정지). 닫기: QA UI Close"),UIId);
    return true;
#else
    return false;
#endif
}

void UOBQACheatManager::CloseUIPreview()
{
    if (!UIPreview) return;
    if (UIPreview) UIPreview->RemoveFromParent();
    UIPreview=nullptr;
    if (HiddenOverlay.IsValid()) HiddenOverlay->SetVisibility(static_cast<ESlateVisibility>(PreviousOverlayVisibility));
    HiddenOverlay.Reset();
    for (auto Audio : PausedAudio) if (Audio.IsValid()) Audio->SetPaused(false);
    PausedAudio.Reset();
    if (auto* PC=Cast<AOBPlayerController>(GetOuterAPlayerController()))
    {
        if (bPreviewOwnsPause) PC->SetPause(false);
        PC->ResetQAInputState(false);
        PC->OnEndPlay.RemoveDynamic(this,&ThisClass::HandlePreviewOwnerEndPlay);
    }
    bPreviewOwnsPause=false;
}
void UOBQACheatManager::HandlePreviewOwnerEndPlay(AActor*,EEndPlayReason::Type)
{
    CloseUIPreview();
    if (auto* Spline=GetCameraTuningSpline()) Spline->EndCameraTuningForQA();
    if (GEngine) GEngine->RemoveOnScreenDebugMessage(0x4f425141);
}

UOBPlayerSplineComponent* UOBQACheatManager::GetCameraTuningSpline() const
{
    auto* PC=GetOuterAPlayerController();
    auto* Player=PC ? Cast<AOBPlayerCharacter>(PC->GetPawn()) : nullptr;
    return Player ? Player->GetPlayerSplineComponent() : nullptr;
}

bool UOBQACheatManager::IsCameraTuningActive() const
{
    const auto* Spline=IsEnabledBuild() ? GetCameraTuningSpline() : nullptr;
    return Spline && Spline->IsCameraEditingForQA();
}

bool UOBQACheatManager::HandleCameraTuningKey(const FKey& Key,bool bAdjust)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    if (!IsCameraTuningActive() || (Key!=EKeys::One && Key!=EKeys::Two && Key!=EKeys::Three && Key!=EKeys::Four
        && Key!=EKeys::Five && Key!=EKeys::Six && Key!=EKeys::Seven && Key!=EKeys::Eight)) return false;
    auto* Viewport=GetWorld()->GetGameViewport();
    if (Viewport && Viewport->ViewportConsole && Viewport->ViewportConsole->ConsoleActive()) return false;
    if (!bAdjust) return true;
    auto* Spline=GetCameraTuningSpline();
    if (!Spline->AdjustCameraForQA(Key==EKeys::One ? 1.f : Key==EKeys::Two ? -1.f : 0.f,
        Key==EKeys::Three ? 1.f : Key==EKeys::Four ? -1.f : 0.f,
        Key==EKeys::Five ? 1.f : Key==EKeys::Six ? -1.f : 0.f,
        Key==EKeys::Seven ? 1.f : Key==EKeys::Eight ? -1.f : 0.f)) return false;
    const FString Status=Spline->GetCameraTuningStatusForQA();
    UE_LOG(LogOBQA,Display,TEXT("%s"),*Status);
    GetOuterAPlayerController()->ClientMessage(Status);ShowCameraHint(Status);
    return true;
#else
    return false;
#endif
}

bool UOBQACheatManager::ExecuteCameraCommand(const TArray<FString>& Args,FString& Reply)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    auto* PC=GetOuterAPlayerController();auto* World=GetWorld();
    if (!World || !PC || !World->IsGameWorld() || UWorld::RemovePIEPrefix(World->GetOutermost()->GetName())!=TEXT("/Game/Maps/Stage_01"))
    { Reply=TEXT("잘못된 접근: QA Zone / Camera is Stage_01-only.");return false; }
    auto* Spline=GetCameraTuningSpline();
    if (!Spline) { Reply=TEXT("QA Camera requires the Stage_01 player.");return false; }
    if (Args[0].Equals(TEXT("zone"),ESearchCase::IgnoreCase))
    {
        if (Args.Num()!=2 || Args[1].Len()!=1 || Args[1][0]<'1' || Args[1][0]>'6')
        { Reply=TEXT("사용법: QA Zone <1~6>");return false; }
        if (auto* OBPC=Cast<AOBPlayerController>(PC); OBPC && OBPC->GetOBHUD() && OBPC->GetOBHUD()->IsMenuVisible())
        { Reply=TEXT("Close the menu or QA Reset before QA Zone.");return false; }
        const int32 Points[]={7,19,44,64,72,86};FString Error;
        if (!Spline->EnterCameraTuningForQA(Points[Args[1][0]-'1'],Error)) { Reply=TEXT("QA Zone rejected: ")+Error;return false; }
        if (auto* OBPC=Cast<AOBPlayerController>(PC)) OBPC->ResetQAInputState(false);
        PC->OnEndPlay.AddUniqueDynamic(this,&ThisClass::HandlePreviewOwnerEndPlay);
        Reply=Spline->GetCameraTuningStatusForQA();ShowCameraHint(Reply);return true;
    }
    const bool bEnd=Args.Num()==2 && Args[1].Equals(TEXT("end"),ESearchCase::IgnoreCase);
    const bool bStart=Args.Num()==2 && Args[1].Equals(TEXT("start"),ESearchCase::IgnoreCase);
    if (Args.Num()!=1 && !bEnd && !bStart) { Reply=TEXT("사용법: QA Camera | QA Camera Start | QA Camera End");return false; }
    if (!Spline->IsCameraTuningForQA()) { Reply=TEXT("QA Camera: 먼저 QA Zone <1~6>을 입력하세요.");return false; }
    if (bStart && !Spline->StartCameraTuningForQA()) { Reply=TEXT("QA Camera Start rejected.");return false; }
    Reply=Spline->GetCameraTuningStatusForQA();
    if (bEnd)
    {
        Spline->EndCameraTuningForQA();PC->OnEndPlay.RemoveDynamic(this,&ThisClass::HandlePreviewOwnerEndPlay);
        if (auto* OBPC=Cast<AOBPlayerController>(PC)) OBPC->ResetQAInputState(false);
        if (GEngine) GEngine->RemoveOnScreenDebugMessage(0x4f425141);
        Reply+=TEXT("\nQA Camera End | 해당 Zone의 적을 Destroy하고 조정을 종료했습니다. 같은 QA Zone을 입력하면 새로 스폰됩니다.");
    }
    else ShowCameraHint(Reply);
    return true;
#else
    Reply=TEXT("QA Camera is disabled in Shipping and Test builds.");return false;
#endif
}

bool UOBQACheatManager::ExecuteBreachCommand(const TArray<FString>& Args,FString& Reply)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    const bool bTrigger=Args.Num()==2 && Args[1].Equals(TEXT("trigger"),ESearchCase::IgnoreCase);
    if (Args.Num()>2 || (Args.Num()==2 && !bTrigger && Args[1]!=TEXT("83") && Args[1]!=TEXT("85")))
    { Reply=BuildQABreachHelp();return false; }
    auto* PC=Cast<AOBPlayerController>(GetOuterAPlayerController());auto* World=GetWorld();
    if (!World || !PC || !PC->IsLocalController() || !World->IsGameWorld()
        || UWorld::RemovePIEPrefix(World->GetOutermost()->GetName())!=TEXT("/Game/Maps/Stage_01"))
    { Reply=TEXT("잘못된 접근: QA Breach is Stage_01-only and requires a local player.");return false; }
    auto* Player=Cast<AOBPlayerCharacter>(PC->GetPawn());
    auto* Flow=Player ? Player->GetStageFlowComponent() : nullptr;
    if (!Flow || !Flow->IsStageActive() || !Flow->bUseZone6Breach)
    { Reply=TEXT("QA Breach requires the active Stage_01 breach flow.");return false; }
    if (bTrigger)
    {
        if (!Flow->IsWaitingForBreach() || UGameplayStatics::IsGamePaused(this))
        { Reply=TEXT("QA Breach Trigger: 85번 브리칭 대기 중에만 사용할 수 있습니다.");return false; }
        if (!Flow->AttachBreachChargeForQA()) { Reply=TEXT("QA Breach Trigger: C4 부착에 실패했습니다.");return false; }
        Reply=TEXT("QA Breach Trigger | C4를 테스트로 부착했습니다.");return true;
    }
    CloseUIPreview();
    const FString Point=Args.Num()==2 ? Args[1] : TEXT("85");
    Reply=FString::Printf(TEXT("QA Breach | Stage_01을 다시 로드하고 %s번부터 시작합니다."),*Point);
    UGameplayStatics::OpenLevel(PC,FName(TEXT("/Game/Maps/Stage_01")),true,TEXT("OBQABreach=")+Point);
    return true;
#else
    Reply=TEXT("QA Breach is disabled in Shipping and Test builds.");return false;
#endif
}

bool UOBQACheatManager::IsTrainingMap(const UWorld* World)
{
    return World && UWorld::RemovePIEPrefix(World->GetOutermost()->GetName())==TEXT("/Game/Maps/TutorialMap");
}

AOBTrainingDirector* UOBQACheatManager::RequireTrainingMap(const FString& Command, FString& Reply) const
{
    auto* World=GetWorld();
    if (!IsTrainingMap(World))
    {
        Reply=FString::Printf(TEXT("잘못된 접근: QA %s is TutorialMap-only. CurrentMap=%s Expected=/Game/Maps/TutorialMap"),
            *Command,World ? *UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()) : TEXT("None"));
        return nullptr;
    }
    for (TActorIterator<AOBTrainingDirector> It(World); It; ++It) return *It;
    Reply=TEXT("잘못된 접근: TutorialMap has no TrainingDirector.");
    return nullptr;
}

void UOBQACheatManager::QA(const FString& CommandLine)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    FString Reply;
    const bool bSuccess=ExecuteCommand(CommandLine,Reply);
    TArray<FString> Lines; Reply.ParseIntoArrayLines(Lines);
    for (const FString& Line : Lines)
    {
        if (bSuccess) { UE_LOG(LogOBQA,Display,TEXT("%s"),*Line); }
        else { UE_LOG(LogOBQA,Warning,TEXT("%s"),*Line); }
        if (auto* PC=GetOuterAPlayerController()) PC->ClientMessage(Line);
    }
#else
    UE_LOG(LogOBQA,Warning,TEXT("QA is disabled in Shipping and Test builds."));
#endif
}

bool UOBQACheatManager::ExecuteCommand(const FString& CommandLine, FString& Reply)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    TArray<FString> Args; CommandLine.ParseIntoArrayWS(Args);
    const FString Command=Args.IsEmpty() ? TEXT("help") : Args[0].ToLower();
    if (Command==TEXT("zone") || Command==TEXT("camera")) return ExecuteCameraCommand(Args,Reply);
    if (Command==TEXT("breach")) return ExecuteBreachCommand(Args,Reply);
    const bool bGoto=Command==TEXT("goto");
    const bool bUI=Command==TEXT("ui");
    const bool bKnown=Command==TEXT("help") || Command==TEXT("list") || Command==TEXT("status") ||
        Command==TEXT("reset") || bGoto || bUI || Command==TEXT("restart") || Command==TEXT("next");
    if (!bKnown)
    { Reply=TEXT("알 수 없는 QA 명령입니다. QA Help로 전체 사용법을 확인하세요."); return false; }
    if (bGoto && Args.Num()!=2)
    {
        Reply=TEXT("QA Goto는 단계 이름을 하나만 받습니다. 맵 이름이나 여러 단계를 붙이지 마세요.\n")
            TEXT("  영점 / 명중 검사: QA Goto Accuracy\n  재장전 검사: QA Goto Reload\n")
            TEXT("  각 명령은 따로 입력하세요. 전체 단계는 QA List로 확인할 수 있습니다.");
        return false;
    }
    int32 UIId=0;
    const bool bUIClose=bUI && Args.Num()==2 && Args[1].Equals(TEXT("close"),ESearchCase::IgnoreCase);
    if (bUI && (Args.Num()!=2 || (!bUIClose && (!Args[1].IsNumeric() || !LexTryParseString(UIId,*Args[1]) || UIId<3001 || UIId>3010))))
    { Reply=TEXT("사용법: QA UI <3001~3010> 또는 QA UI Close\n")+BuildQAUIList(); return false; }
    if (!bGoto && !bUI && Args.Num()>1)
    { Reply=TEXT("이 QA 명령은 추가 인수를 받지 않습니다. QA Help로 사용법을 확인하세요."); return false; }
    if (Command==TEXT("help"))
    {
        Reply=TEXT("[QA 도움말 - PIE / Standalone / Development]\n")
            TEXT("[공통 명령 - 모든 맵]\n")
            TEXT("  QA Help    - 전체 사용법과 단계별 명령 표시\n")
            TEXT("  QA List    - 단계 이름과 설명 표시\n")
            TEXT("  QA Status  - 현재 맵 / 단계 / 탄약 / 남은 시간 확인\n")
            TEXT("  QA Reset   - 현재 맵을 다시 로드하여 처음부터 시작\n")
            TEXT("[TutorialMap 전용 명령]\n")
            TEXT("  QA Goto <단계> - 해당 위치로 이동하고 검사 시작 (단계 하나만 입력)\n")
            TEXT("  QA Restart     - 현재 단계 초기화 후 다시 시작\n")
            TEXT("  QA Next        - 다음 QA 단계로 이동\n")
            TEXT("  다른 맵에서는 실행하지 않고 잘못된 접근 경고를 출력합니다.\n");
        Reply+=BuildQACheckpointList(GetWorld());
        Reply+=TEXT("\n")+BuildQAUIList();
        Reply+=TEXT("\n")+BuildQACameraHelp();
        Reply+=TEXT("\n")+BuildQABreachHelp();
        Reply+=TEXT("\n한 줄에 명령 하나씩 입력하세요. 예: QA Goto Accuracy / QA Goto Reload (각각 입력)\n")
            TEXT("도움말은 콘솔 / Output Log에 표시됩니다. 게임 콘솔이 한 줄이면 ~ 키를 한 번 더 눌러 펼치세요.");
        return true;
    }
    if (Command==TEXT("list"))
    {
        Reply=BuildQACheckpointList(GetWorld())+TEXT("\n")+BuildQAUIList()+TEXT("\n")+BuildQACameraHelp()+TEXT("\n")+BuildQABreachHelp();
        return true;
    }
    auto* PC=GetOuterAPlayerController();
    auto* World=GetWorld();
    if (!World || !PC || !World->IsGameWorld()) { Reply=TEXT("QA requires a local game world and PlayerController."); return false; }
    if (Command==TEXT("status"))
    {
        Reply=FString::Printf(TEXT("QA Map=%s Pawn=%s Location=%s"),
            *UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()),*GetNameSafe(PC->GetPawn()),
            PC->GetPawn() ? *PC->GetPawn()->GetActorLocation().ToCompactString() : TEXT("None"));
        for (TActorIterator<AOBTrainingDirector> It(World); It; ++It) Reply+=TEXT(" ")+It->GetQAStatus();
        if (auto* Spline=GetCameraTuningSpline(); Spline && Spline->IsCameraTuningForQA()) Reply+=TEXT("\n")+Spline->GetCameraTuningStatusForQA();
        return true;
    }
    if (Command==TEXT("reset"))
    {
        CloseUIPreview();
        if (auto* Spline=GetCameraTuningSpline()) Spline->EndCameraTuningForQA();
        const FString Map=UWorld::RemovePIEPrefix(World->GetOutermost()->GetName());
        Reply=TEXT("QA Reset: reloading ")+Map;
        UGameplayStatics::OpenLevel(PC,FName(*Map));
        return true;
    }
    auto* Director=RequireTrainingMap(Command,Reply);
    if (!Director) return false;
    if (bUI)
    {
        if (bUIClose) { CloseUIPreview(); Reply=TEXT("QA UI Close | 미리보기 종료"); return true; }
        return ShowUIPreview(UIId,Reply);
    }
    CloseUIPreview();
    FName Checkpoint=bGoto ? FName(*Args[1]) : Director->GetQACheckpoint();
    const auto Choices=Director->GetQACheckpoints();
    if (Command==TEXT("next"))
    {
        const int32 Index=Choices.IndexOfByKey(Checkpoint);
        if (Index==INDEX_NONE || !Choices.IsValidIndex(Index+1))
        { Reply=TEXT("QA Next: no next checkpoint. Use QA List or QA Goto <checkpoint>."); return false; }
        Checkpoint=Choices[Index+1];
    }
    FString Error;
    if (!Director->EnterQACheckpoint(Checkpoint,Error)) { Reply=TEXT("QA rejected: ")+Error; return false; }
    Reply=TEXT("QA entered ")+Checkpoint.ToString()+TEXT(" | ")+Director->GetQAStatus();
    return true;
#else
    Reply=TEXT("QA is disabled in this build.");
    return false;
#endif
}

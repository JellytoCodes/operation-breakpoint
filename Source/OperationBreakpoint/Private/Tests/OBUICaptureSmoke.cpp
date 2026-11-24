#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Editor.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Camera/PlayerCameraManager.h"
#include "EngineUtils.h"
#include "UnrealClient.h"
#include "Kismet/GameplayStatics.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/TextBlock.h"
#include "UObject/StrongObjectPtr.h"
#include "Core/OBPlayerController.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Stage/OBStageFlowComponent.h"
#include "Stage/OBStageCinematicWidget.h"
#include "MediaPlayer.h"
#include "MediaSource.h"
#include "Widget/OBHUD.h"
#include "Widget/OBTitleWidget.h"
#include "Widget/OBPlayerOverlayWidget.h"
#include "Widget/OBHudPanelWidget.h"
#include "Widget/OBTrainingSkipWidget.h"
#include "Widget/OBDamageScreenWidget.h"
#include "Training/OBTrainingDirector.h"
#include "Training/OBTrainingResultWidget.h"
#include "Training/OBTrainingStatusWidget.h"
#include "Input/OBDeviceSettingsSubsystem.h"
#include "Input/OBOpenFireSubsystem.h"
#include "MediaPipe/OBMediaPipeOSCSubsystem.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWidget.h"

// Disposable PIE presentation captures. No assets, calibration, routes or
// training records are saved. Sampled UI states are identified in the manifest.
class FOBUICaptureSmoke
{
    struct FState;
    struct FCase { FString File,Title,Group,Kind;float Wait=1.f;TFunction<void(FState&)> Apply; };
    struct FState
    {
        FString Suite,Out;TArray<FCase> Cases;TArray<TSharedPtr<FJsonValue>> Captured;
        TWeakObjectPtr<UWorld> World;TWeakObjectPtr<AOBPlayerController> PC;
        TStrongObjectPtr<UOBPlayerOverlayWidget> Preview;
        int32 Index=0;double At=0,Began=FPlatformTime::Seconds();bool Requested=false,Applied=false,KeepGuideFresh=false;
        TSet<int32> MovieCaptions;
    };
    static UOBStageFlowComponent* Flow(FState& S) { auto* P=Cast<AOBPlayerCharacter>(S.PC->GetPawn());return P ? P->GetStageFlowComponent() : nullptr; }
    static AOBTrainingDirector* Director(FState& S) { for (TActorIterator<AOBTrainingDirector> It(S.World.Get());It;++It) return *It;return nullptr; }
    static UOBPlayerOverlayWidget* HUD(FState& S) { return S.PC->GetOBHUD()->GetPlayerOverlayWidget(); }
    static UOBPlayerOverlayWidget* VisibleOverlay(FState& S)
    {
        TArray<UUserWidget*> Widgets;UWidgetBlueprintLibrary::GetAllWidgetsOfClass(S.World.Get(),Widgets,UOBPlayerOverlayWidget::StaticClass(),false);
        for (auto* W:Widgets) if (W->IsInViewport() && W->IsVisible()) return Cast<UOBPlayerOverlayWidget>(W);
        return HUD(S);
    }
    static UOBPlayerOverlayWidget* Sample(FState& S,int32 Mask)
    {
        if (Director(S)) S.PC->ConsoleCommand(TEXT("QA UI Close"),false);
        if (auto* F=Flow(S);F && F->IsStageActive()) { F->SuspendForQA();F->SetComponentTickEnabled(false); }
        if (auto* D=Director(S)) D->SetActorTickEnabled(false);
        UGameplayStatics::SetGamePaused(S.World.Get(),true);
        if (S.PC->PlayerCameraManager) S.PC->PlayerCameraManager->UpdateCamera(0.f);
        if (S.Preview.IsValid()) S.Preview->RemoveFromParent();
        auto* Class=LoadClass<UOBPlayerOverlayWidget>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/WBP_PlayerOverlay.WBP_PlayerOverlay_C"));
        S.Preview.Reset(CreateWidget<UOBPlayerOverlayWidget>(S.PC.Get(),Class));
        S.Preview->AddToPlayerScreen(900);S.Preview->StageElements=Mask;S.Preview->SetStageMode();
        if (auto* Original=HUD(S)) Original->SetVisibility(ESlateVisibility::Hidden);
        S.KeepGuideFresh=false;return S.Preview.Get();
    }
    static void Add(FState& S,const TCHAR* File,const TCHAR* Title,const TCHAR* Group,TFunction<void(FState&)> Fn,float Wait=1.f,const TCHAR* Kind=TEXT("게임 UI 상태 예시"))
    { S.Cases.Add({File,Title,Group,Kind,Wait,MoveTemp(Fn)}); }
    static FOBTrainingResultData StageData(bool Passed)
    {
        FOBTrainingResultData D;D.bFinished=D.bPrototypeFlow=D.bHasStageScore=true;D.bScenarioCompleted=Passed;
        D.StageScore=Passed?92.5:54.;D.StageRank=FOBStageScoring::Rank(D.StageScore);D.ShotsFired=40;D.ValidHits=Passed?31:17;D.TotalSeconds=Passed?213.6f:265.f;
        return D;
    }
    static void GuideState(FState& S,int32 State,bool Stage)
    {
        auto* O=Sample(S,Stage ? 16|32|64 : 64);
        if (Stage) { auto* F=Flow(S);F->Phase=EOBStagePhase::Zone6Breach;F->bBreachSawNeutral=true;F->bBreachRequested=F->bBreachCompleted=false;O->SetStageTime(10);O->SetZoneProgress(6,6); }
        else if (auto* D=Director(S)) { D->CurrentStep=EOBTrainingStep::Breach;D->bPrototypePaused=false;D->Result.bFinished=false;O->GetTrainingMarkers()->SetDirector(D); }
        auto* M=S.World->GetGameInstance()->GetSubsystem<UOBMediaPipeOSCSubsystem>();
        FOBBreachGuideState G;G.bFresh=State>0;G.bBreachMode=State>1;G.bPlayerTracked=State>2;
        if (State>3) { G.LeftVisibility=G.RightVisibility=1;G.LeftWrist=FVector2D(.7,.46);G.RightWrist=FVector2D(.28,.46); }
        G.bLeftInside=G.bRightInside=State>4;G.Progress=State==5 ? .5f : State>5 ? 1.f : 0.f;G.bReady=State>5;
        M->LatestBreachGuide=G;M->LastBreachGuideTime=State>0 ? FPlatformTime::Seconds() : 0;S.KeepGuideFresh=State>0;
    }
    static void Configure(FState& S)
    {
        if (S.Suite==TEXT("title"))
        {
            Add(S,TEXT("title-idle"),TEXT("타이틀 · 대기 영상"),TEXT("타이틀"),[](FState&){},4,TEXT("실제 재생 화면"));
            Add(S,TEXT("title-entrance"),TEXT("타이틀 · 훈련장 이동 영상"),TEXT("타이틀"),[](FState& X) {
                TArray<UUserWidget*> W;UWidgetBlueprintLibrary::GetAllWidgetsOfClass(X.World.Get(),W,UOBTitleWidget::StaticClass(),false);
                if (!W.IsEmpty()) Cast<UOBTitleWidget>(W[0])->RequestStart();
            },.6f,TEXT("실제 재생 화면"));
            Add(S,TEXT("title-transition-black"),TEXT("훈련장 이동 · 암전 전환"),TEXT("타이틀"),[](FState&){},.6f,TEXT("실제 전환 화면"));return;
        }
        if (S.Suite==TEXT("tutorial"))
        {
            Add(S,TEXT("tutorial-intro"),TEXT("훈련장 진입 · 건너뛰기 안내"),TEXT("훈련장"),[](FState&){},4,TEXT("실제 훈련 진입 화면"));
            Add(S,TEXT("tutorial-skip-hold"),TEXT("훈련 건너뛰기 · 길게 누르기"),TEXT("훈련장"),[](FState& X) {
                auto* O=HUD(X);auto* Skip=Cast<UOBTrainingSkipWidget>(O->GetWidgetFromName(TEXT("TrainingSkip")));
                if (Skip) { Skip->HandleTrigger(false);Skip->HandleTrigger(true);Skip->AdvanceHold(1.5f);Skip->SetDesignerFlags(EWidgetDesignFlags::Designing); }
            });
            const TCHAR* Checkpoints[]={TEXT("WeaponCheck"),TEXT("AimHold"),TEXT("Cover"),TEXT("Reload"),TEXT("Accuracy"),TEXT("Countdown"),TEXT("Shooting1"),TEXT("Shooting2"),TEXT("Breach")};
            const TCHAR* CheckpointTitles[]={TEXT("훈련 진행 · 격발 확인"),TEXT("훈련 진행 · 조준"),TEXT("훈련 진행 · 엄폐"),TEXT("훈련 진행 · 재장전"),TEXT("훈련 진행 · 영점 사격"),TEXT("훈련 진행 · 시작 대기"),TEXT("훈련 진행 · 모의 사격 1"),TEXT("훈련 진행 · 모의 사격 2"),TEXT("훈련 진행 · 브리칭")};
            for (int32 I=0;I<9;++I)
            {
                const FString Point=Checkpoints[I],File=FString::Printf(TEXT("training-checkpoint-%d"),I+1);
                Add(S,*File,CheckpointTitles[I],TEXT("훈련장"),[Point](FState& X){
                    auto* Skip=Cast<UOBTrainingSkipWidget>(HUD(X)->GetWidgetFromName(TEXT("TrainingSkip")));if (Skip) Skip->SetDesignerFlags(EWidgetDesignFlags::None);
                    X.PC->ConsoleCommand(TEXT("QA Goto ")+Point,false);if (auto* D=Director(X)) D->FinishPrototypeNarration();
                },3.f,TEXT("게임 QA 단계 진입 화면"));
            }
            const TCHAR* Names[]={TEXT("훈련장 안내"),TEXT("격발 확인"),TEXT("조준 훈련"),TEXT("엄폐 · 재장전"),TEXT("영점 사격"),TEXT("모의 훈련 안내 1"),TEXT("모의 훈련 안내 2")};
            for (int32 I=0;I<7;++I)
            {
                const FString File=FString::Printf(TEXT("guide-%d"),I+1),Title=Names[I];
                Add(S,*File,*Title,TEXT("훈련 안내"),[I](FState& X){X.PC->ConsoleCommand(FString::Printf(TEXT("QA UI %d"),3001+I),false);},1.5f,TEXT("게임 UI 미리보기"));
            }
            for (int32 Id=3008;Id<=3010;++Id)
            {
                const FString File=FString::Printf(TEXT("tutorial-result-%d"),Id);
                const TCHAR* Name=Id==3008 ? TEXT("훈련 중단 · 일시정지") : Id==3009 ? TEXT("훈련 결과 · 통과") : TEXT("훈련 결과 · 실패");
                Add(S,*File,Name,TEXT("훈련 결과"),[Id](FState& X){
                    if (auto* R=VisibleOverlay(X)->GetTrainingResultWidget()) R->SetDesignerFlags(EWidgetDesignFlags::None);
                    X.PC->ConsoleCommand(FString::Printf(TEXT("QA UI %d"),Id),false);
                },1.5f,TEXT("게임 UI 미리보기"));
                const FString Held=File+TEXT("-hold");
                Add(S,*Held,*FString::Printf(TEXT("%s · 길게 누르기 50%%"),Name),TEXT("훈련 결과"),[](FState& X){
                    auto* R=VisibleOverlay(X)->GetTrainingResultWidget();R->HandleTrigger(false);R->HandleTrigger(true);R->AdvanceInteraction(1);R->SetDesignerFlags(EWidgetDesignFlags::Designing);
                });
            }
            Add(S,TEXT("training-hud"),TEXT("훈련 HUD · 목표 · 시간 · 자막"),TEXT("훈련장"),[](FState& X){
                auto* O=Sample(X,8|16|32);O->SetHealth(5,5);O->SetStageTime(90);O->SetZoneProgress(1,3);
                O->ShowPersistentSubtitle(FText::FromString(TEXT("교관")),FText::FromString(TEXT("표적을 정확히 조준하고 방아쇠를 당기세요.")));
                Cast<UOBMissionWidget>(O->GetWidgetFromName(TEXT("Mission")))->UpdateTrainingObjectives(FOBTrainingResultData(),true);
            });
            Add(S,TEXT("mission-mixed"),TEXT("훈련 목표 · 미실행 · 통과 · 실패"),TEXT("훈련장"),[](FState& X){
                auto* O=Sample(X,8);FOBTrainingResultData D;
                for (int32 I=0;I<2;++I) { auto& R=D.Steps.AddDefaulted_GetRef();R.Step=I ? EOBTrainingStep::AimHold : EOBTrainingStep::WeaponCheck;R.Outcome=I ? EOBTrainingOutcome::Passed : EOBTrainingOutcome::Failed; }
                Cast<UOBMissionWidget>(O->GetWidgetFromName(TEXT("Mission")))->UpdateTrainingObjectives(D,true);
            });
            Add(S,TEXT("system-message"),TEXT("시스템 안내 메시지"),TEXT("메시지"),[](FState& X){Sample(X,0)->ShowPersistentSystemMessage(FText::FromString(TEXT("기초 훈련을 시작합니다. 교관의 명령에 따라 훈련을 진행합니다.")));});
            Add(S,TEXT("subtitle-long"),TEXT("긴 한국어 자막"),TEXT("메시지"),[](FState& X){Sample(X,0)->ShowPersistentSubtitle(FText::FromString(TEXT("상황실")),FText::FromString(TEXT("전방에 적과 인질이 함께 있습니다. 사격 전에 대상을 정확히 식별하고, 엄폐물을 이용해 이동하십시오. 아군의 지원 신호를 확인한 뒤 다음 구역으로 진입합니다.")));});
            for (int32 I=0;I<=2;++I)
            {
                const FString File=FString::Printf(TEXT("aim-%d"),I);
                Add(S,*File,I==0 ? TEXT("조준 · 대기") : I==1 ? TEXT("조준 · 유지 50%") : TEXT("조준 · 완료"),TEXT("조준 · 브리칭"),[I](FState& X){
                    auto* O=Sample(X,64);auto* D=Director(X);D->CurrentStep=EOBTrainingStep::AimHold;D->bPrototypePaused=false;D->Result.bFinished=false;D->HoldSeconds=D->AimHoldSeconds*(I*.5f);O->GetTrainingMarkers()->SetDirector(D);
                });
            }
            const TCHAR* BreachNames[]={TEXT("브리칭 · 카메라 대기"),TEXT("브리칭 · 인식 모드 안내"),TEXT("브리칭 · 플레이어 인식 안내"),TEXT("브리칭 · 손목 인식 안내"),TEXT("브리칭 · 손 위치 조정"),TEXT("브리칭 · 유지 50%"),TEXT("브리칭 · 준비 완료")};
            for (int32 I=0;I<7;++I) {const FString File=FString::Printf(TEXT("training-breach-%d"),I);Add(S,*File,BreachNames[I],TEXT("조준 · 브리칭"),[I](FState& X){GuideState(X,I,false);});}
            return;
        }
        if (S.Suite==TEXT("stage"))
        {
            for (int32 Zone=1;Zone<=6;++Zone)
            {
                const FString File=FString::Printf(TEXT("stage-zone-%d"),Zone);
                Add(S,*File,*FString::Printf(TEXT("Stage_01 · Zone %d HUD"),Zone),TEXT("Stage_01 HUD"),[Zone](FState& X){
                    if (X.Preview.IsValid()) X.Preview->RemoveFromParent();UGameplayStatics::SetGamePaused(X.World.Get(),false);
                    if (auto* F=Flow(X)) F->SetComponentTickEnabled(true);
                    X.PC->ConsoleCommand(FString::Printf(TEXT("QA Zone %d"),Zone),false);
                    auto* O=Sample(X,16|32);O->SetZoneProgress(Zone,6);O->SetStageTime(Zone==6?10:90);O->SetHealth(5,5);
                },1.5f);
            }
            for (int32 Hearts=5;Hearts>=0;--Hearts)
            {
                const FString File=FString::Printf(TEXT("life-%d"),Hearts);
                Add(S,*File,*FString::Printf(TEXT("체력 · %d / 5"),Hearts),TEXT("체력 · 피격"),[Hearts](FState& X){auto* O=Sample(X,16|32);O->SetZoneProgress(3,6);O->SetStageTime(90);O->SetHealth(Hearts,5);});
            }
            Add(S,TEXT("stage-hit"),TEXT("피격 · 화면 효과"),TEXT("체력 · 피격"),[](FState& X){auto* O=Sample(X,16|32);O->SetHealth(4,5);Flow(X)->GetDamageScreenWidget()->PlayHit();},.2f);
            Add(S,TEXT("stage-injured"),TEXT("체력 소진 · 부상 효과"),TEXT("체력 · 피격"),[](FState& X){auto* O=Sample(X,16|32);O->SetHealth(0,5);auto* FX=Flow(X)->GetDamageScreenWidget();FX->ResetEffects();FX->SetInjuredActive(true);},.15f);
            for (int32 I:{0,5,6})
            {
                const FString File=FString::Printf(TEXT("stage-breach-%d"),I);
                Add(S,*File,I==0 ? TEXT("Zone6 · 브리칭 안내") : I==5 ? TEXT("Zone6 · 브리칭 유지 50%") : TEXT("Zone6 · 브리칭 준비 완료"),TEXT("조준 · 브리칭"),[I](FState& X){Flow(X)->GetDamageScreenWidget()->ResetEffects();GuideState(X,I,true);});
            }
            Add(S,TEXT("stage-breach-release"),TEXT("Zone6 · 손을 뺐다가 다시 넣기"),TEXT("조준 · 브리칭"),[](FState& X){GuideState(X,5,true);Flow(X)->bBreachSawNeutral=false;});
            for (bool Pass:{true,false}) for (bool Held:{false,true})
            {
                const FString File=FString::Printf(TEXT("stage-result-%s%s"),Pass?TEXT("clear"):TEXT("fail"),Held?TEXT("-hold"):TEXT(""));
                Add(S,*File,*FString::Printf(TEXT("Stage_01 · %s 결과%s"),Pass?TEXT("성공"):TEXT("실패"),Held?TEXT(" · 길게 누르기 50%"):TEXT("")),TEXT("Stage_01 결과"),[Pass,Held](FState& X){
                    auto* O=Sample(X,128);O->ShowTrainingResult(StageData(Pass));
                    auto* R=O->GetTrainingResultWidget();R->bAutomaticNavigation=false;R->SetStageCompletion(Pass,true);
                    if (Held) {R->HandleTrigger(false);R->HandleTrigger(true);R->AdvanceInteraction(1);R->SetDesignerFlags(EWidgetDesignFlags::Designing);}
                });
            }
            Add(S,TEXT("stage-result-save-failed"),TEXT("Stage_01 · 결과 저장 실패 안내"),TEXT("Stage_01 결과"),[](FState& X){
                auto* O=Sample(X,128);O->ShowTrainingResult(StageData(true));
                auto* R=O->GetTrainingResultWidget();R->bAutomaticNavigation=false;R->SetStageCompletion(true,false);
            });
            Add(S,TEXT("stage-result-no-score"),TEXT("Stage_01 · 결과 수치 없음"),TEXT("Stage_01 결과"),[](FState& X){
                auto* O=Sample(X,128);FOBTrainingResultData D;D.bFinished=D.bPrototypeFlow=true;O->ShowTrainingResult(D);
                auto* R=O->GetTrainingResultWidget();R->bAutomaticNavigation=false;R->SetStageCompletion(true,true);
            });
            return;
        }
        if (S.Suite==TEXT("devices"))
        {
            Add(S,TEXT("f6-target"),TEXT("F6 · 랜덤 표적 · 잔탄 20발"),TEXT("장치 설정"),[](FState& X){X.World->GetGameInstance()->GetSubsystem<UOBDeviceSettingsSubsystem>()->Toggle(1);},2,TEXT("실제 설정 화면"));
            Add(S,TEXT("f6-shot"),TEXT("F6 · 명중 후 표적 이동 · 탄흔"),TEXT("장치 설정"),[](FState& X){
                auto* D=X.World->GetGameInstance()->GetSubsystem<UOBDeviceSettingsSubsystem>();auto* G=X.World->GetGameInstance()->GetSubsystem<UOBOpenFireSubsystem>();
                auto& I=const_cast<FOBOpenFireState&>(G->GetState());I.bHasFrame=true;I.ReceivedAt=FPlatformTime::Seconds();I.Frame.bConnected=I.Frame.bAimValid=true;
                const FVector2D Size=D->Screen->GetCachedGeometry().GetLocalSize();I.Frame.Position=D->GetTargetCenter(Size)/Size;D->Shoot();
            });
            Add(S,TEXT("f6-empty"),TEXT("F6 · 빈 탄창"),TEXT("장치 설정"),[](FState& X){X.World->GetGameInstance()->GetSubsystem<UOBDeviceSettingsSubsystem>()->Ammo=0;});
            Add(S,TEXT("f7-camera"),TEXT("F7 · 카메라 설정"),TEXT("장치 설정"),[](FState& X){X.World->GetGameInstance()->GetSubsystem<UOBDeviceSettingsSubsystem>()->Toggle(2);},5,TEXT("실제 설정 화면"));return;
        }
    }
    static void Finish(const TSharedPtr<FState>& S,bool OK)
    {
        if (auto* D=Director(*S)) D->bQASession=true;
        FString Json;auto Root=MakeShared<FJsonObject>();Root->SetStringField(TEXT("suite"),S->Suite);Root->SetBoolField(TEXT("complete"),OK);Root->SetArrayField(TEXT("screenshots"),S->Captured);
        FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Json));FFileHelper::SaveStringToFile(Json,*(S->Out/TEXT("manifest.json")));
        S->Preview.Reset();GEditor->RequestEndPlayMap();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([OK](float){if (GEditor->PlayWorld) return true;FPlatformMisc::RequestExitWithStatus(false,OK?0:1);return false;}),.2f);
    }
    static bool Movies(const TSharedPtr<FState>& S,double Now)
    {
        auto* F=Flow(*S);
        auto Capture=[&](const FString& File,const FString& Title) {
            FScreenshotRequest::RequestScreenshot(S->Out/(File+TEXT(".png")),true,false);
            auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("file"),S->Suite/(File+TEXT(".png")));
            R->SetStringField(TEXT("title"),Title);R->SetStringField(TEXT("group"),TEXT("영상 자막"));R->SetStringField(TEXT("kind"),TEXT("실제 영상 재생 · 저장된 자막"));S->Captured.Add(MakeShared<FJsonValueObject>(R));
        };
        if (S->Requested)
        {
            if (Now-S->At<1.) return true;
            bool OK=S->MovieCaptions.Num()==4;
            for (const auto& V:S->Captured) OK &= FPaths::FileExists(S->Out/FPaths::GetCleanFilename(V->AsObject()->GetStringField(TEXT("file"))));
            Finish(S,OK);return false;
        }
        if (F->Phase==EOBStagePhase::Opening)
        {
            const int32 Id=F->PresentationSubtitleId;const auto* Row=F->FindScript(Id);
            const auto* Caption=F->GetOverlay() ? F->GetOverlay()->GetSubtitleWidget() : nullptr;
            if (Id>=2001 && Id<=2004 && Row && Caption && Caption->GetRenderOpacity()>.95f
                && !S->MovieCaptions.Contains(Id) && F->VoiceElapsed>Row->OpeningStartSeconds+1.4f)
            {Capture(FString::Printf(TEXT("opening-%d"),Id),FString::Printf(TEXT("오프닝 자막 · %d / 4"),Id-2000));S->MovieCaptions.Add(Id);}
            return true;
        }
        if (!S->Applied)
        {
            // Reuse the normal movie/caption widgets without finalizing or saving
            // a fabricated training result just to photograph an ending.
            F->ClearPresentation();F->Phase=EOBStagePhase::Ending;F->bSucceeded=S->Suite==TEXT("cinematic-clear");
            F->bEndingVideoOpened=false;F->PresentationElapsed=0;F->BeginPresentationSubtitles();
            F->HoldPlayerMovement();F->Cinematic=CreateWidget<UOBStageCinematicWidget>(S->PC.Get());F->Cinematic->AddToPlayerScreen(-100);
            F->Cinematic->OnVideoOpened.AddUObject(F,&UOBStageFlowComponent::HandleVideoOpened);
            F->Cinematic->OpenVideo((F->bSucceeded?F->ClearVideo:F->FailVideo).LoadSynchronous(),false,0);
            S->Applied=true;S->At=Now;return true;
        }
        auto* M=F->Cinematic ? F->Cinematic->GetMediaPlayer() : nullptr;
        if (F->bEndingVideoOpened && M && M->IsPlaying() && M->GetTime().GetTotalSeconds()>2.)
        {Capture(TEXT("ending"),F->bSucceeded?TEXT("성공 엔딩 영상 · 자막"):TEXT("실패 엔딩 영상 · 자막"));S->Requested=true;S->At=Now;}
        return true;
    }
public:
    static void Start()
    {
        auto S=MakeShared<FState>();S->Suite=TEXT("tutorial");FParse::Value(FCommandLine::Get(),TEXT("UICaptureSuite="),S->Suite);
        FString Folder=TEXT("Saved/Tests/UIReview/Captures/Current");FParse::Value(FCommandLine::Get(),TEXT("UICaptureOutput="),Folder);
        S->Out=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/Folder/S->Suite);IFileManager::Get().MakeDirectory(*S->Out,true);
        FRequestPlaySessionParams P;P.bAllowOnlineSubsystem=false;
        P.GlobalMapOverride=S->Suite==TEXT("title") || S->Suite==TEXT("devices") ? TEXT("/Game/Maps/TitleMap") : S->Suite==TEXT("stage") || S->Suite.StartsWith(TEXT("cinematic")) ? TEXT("/Game/Maps/Stage_01") : TEXT("/Game/Maps/TutorialMap");
        P.EditorPlaySettings=DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(),GetTransientPackage());
        P.EditorPlaySettings->NewWindowWidth=1920;P.EditorPlaySettings->NewWindowHeight=1080;P.EditorPlaySettings->CenterNewWindow=true;
        GEditor->RequestPlaySession(P);GAreScreenMessagesEnabled=false;
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float){
            const double Now=FPlatformTime::Seconds();if (Now-S->Began>240) {Finish(S,false);return false;}
            auto* W=GEditor->PlayWorld.Get();auto* PC=W ? Cast<AOBPlayerController>(W->GetFirstPlayerController()) : nullptr;
            if (!PC || !PC->GetOBHUD() || W->GetTimeSeconds()<.8f) return true;
            if (!S->PC.IsValid()) {S->PC=PC;S->World=W;if (auto Pawn=PC->GetPawn()) Pawn->SetCanBeDamaged(false);if (auto* D=Director(*S)) D->bQASession=true;Configure(*S);}
            if (S->Suite.StartsWith(TEXT("cinematic"))) return Movies(S,Now);
            if (S->Index>=S->Cases.Num()) {Finish(S,true);return false;}
            auto& C=S->Cases[S->Index];
            if (!S->Applied) {C.Apply(*S);S->Applied=true;S->At=Now;return true;}
            if (S->KeepGuideFresh) W->GetGameInstance()->GetSubsystem<UOBMediaPipeOSCSubsystem>()->LastBreachGuideTime=Now;
            if (!S->Requested && Now-S->At>C.Wait)
            {FScreenshotRequest::RequestScreenshot(S->Out/(C.File+TEXT(".png")),true,false);S->Requested=true;S->At=Now;return true;}
            if (S->Requested && Now-S->At>.5f)
            {
                if (!FPaths::FileExists(S->Out/(C.File+TEXT(".png")))) {Finish(S,false);return false;}
                auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("file"),S->Suite/(C.File+TEXT(".png")));R->SetStringField(TEXT("title"),C.Title);R->SetStringField(TEXT("group"),C.Group);R->SetStringField(TEXT("kind"),C.Kind);S->Captured.Add(MakeShared<FJsonValueObject>(R));
                UE_LOG(LogTemp,Display,TEXT("UI_CAPTURE %s %s"),*S->Suite,*C.File);++S->Index;S->Applied=S->Requested=false;
            }
            return true;
        }),.05f);
    }
};
static FAutoConsoleCommand GOBUICaptureAll(TEXT("OB.UI.CaptureAll"),TEXT("Capture the current UI states in disposable PIE; -UICaptureSuite=title|tutorial|stage|devices -UICaptureOutput=<project-relative-folder>. Exits this editor without saving assets."),FConsoleCommandDelegate::CreateStatic(&FOBUICaptureSmoke::Start));
#endif

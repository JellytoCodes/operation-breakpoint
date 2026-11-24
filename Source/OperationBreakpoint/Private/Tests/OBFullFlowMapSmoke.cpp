#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Editor.h"
#include "LevelEditor.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBEnemyCharacter.h"
#include "Character/OBHostageCharacter.h"
#include "Character/OBActionComponent.h"
#include "Core/OBPlayerController.h"
#include "Core/OBGameViewportClient.h"
#include "Stage/OBStageFlowComponent.h"
#include "Training/OBTrainingDirector.h"
#include "Training/OBTrainingTarget.h"
#include "Training/OBTrainingResultWidget.h"
#include "Widget/OBHUD.h"
#include "Widget/OBTitleWidget.h"
#include "Widget/OBUIVideo.h"
#include "Widget/OBPlayerOverlayWidget.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextBlock.h"
#include "Framework/Application/SlateApplication.h"
#include "MediaPlayer.h"
#include "InputKeyEventArgs.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"

// Disposable editor only. Natural course/rail/cinematics/timers and production camera traces.
// No score fixtures, teleports, phase assignments, invulnerability or shortened media/dialogue.
class FOBFullFlowMapSmoke
{
    struct FState
    {
        int32 Step=0, LastZone=0, LastPhase=-1, LastDialogue=-1, LastTrainingStep=-1;
        double Started=FPlatformTime::Seconds(), At=Started, LastTick=Started, LastShot=0;
        bool OK=true, TitleCaptured=false, TutorialRecorded=false, ResultChecked=false;
        bool Holding=false, SawEnding=false, SawIntro=false, SawBreach=false;
        FString Log, LastMap;
        FGuid CompletedSession;
        TSet<int32> ClearZones, FailZones;
        TWeakObjectPtr<UWorld> Previous;
        TArray<TWeakObjectPtr<UMediaPlayer>> OldPlayers;
        TMap<TWeakObjectPtr<AOBEnemyCharacter>,double> ExpectedDamage;
        TMap<int32,double> ClearKillAssessment,FailKillAssessment;
        int32 ClearShots=0,ClearHits=0,FailShots=0,FailHits=0;
        bool FailureRun=false;
    };
    static bool Repeat() { return FParse::Param(FCommandLine::Get(),TEXT("FullFlowSkipCourse")); }
    static FString Folder() { return FPaths::ProjectSavedDir()/(Repeat() ? TEXT("Tests/FullFlow/Repeat") : TEXT("Tests/FullFlow")); }
    static void Log(const TSharedPtr<FState>& S,const FString& Message)
    {
        S->Log+=Message+TEXT("\n");
        UE_LOG(LogTemp,Display,TEXT("FULL_FLOW %s"),*Message);
        FFileHelper::SaveStringToFile(S->Log,*(Folder()/TEXT("progress.txt")));
    }
    static void Check(const TSharedPtr<FState>& S,bool OK,const FString& Message)
    { S->OK &= OK;Log(S,(OK ? TEXT("PASS ") : TEXT("FAIL "))+Message); }
    static void Step(const TSharedPtr<FState>& S,int32 Next)
    { S->Step=Next;S->At=FPlatformTime::Seconds();S->Holding=false;S->ResultChecked=false;Log(S,FString::Printf(TEXT("step=%d"),Next)); }
    static void Key(AOBPlayerController* PC,FKey K,bool Pressed)
    { PC->InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),K,Pressed ? IE_Pressed : IE_Released,FPlatformTime::Cycles64())); }
    static void Tap(AOBPlayerController* PC)
    { Key(PC,EKeys::LeftMouseButton,true);Key(PC,EKeys::LeftMouseButton,false); }
    static void Capture(const TCHAR* Name)
    { FScreenshotRequest::RequestScreenshot(Folder()/Name,true,false); }
    static void Reset(AOBPlayerController* PC,const TSharedPtr<FState>& S)
    {
        S->Previous=PC->GetWorld();
        auto* V=PC->GetLocalPlayer() ? PC->GetLocalPlayer()->ViewportClient.Get() : nullptr;
        Check(S,Cast<UOBGameViewportClient>(V)!=nullptr,TEXT("Actual viewport owns F5"));
        if (V) V->InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),EKeys::F5,IE_Pressed,FPlatformTime::Cycles64()));
    }
    static bool TitleReady(UOBTitleWidget* T)
    { return T && T->Clips.Num()==3 && T->ActiveClip!=INDEX_NONE && !T->bEntrancePlaying && T->ClipStates[2]==UOBTitleWidget::EClipState::Ready; }
    static void StartTitle(UOBTitleWidget* T,const TSharedPtr<FState>& S)
    {
        FSlateApplication::Get().SetAllUserFocusToGameViewport();
        FPointerEvent Click(0,FVector2D(960,540),FVector2D(960,540),TSet<FKey>{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState());
        T->NativeOnPreviewMouseButtonDown(FGeometry(),Click);
        Check(S,T->bStartRequested && T->bEntrancePlaying,TEXT("Title mouse trigger starts entrance and narration"));
        S->TitleCaptured=false;S->OldPlayers.Reset();
        for (UOBUIVideo* Clip:T->Clips) S->OldPlayers.Add(Clip->GetPlayer());
    }
    static void CheckMain(UWorld* W,AOBPlayerController* PC,const TSharedPtr<FState>& S,const TCHAR* Context)
    {
        auto* H=PC->GetOBHUD();auto* T=H ? H->GetTitleWidget() : nullptr;
        Check(S,W!=S->Previous.Get() && !UGameplayStatics::IsGamePaused(W) && H && H->IsMenuVisible()
            && T && !T->bStartRequested && !PC->IsTriggerHeld(),Context);
    }
    static void Shoot(AOBPlayerController* PC,AOBPlayerCharacter* P,AOBEnemyCharacter* Target,const TSharedPtr<FState>& S)
    {
        UBoxComponent* Head=nullptr;TInlineComponentArray<UBoxComponent*> Boxes(Target);
        for (auto* B:Boxes) if (B->GetFName()==TEXT("HeadHitBox")) Head=B;
        if (!Head) { Check(S,false,TEXT("Live target has a head hitbox"));return; }
        const FVector Start=P->GetPlayerCamera()->GetComponentLocation(), Aim=Head->GetComponentLocation();
        FHitResult Resolved;bool Fired=false;AOBEnemyCharacter* Victim=nullptr;float Before=0;
        const int32 Zone=P->GetStageFlowComponent()->GetCurrentZone();
        const auto Handle=PC->OnShotResolved.AddLambda([&](const FHitResult& Hit) {
            Resolved=Hit;Fired=true;Victim=Cast<AOBEnemyCharacter>(Hit.GetActor());Before=Victim ? Victim->GetCurrentHealth() : 0;
        });
        PC->SetCrouchMode(P->bIsCrouched);
        PC->FireAlongRay(Start,Aim+(Aim-Start).GetSafeNormal()*50.f);
        PC->OnShotResolved.Remove(Handle);
        if (Fired)
        {
            ++(S->FailureRun ? S->FailShots : S->ClearShots);
            if (Victim && Victim->IsEncounterCombatant() && Before>0)
            {
                ++(S->FailureRun ? S->FailHits : S->ClearHits);
                const auto* Box=Resolved.GetComponent();
                const double Weight=Box && Box->ComponentHasTag(TEXT("HitZone.Head")) ? 1. : Box && Box->ComponentHasTag(TEXT("HitZone.Body")) ? .75 : .35;
                double& Assessment=S->ExpectedDamage.FindOrAdd(Victim);
                Assessment+=FMath::Max(0.f,Before-Victim->GetCurrentHealth())/Victim->GetMaxHealth()*Weight;
                if (Victim->GetCurrentHealth()<=0) (S->FailureRun ? S->FailKillAssessment : S->ClearKillAssessment).FindOrAdd(Zone)+=Assessment;
            }
        }
        Log(S,FString::Printf(TEXT("shot intended=%s actual=%s box=%s health=%.1f fired=%d"),*Target->GetName(),*GetNameSafe(Resolved.GetActor()),*GetNameSafe(Resolved.GetComponent()),Target->GetCurrentHealth(),Fired));
        if (Fired) Check(S,Resolved.GetActor()==Target,TEXT("Production camera ray hits the intended live actor"));
    }
    static void Course(AOBTrainingDirector* D,AOBPlayerController* PC,AOBPlayerCharacter* P,const TSharedPtr<FState>& S,float Delta)
    {
        if (!D->IsReadyForQA()) return;
        D->Result.bUsedTestInputs=true;D->bQASession=true; // isolate records; do not alter exercise outcomes.
        if (S->LastTrainingStep!=int32(D->CurrentStep) || S->LastDialogue!=D->CurrentDialogueId)
        {
            S->LastTrainingStep=int32(D->CurrentStep);S->LastDialogue=D->CurrentDialogueId;
            Log(S,FString::Printf(TEXT("tutorial step=%s dialogue=%d route=%d"),*StaticEnum<EOBTrainingStep>()->GetNameStringByValue(int64(D->CurrentStep)),D->CurrentDialogueId,D->PrototypeRouteIndex));
        }
        // The input driver supplies a stable synthetic aim instead of an optical controller.
        // Keep the real hold duration and all guide/media/route timing.
        D->SetActorTickEnabled(D->CurrentStep!=EOBTrainingStep::AimHold);
        if (D->CurrentStep==EOBTrainingStep::Welcome && D->bGuideAdvanceReady) { Tap(PC);return; }
        if (!D->AcceptsInput(D->CurrentStep)) return;
        if (D->CurrentStep==EOBTrainingStep::WeaponCheck) { Tap(PC);return; }
        if (D->CurrentStep==EOBTrainingStep::AimHold)
        { D->UpdateAimHold(Delta,D->GetAimTargetPosition()*FVector2D(1280,720),FVector2D(1280,720));return; }
        if (D->CurrentStep==EOBTrainingStep::Cover) { P->Crouch();return; }
        if (D->CurrentStep==EOBTrainingStep::Reload) { PC->CompleteReload();return; }
        if (D->CurrentStep==EOBTrainingStep::Breach) { D->NotifyBreachState(false);D->NotifyBreachState(true);return; }
        if (D->CurrentStep!=EOBTrainingStep::Accuracy && D->CurrentStep!=EOBTrainingStep::TimedTargets) return;
        if (FPlatformTime::Seconds()-S->LastShot<.35) return;
        if (PC->GetCurrentAmmo()==0) { PC->CompleteReload();return; }
        auto* Target=D->CurrentStep==EOBTrainingStep::Accuracy ? D->AccuracyTarget.Get() : D->GetActiveTimedTarget();
        if (!Target && D->CurrentStep==EOBTrainingStep::TimedTargets)
            for (AOBTrainingTarget* Candidate:D->TimedTargets)
                if (D->IsPrototypeGroupTarget(Candidate) && !D->HitTargets.Contains(Candidate) && Candidate->IsTargetRaised()) { Target=Candidate;break; }
        if (!Target) return;
        const FVector Start=PC->PlayerCameraManager->GetCameraLocation(),Aim=Target->BoardMesh->Bounds.Origin;
        PC->SetCrouchMode(P->bIsCrouched);
        PC->FireAlongRay(Start,Aim+(Aim-Start).GetSafeNormal()*100.f);S->LastShot=FPlatformTime::Seconds();
    }
    static void StageRun(UWorld* W,AOBPlayerController* PC,AOBPlayerCharacter* P,const TSharedPtr<FState>& S,bool Fail)
    {
        auto* F=P->GetStageFlowComponent();if (!F->IsStageActive()) return;
        S->FailureRun=Fail;
        F->StageRecord.bUsedTestInputs=true;
        const int32 Phase=int32(F->GetPhase());
        if (Phase!=S->LastPhase || F->GetCurrentZone()!=S->LastZone)
        {
            Log(S,FString::Printf(TEXT("stage case=%s phase=%s zone=%d remaining=%.2f hearts=%d"),Fail ? TEXT("hostage_failure") : TEXT("clear"),*StaticEnum<EOBStagePhase>()->GetNameStringByValue(Phase),F->GetCurrentZone(),F->GetRemainingSeconds(),F->GetRemainingLives()));
            S->LastPhase=Phase;S->LastZone=F->GetCurrentZone();
        }
        S->SawIntro |= F->GetPhase()==EOBStagePhase::Zone6Intro;
        S->SawEnding |= F->GetPhase()==EOBStagePhase::Ending;
        if (F->IsWaitingForBreach())
        { S->SawBreach=F->AttachBreachChargeForQA(); }
        if (F->GetPhase()!=EOBStagePhase::Combat || !PC->IsActionInputAllowed() || FPlatformTime::Seconds()-S->LastShot<.18) return;
        (Fail ? S->FailZones : S->ClearZones).Add(F->GetCurrentZone());
        AOBEnemyCharacter* Target=nullptr;
        if (Fail && F->GetCurrentZone()==6)
        {
            for (TActorIterator<AOBHostageCharacter> It(W);It;++It) if (It->GetCurrentHealth()>0) { Target=*It;break; }
        }
        else
            for (AOBEnemyCharacter* E:P->GetActionComponent()->GetEncounterEnemies()) if (IsValid(E) && E->GetCurrentHealth()>0) { Target=E;break; }
        if (Target) { Shoot(PC,P,Target,S);S->LastShot=FPlatformTime::Seconds(); }
    }
    static void VerifyResult(UOBStageFlowComponent* F,UOBTrainingResultWidget* R,const TSharedPtr<FState>& S,bool Success)
    {
        const auto& Data=F->GetStageRecord();double Sum=0;
        Check(S,Data.bFinished && Data.bHasStageScore && F->DidSucceed()==Success,TEXT("Natural ending finalizes the expected rescue outcome"));
        Check(S,(Success ? S->ClearZones : S->FailZones).Num()==6 && Data.StageZones.Num()==6,TEXT("All six actual combat zones were traversed without teleporting"));
        for (const auto& Z:Data.StageZones)
        {
            double Expected=0;
            const double Kill=(Success ? S->ClearKillAssessment : S->FailKillAssessment).FindRef(Z.Zone)/FMath::Max(1,Z.PlannedEnemies)*70.;
            if (Z.Zone<6) Expected=Kill+(Z.ElapsedSeconds<=25 ? 20 : Z.ElapsedSeconds<=30 ? 15 : Z.ElapsedSeconds<=34 ? 10 : 5)+Z.RemainingHearts*2.;
            else if (Success) Expected=Kill+(Z.RemainingSeconds>7 ? 30 : Z.RemainingSeconds>5 ? 22.5 : Z.RemainingSeconds>2 ? 15 : 7.5);
            Check(S,Z.bFinalized && FMath::IsNearlyEqual(Z.Score,Expected,.01),FString::Printf(TEXT("Zone %d score %.4f equals independently observed hit HP/time/hearts expectation %.4f (kills %d/%d)"),Z.Zone,Z.Score,Expected,Z.PlayerKills,Z.PlannedEnemies));
            if (Z.Zone<6 || Success) Check(S,Z.PlayerKills==Z.PlannedEnemies && Z.SupportReason.IsEmpty(),TEXT("Every combatant was credited through production camera damage"));
            Sum+=Z.Score;
        }
        const double Score=Sum/6.;const FString Rank=Score>=90 ? TEXT("S") : Score>=80 ? TEXT("A") : Score>=70 ? TEXT("B") : Score>=60 ? TEXT("C") : TEXT("D");
        Check(S,Data.ShotsFired==(Success ? S->ClearShots : S->FailShots) && Data.ValidHits==(Success ? S->ClearHits : S->FailHits),TEXT("Shot count and accuracy include exactly the observed camera traces"));
        Check(S,FMath::IsNearlyEqual(Data.StageScore,Score,1.e-8) && Data.StageRank==Rank,TEXT("Score and rank equal the actual six-zone average"));
        auto* ScoreText=Cast<UTextBlock>(R->GetWidgetFromName(TEXT("StageScoreText")));auto* RankText=Cast<UTextBlock>(R->GetWidgetFromName(TEXT("StageRankText")));
        Check(S,ScoreText && ScoreText->GetText().ToString()==FString::Printf(TEXT("%.2f / 100"),Score) && RankText && RankText->GetText().ToString()==Rank,TEXT("Displayed score and rank match the recorded values"));
        FString Text;TSharedPtr<FJsonObject> Json;
        const FString File=FPaths::ProjectSavedDir()/TEXT("Tests/StageResults/Records")/(Data.SessionId.ToString(EGuidFormats::Digits)+TEXT(".json"));
        const bool Saved=FFileHelper::LoadFileToString(Text,*File) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Json);
        Check(S,Saved && Json->GetNumberField(TEXT("score"))==Data.StageScore && Json->GetStringField(TEXT("rank"))==Rank && Json->GetArrayField(TEXT("zones")).Num()==6,TEXT("Saved JSON agrees with the actual result UI and all six zones"));
        Check(S,S->SawEnding && S->SawBreach && S->SawIntro,TEXT("Real opening, breach, rescue intro and ending gates were retained"));
        Log(S,FString::Printf(TEXT("result case=%s score=%.4f rank=%s shots=%d hits=%d session=%s"),Success ? TEXT("clear") : TEXT("failure"),Score,*Rank,Data.ShotsFired,Data.ValidHits,*Data.SessionId.ToString()));
        S->CompletedSession=Data.SessionId;
    }
    static void Finish(const TSharedPtr<FState>& S)
    {
        Log(S,S->OK ? TEXT("outcome=PASS") : TEXT("outcome=FAIL"));
        FFileHelper::SaveStringToFile(S->Log,*(Folder()/TEXT("full-flow.txt")));
        if (GEditor && GEditor->PlayWorld) GEditor->RequestEndPlayMap();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float) { FPlatformMisc::RequestExitWithStatus(false,S->OK ? 0 : 1);return false; }),1.f);
    }
public:
    static void Start()
    {
        if (!GEditor) return;
        IFileManager::Get().MakeDirectory(*Folder(),true);
        FRequestPlaySessionParams Params;Params.GlobalMapOverride=TEXT("/Game/Maps/TitleMap");Params.bAllowOnlineSubsystem=false;
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();GEditor->RequestPlaySession(Params);
        auto S=MakeShared<FState>();Log(S,TEXT("Automatic mouse/keyboard/aim/breach inputs; saved production maps and normal time. No physical controller qualification."));
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float) {
            const double Now=FPlatformTime::Seconds();const float Delta=float(FMath::Min(.2,Now-S->LastTick));S->LastTick=Now;
            if (Now-S->Started>1200 || Now-S->At>330) { Check(S,false,FString::Printf(TEXT("Flow deadline at step %d map %s"),S->Step,*S->LastMap));Finish(S);return false; }
            auto* W=GEditor ? GEditor->PlayWorld.Get() : nullptr;auto* PC=W ? Cast<AOBPlayerController>(W->GetFirstPlayerController()) : nullptr;
            if (!PC || W->GetTimeSeconds()<.2) return true;
            auto* H=PC->GetOBHUD();auto* O=H ? H->GetPlayerOverlayWidget() : nullptr;auto* R=O ? O->GetTrainingResultWidget() : nullptr;
            auto* P=Cast<AOBPlayerCharacter>(PC->GetPawn());auto* F=P ? P->GetStageFlowComponent() : nullptr;
            const FString Map=UGameplayStatics::GetCurrentLevelName(W,true);
            if (Map!=S->LastMap) { Log(S,TEXT("map=")+Map);S->LastMap=Map;S->LastPhase=-1;S->LastZone=0;FSlateApplication::Get().SetAllUserFocusToGameViewport(); }
            auto* T=H ? H->GetTitleWidget() : nullptr;
            if (Map==TEXT("TitleMap") && T && T->bEntrancePlaying && !S->TitleCaptured && T->Video && T->Video->GetPlayer()->GetTime().GetTotalSeconds()>.12)
            { Capture(TEXT("title-noise.png"));S->TitleCaptured=true; }
            if ((S->Step==0 || S->Step==5 || S->Step==9 || S->Step==11) && Map==TEXT("TitleMap") && TitleReady(T))
            {
                if (S->Step==5) CheckMain(W,PC,S,TEXT("F5 during restarted opening returns to fresh usable title"));
                if (S->Step==9) CheckMain(W,PC,S,TEXT("Two-second result hold returns to fresh usable title"));
                if (S->Step==11) CheckMain(W,PC,S,TEXT("F5 during TutorialMap cancels training and returns to title"));
                const int32 Next=S->Step==0 ? 1 : S->Step==5 ? 6 : S->Step==9 ? 10 : 12;
                StartTitle(T,S);Step(S,Next);return true;
            }
            if (Map==TEXT("TutorialMap") && (S->Step==1 || S->Step==6 || S->Step==10 || S->Step==12))
            {
                AOBTrainingDirector* D=nullptr;for (TActorIterator<AOBTrainingDirector> It(W);It;++It) { D=*It;break; }
                if (!D || !P || !D->IsReadyForQA()) return true;
                if (!S->TutorialRecorded)
                {
                    Check(S,S->TitleCaptured,TEXT("Entrance movie rendered before TutorialMap travel"));
                    for (auto Old:S->OldPlayers) Check(S,!Old.IsValid() || Old->IsClosed(),TEXT("Title media closes when the map changes"));
                    S->TutorialRecorded=true;Capture(TEXT("tutorial-intro.png"));
                }
                if (S->Step==1)
                {
                    if (Repeat())
                    {
                        D->Result.bUsedTestInputs=true;D->bQASession=true;
                        if (D->IsInitialSkipAvailable())
                        { Check(S,D->RequestInitialSkip(),TEXT("Repeated score/navigation run skips the already verified complete course"));Step(S,2); }
                        return true;
                    }
                    Course(D,PC,P,S,Delta);
                    if (D->Result.bFinished && O && O->IsTrainingResultVisible() && Now-S->LastShot>1)
                    {
                        if (!S->ResultChecked) { Check(S,D->Result.IsScenarioPassed() && !D->Result.bAborted,TEXT("Complete TutorialMap course passes using actual training targets and door"));Capture(TEXT("tutorial-result.png"));S->ResultChecked=true;S->At=Now; }
                        if (Now-S->At>1) { Tap(PC);Step(S,2); }
                    }
                }
                else if (D->IsInitialSkipAvailable())
                {
                    D->Result.bUsedTestInputs=true;D->bQASession=true;
                    if (S->Step==10) { Reset(PC,S);Step(S,11);S->TutorialRecorded=false; }
                    else { Check(S,D->RequestInitialSkip(),TEXT("Authored TutorialMap skip enters Stage_01 for repeated navigation cases"));Step(S,S->Step==6 ? 7 : 13); }
                }
                return true;
            }
            if (Map==TEXT("Stage_01") && F && (S->Step==2 || S->Step==7 || S->Step==13))
            {
                if (S->Step==13)
                {
                    F->StageRecord.bUsedTestInputs=true;
                    if (F->GetPhase()==EOBStagePhase::Combat && F->GetCurrentZone()==1)
                    { Check(S,PC->SetPause(true),TEXT("Actual first combat can be paused"));Reset(PC,S);Step(S,14); }
                    return true;
                }
                const bool Fail=S->Step==7;StageRun(W,PC,P,S,Fail);
                if (F->GetPhase()==EOBStagePhase::Complete && O && O->IsTrainingResultVisible() && R)
                { VerifyResult(F,R,S,!Fail);Capture(Fail ? TEXT("stage-failure-result.png") : TEXT("stage-clear-result.png"));Step(S,Fail ? 8 : 3); }
                return true;
            }
            if (S->Step==3 && Map==TEXT("Stage_01") && Now-S->At>1)
            { S->Previous=W;Tap(PC);Step(S,4);return true; }
            if (S->Step==4 && W!=S->Previous.Get() && Map==TEXT("Stage_01") && F)
            {
                const auto& D=F->GetStageRecord();
                Check(S,F->GetPhase()==EOBStagePhase::Opening && F->GetCurrentZone()==0 && D.ShotsFired==0 && D.ValidHits==0 && D.SessionId!=S->CompletedSession && !D.bFinished,TEXT("Result tap restarts real opening with a new session and zero score counters"));
                F->StageRecord.bUsedTestInputs=true;Reset(PC,S);Step(S,5);S->TutorialRecorded=false;S->SawEnding=S->SawBreach=S->SawIntro=false;return true;
            }
            if (S->Step==8 && Map==TEXT("Stage_01") && R && Now-S->At>1)
            {
                if (!S->Holding) { FSlateApplication::Get().SetAllUserFocusToGameViewport();S->Previous=W;Key(PC,EKeys::LeftMouseButton,true);S->Holding=true; }
                // NativeTick advances the actual two-second hold in normal real time.
                if (Now-S->At>8) { Check(S,false,TEXT("Result hold navigates within its real two-second gesture"));Finish(S);return false; }
                return true;
            }
            if (S->Step==8 && Map==TEXT("TitleMap")) { Step(S,9);S->TutorialRecorded=false;return true; }
            if (S->Step==14 && Map==TEXT("TitleMap") && TitleReady(T))
            { CheckMain(W,PC,S,TEXT("F5 while actual combat is paused clears pause, held input and stage state"));Capture(TEXT("reset-title.png"));Finish(S);return false; }
            return true;
        }),.05f);
    }
};
static FAutoConsoleCommand GFullFlowMapSmoke(TEXT("OB.Flow.FullPIE"),TEXT("Disposable complete title/course/six-zone score/retry/home/F5 PIE run with automatic inputs; exits its editor."),FConsoleCommandDelegate::CreateStatic(&FOBFullFlowMapSmoke::Start));
#endif

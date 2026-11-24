#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Editor.h"
#include "LevelEditor.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "UnrealClient.h"
#include "TimerManager.h"
#include "Components/SplineComponent.h"
#include "Components/StateTreeAIComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/DamageType.h"
#include "Core/OBPlayerController.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Character/OBActionComponent.h"
#include "Character/OBEnemyCharacter.h"
#include "Character/OBAllyCharacter.h"
#include "Character/OBHostageCharacter.h"
#include "Stage/OBStageFlowComponent.h"
#include "Stage/OBStageCinematicWidget.h"
#include "World/OBRailPath.h"
#include "World/OBEncounterTrigger.h"
#include "World/OBEncounterSpawner.h"
#include "AI/OBAIController.h"
#include "Widget/OBHUD.h"
#include "Widget/OBPlayerOverlayWidget.h"
#include "Widget/OBDamageScreenWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "MediaPlayer.h"

class FOBStageFlowMapSmoke
{
public:
    static void DamagePreview()
    {
        if (!GEditor || GEditor->PlayWorld) return;
        AudioPreview({TEXT("3")});
        struct FDamagePreview { int32 Step=0;double Start=0,Deadline=0;bool OK=true,bSawHold=false;FString Log; };
        auto S=MakeShared<FDamagePreview>();S->Deadline=FPlatformTime::Seconds()+120;
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float) {
            auto* W=GEditor ? GEditor->PlayWorld.Get() : nullptr;
            auto* PC=W ? Cast<AOBPlayerController>(W->GetFirstPlayerController()) : nullptr;
            auto* P=PC ? Cast<AOBPlayerCharacter>(PC->GetPawn()) : nullptr;
            auto* F=P ? P->GetStageFlowComponent() : nullptr;
            auto Check=[&](bool OK,const TCHAR* Label) { S->OK &= OK;S->Log+=FString::Printf(TEXT("%s %s\n"),OK ? TEXT("PASS") : TEXT("FAIL"),Label); };
            auto Capture=[&](const TCHAR* Name) { FScreenshotRequest::RequestScreenshot(FPaths::ProjectDir()/TEXT("Saved/Tests/DamageFX")/Name,true,false); };
            auto Finish=[&]() { S->Log+=FString::Printf(TEXT("outcome=%s\n"),S->OK ? TEXT("PASS") : TEXT("FAIL"));FFileHelper::SaveStringToFile(S->Log,*(FPaths::ProjectDir()/TEXT("Saved/Tests/DamageFX/pie-verification.txt")));UE_LOG(LogTemp,Display,TEXT("STAGE_DAMAGE_PREVIEW\n%s"),*S->Log); };
            if (FPlatformTime::Seconds()>S->Deadline) { Check(false,TEXT("Preview deadline"));Finish();return false; }
            if (!F || !F->DamageScreen) return true;
            auto* FX=F->DamageScreen.Get();const double Now=W->GetTimeSeconds();
            if (S->Step==0)
            {
                if (F->CurrentZone!=3 || F->Phase!=EOBStagePhase::Combat || Now<2.f) return true;
                P->GetActionComponent()->bAutoAdvanceOnEnemyClear=true;F->StageRecord.bUsedTestInputs=true;
                Check(PC->GetViewTarget()==P,TEXT("Native first-person player remains the view target"));
                Check(F->ApplyPlayerHit(1),TEXT("A real Stage life decrement triggers the effect"));S->Start=Now;S->Step=1;
            }
            else if (S->Step==1 && Now-S->Start>=.1) { Check(FX->IsHitActive(),TEXT("Fracture appears on hit"));Capture(TEXT("01-Hit-Impact.png"));S->Step=2; }
            else if (S->Step==2 && Now-S->Start>=.35) { Capture(TEXT("02-Hit-Fade.png"));S->Step=3; }
            else if (S->Step==3 && Now-S->Start>=.6) { Check(!FX->IsHitActive(),TEXT("Fracture is gone after half a second"));Capture(TEXT("03-Hit-Clear.png"));S->Step=4; }
            else if (S->Step==4 && Now-S->Start>=2.) { F->ApplyPlayerHit(F->RemainingLives);Check(FX->IsInjuredActive(),TEXT("Zero lives starts heartbeat"));S->Start=Now;S->Step=5; }
            else if (S->Step==5 && Now-S->Start>=.1) { Capture(TEXT("04-Injured-Beat.png"));S->Step=6; }
            else if (S->Step==6 && Now-S->Start>=.5) { Capture(TEXT("05-Injured-Rest.png"));S->Step=7; }
            else if (S->Step==7)
            {
                if (F->Phase==EOBStagePhase::SupportingFire || F->Phase==EOBStagePhase::AwaitingDeparture)
                {
                    S->OK &= FX->IsInjuredActive();
                    if (F->Phase==EOBStagePhase::AwaitingDeparture && !S->bSawHold) { S->bSawHold=true;Capture(TEXT("06-Injured-DepartureHold.png")); }
                }
                else if (F->Phase==EOBStagePhase::Travel)
                {
                    Check(S->bSawHold,TEXT("Injured stays visible through the departure narration"));
                    Check(!FX->IsInjuredActive(),TEXT("Heartbeat turns off before the next-zone movement"));
                    Check(PC->GetViewTarget()==P,TEXT("First-person view is preserved after support"));
                    Capture(TEXT("07-Departure-Clear.png"));Finish();return false;
                }
            }
            return true;
        }),.02f);
    }
    static void AudioPreview(const TArray<FString>& Args)
    {
        if (!GEditor || GEditor->PlayWorld) return;
        const int32 Zone=FMath::Clamp(Args.IsEmpty() ? 1 : FCString::Atoi(*Args[0]),1,6);
        FRequestPlaySessionParams Params;Params.GlobalMapOverride=TEXT("/Game/Maps/Stage_01");Params.bAllowOnlineSubsystem=false;
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        GEditor->RequestPlaySession(Params);
        const double Begin=FPlatformTime::Seconds();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Begin,Zone](float) {
            auto* W=GEditor ? GEditor->PlayWorld.Get() : nullptr;
            auto* PC=W ? Cast<AOBPlayerController>(W->GetFirstPlayerController()) : nullptr;
            auto* P=PC ? Cast<AOBPlayerCharacter>(PC->GetPawn()) : nullptr;
            if (P && W->GetTimeSeconds()>.5f && P->GetPlayerSplineComponent()->GetRailPath())
            {
                const int32 Points[]={7,19,44,64,72,86};
                P->SetCanBeDamaged(false);
                P->GetActionComponent()->bAutoAdvanceOnEnemyClear=false;
                StartZone(W,P,Points[Zone-1]);
                auto* Flow=P->GetStageFlowComponent();
                Flow->ClearPresentation();Flow->Phase=EOBStagePhase::Combat;Flow->RemainingSeconds=3600.f;
                UE_LOG(LogTemp,Display,TEXT("STAGE_AUDIO_PREVIEW Zone=%d NativeFirstPerson=1 Invulnerable=1"),Zone);
                return false;
            }
            return FPlatformTime::Seconds()-Begin<100;
        }),.1f);
    }
    static void Start()
    {
        FRequestPlaySessionParams Params;Params.GlobalMapOverride=TEXT("/Game/Maps/Stage_01");Params.bAllowOnlineSubsystem=false;
        if (!FParse::Param(FCommandLine::Get(),TEXT("StageSubtitleCheck")))
            Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        GEditor->RequestPlaySession(Params);
        const double Begin=FPlatformTime::Seconds();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Begin](float) {
            auto* W=GEditor ? GEditor->PlayWorld.Get() : nullptr;
            auto* PC=W ? Cast<AOBPlayerController>(W->GetFirstPlayerController()) : nullptr;
            auto* P=PC ? Cast<AOBPlayerCharacter>(PC->GetPawn()) : nullptr;
            if (P && W->GetTimeSeconds()>.5f && P->GetPlayerSplineComponent()->GetRailPath()) { Run(W,PC,P);return false; }
            if (FPlatformTime::Seconds()-Begin>100) { FPlatformMisc::RequestExitWithStatus(false,1);return false; }
            return true;
        }),.1f);
    }
private:
    static void StartZone(UWorld* W,AOBPlayerCharacter* P,int Point)
    {
        auto* F=P->GetStageFlowComponent();auto* C=P->GetPlayerSplineComponent();
        F->RestoreSlowMotion();F->ClearPresentation();F->RestoreOpeningMovement();F->Phase=EOBStagePhase::Travel;
        C->SetStageMovementBlocked(false);P->GetActionComponent()->ResetEncounterForQA();
        AOBEncounterTrigger* Selected=nullptr;
        for (TActorIterator<AOBEncounterTrigger> It(W);It;++It) { It->ResetForQA();if (It->GetPointIndex()==Point) Selected=*It; }
        C->CurrentPointIndex=Point;C->NextSplineCuePoint=Point+1;C->TargetPointIndex=INDEX_NONE;
        C->CurrentDistance=C->TargetDistance=C->GetRailPath()->GetSpline()->GetDistanceAlongSplineAtSplinePoint(Point);
        C->ApplySplineTransform(C->CurrentDistance);
        if (Selected) Selected->ActivateForPlayer(P);
        C->BeginStopAlignment(Point);C->BeginAction();
    }
    static void Run(UWorld* W,AOBPlayerController* PC,AOBPlayerCharacter* P)
    {
        struct FState { int Step=0;double Begin=0,StepTime=0;float MaxOpeningDrift=0,MaxDepartureDrift=0,MaxAllyDepartureDrift=0;FVector OpeningPosition;FString Mode=TEXT("Clear"),Log,Output=TEXT("Build/Testing/Verification/StageFlow");TSet<int32> Captions;TArray<TWeakObjectPtr<AOBEnemyCharacter>> Wave;TMap<TWeakObjectPtr<AOBAllyCharacter>,FVector> AllyPositions;TWeakObjectPtr<ACameraActor> HostagePoseCamera;TWeakObjectPtr<AActor> OriginalViewTarget;TWeakObjectPtr<AOBHostageCharacter> PoseHostage;FTransform HostagePoseTransform;FTimerHandle Timer;bool OK=true,bSawFirstDeparture=false,bHostagePoseCaptured=false; };
        auto S=MakeShared<FState>();FParse::Value(FCommandLine::Get(),TEXT("StageFlowCase="),S->Mode);
        FParse::Value(FCommandLine::Get(),TEXT("StageFlowOutput="),S->Output);
        S->Begin=S->StepTime=W->GetTimeSeconds();S->OpeningPosition=P->GetActorLocation();
        for (TActorIterator<AOBAllyCharacter> It(W);It;++It) S->AllyPositions.Add(*It,It->GetActorLocation());
        P->SetCanBeDamaged(false);
        TWeakObjectPtr<UWorld> Weak=W;
        W->GetTimerManager().SetTimer(S->Timer,FTimerDelegate::CreateLambda([S,Weak]() {
            auto* World=Weak.Get();if (!World) return;
            auto* Controller=Cast<AOBPlayerController>(World->GetFirstPlayerController());
            auto* Player=Controller ? Cast<AOBPlayerCharacter>(Controller->GetPawn()) : nullptr;if (!Player) return;
            auto* F=Player->GetStageFlowComponent();auto* C=Player->GetPlayerSplineComponent();auto* A=Player->GetActionComponent();
            const auto KeepAlive=S;
            auto Check=[&](bool OK,const TCHAR* Message) { S->OK &= OK;S->Log+=FString::Printf(TEXT("%s %s\n"),OK ? TEXT("PASS") : TEXT("FAIL"),Message); };
            auto CaptureWave=[&]() { S->Wave.Reset();for (AOBEnemyCharacter* E : A->GetEncounterEnemies()) S->Wave.Add(E); };
            auto WaveDead=[&]() { for (auto E : S->Wave) if (E.IsValid() && E->GetCombatState()!=EOBEnemyCombatState::Dead) return false;return true; };
            auto Finish=[&]() {
                if (S->HostagePoseCamera.IsValid()) { Controller->SetViewTarget(S->OriginalViewTarget.IsValid() ? S->OriginalViewTarget.Get() : Player);S->HostagePoseCamera->Destroy();S->HostagePoseCamera.Reset(); }
                S->Log+=FString::Printf(TEXT("outcome=%s case=%s\n"),S->OK ? TEXT("PASS") : TEXT("FAIL"),*S->Mode);
                FFileHelper::SaveStringToFile(S->Log,*(FPaths::ProjectDir()/S->Output/(S->Mode+TEXT("-pie.txt"))));
                UE_LOG(LogTemp,Display,TEXT("STAGE_FLOW_PIE %s"),*S->Log);
                const bool OK=S->OK;
                World->GetTimerManager().ClearTimer(S->Timer);
                GEditor->RequestEndPlayMap();
                FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([OK](float) {
                    if (GEditor && GEditor->PlayWorld) return true;
                    FPlatformMisc::RequestExitWithStatus(false,OK ? 0 : 1);return false;
                }),.5f);
            };
            if (World->GetTimeSeconds()-S->Begin>140) { Check(false,TEXT("Scenario timed out"));Finish();return; }
            if (FParse::Param(FCommandLine::Get(),TEXT("StageSubtitleCheck")))
            {
                if (S->Step==99)
                {
                    auto* Overlay=Controller->GetOBHUD()->GetPlayerOverlayWidget();
                    if (F->Phase==EOBStagePhase::Opening)
                    {
                        const auto* Row=F->FindScript(F->OpeningSubtitleId);
                        auto* Caption=Overlay ? Cast<UUserWidget>(Overlay->GetSubtitleWidget()) : nullptr;
                        auto* Text=Caption ? Cast<UTextBlock>(Caption->GetWidgetFromName(TEXT("Script"))) : nullptr;
                        if (Row && Text && Overlay->IsSubtitleActive() && Caption->IsVisible() && Caption->GetRenderOpacity()>.95f
                            && Text->IsVisible() && Text->GetCachedGeometry().GetLocalSize().Y>0 && Text->GetText().EqualTo(Row->Text))
                            S->Captions.Add(F->OpeningSubtitleId);
                        return;
                    }
                    Check(S->Captions.Num()==4,TEXT("All four real opening captions were laid out with the matching dialogue"));
                    Check(Overlay && !Overlay->IsCinematicSubtitleMode(),TEXT("Opening restores the gameplay HUD"));
                    Finish();return;
                }
                if (World->GetTimeSeconds()-S->Begin<7.f) return;
                auto* Overlay=Controller->GetOBHUD()->GetPlayerOverlayWidget();
                Check(Overlay && Overlay->IsSubtitleActive(),TEXT("Opening has an active caption"));
                Check(Overlay && !Overlay->GetSubtitleText().IsEmpty(),TEXT("Opening caption has dialogue text"));
                S->Log+=FString::Printf(TEXT("opening_id=%d voice_seconds=%.2f mode=%d media=%d\n"),F->OpeningSubtitleId,F->VoiceElapsed,
                    Overlay && Overlay->IsCinematicSubtitleMode(),F->Cinematic && F->Cinematic->GetMediaPlayer() && F->Cinematic->GetMediaPlayer()->IsPlaying());
                auto Dump=[&](UWidget* Widget) {
                    const auto Size=Widget->GetCachedGeometry().GetLocalSize();
                    const auto* Text=Cast<UTextBlock>(Widget);
                    S->Log+=FString::Printf(TEXT("widget=%s visibility=%d opacity=%.2f size=%.1f/%.1f text=%s\n"),*Widget->GetName(),
                        int32(Widget->GetVisibility()),Widget->GetRenderOpacity(),Size.X,Size.Y,Text ? *Text->GetText().ToString() : TEXT(""));
                };
                if (Overlay)
                {
                    Dump(Overlay);Overlay->WidgetTree->ForEachWidget(Dump);
                    if (auto* Child=Cast<UUserWidget>(Overlay->GetSubtitleWidget());Child && Child->WidgetTree) Child->WidgetTree->ForEachWidget(Dump);
                }
                FScreenshotRequest::RequestScreenshot(FPaths::ProjectDir()/S->Output/TEXT("OpeningSubtitle.png"),true,false);
                S->Step=99;S->StepTime=World->GetTimeSeconds();return;
            }
            if (S->Step==0)
            {
                Check(F->bActive,TEXT("Real Stage_01 native flow component is active"));
                if (S->Mode!=TEXT("Clear")) { F->FinishOpening();StartZone(World,Player,85);S->Step=4;S->StepTime=World->GetTimeSeconds();return; }
                Check(F->Phase==EOBStagePhase::Opening,TEXT("Stage starts with the cinematic"));S->Step=1;return;
            }
            if (S->Step==1)
            {
                if (F->Phase==EOBStagePhase::Opening && F->OpeningSubtitleId) S->Captions.Add(F->OpeningSubtitleId);
                if (F->Phase==EOBStagePhase::Opening)
                {
                    if (World->GetTimeSeconds()-S->StepTime>4 && S->StepTime==S->Begin)
                    { FScreenshotRequest::RequestScreenshot(FPaths::ProjectDir()/S->Output/TEXT("Opening.png"),true,false);S->StepTime+=.01; }
                    S->MaxOpeningDrift=FMath::Max(S->MaxOpeningDrift,float(FVector::Distance(Player->GetActorLocation(),S->OpeningPosition)));
                    return;
                }
                if (F->Phase==EOBStagePhase::AwaitingDeparture)
                {
                    if (!S->bSawFirstDeparture)
                    {
                        Check(F->CurrentZone==0 && F->ActiveScriptId==2005,TEXT("First departure order starts after the opening and before travel"));
                        Check(F->IsInputBlocked(),TEXT("First departure narration keeps gameplay input blocked"));
                        S->bSawFirstDeparture=true;
                    }
                    S->MaxDepartureDrift=FMath::Max(S->MaxDepartureDrift,float(FVector::Distance(Player->GetActorLocation(),S->OpeningPosition)));
                    for (const auto& Ally : S->AllyPositions) if (Ally.Key.IsValid())
                        S->MaxAllyDepartureDrift=FMath::Max(S->MaxAllyDepartureDrift,float(FVector::Distance(Ally.Key->GetActorLocation(),Ally.Value)));
                    return;
                }
                Check(S->MaxOpeningDrift<=1.f,TEXT("Opening freezes character physics as well as spline travel"));
                Check(S->Captions.Num()==4,TEXT("All four briefing subtitles were driven by voice playback"));
                Check(S->bSawFirstDeparture,TEXT("Opening is followed by the departure narration gate"));
                Check(S->MaxDepartureDrift<=1.f,TEXT("Player stays still until the first departure order finishes"));
                Check(!S->AllyPositions.IsEmpty() && S->MaxAllyDepartureDrift<=1.f,TEXT("Level allies wait for the first departure order too"));
                Check(F->Phase==EOBStagePhase::Travel && !F->bDeparturePending,TEXT("Video briefing and first order all finish before travel starts"));
                StartZone(World,Player,7);CaptureWave();S->Step=2;S->StepTime=World->GetTimeSeconds();return;
            }
            if (S->Step==2)
            {
                bool Ready=true;
                for (auto E : S->Wave) if (E.IsValid())
                { auto* AI=Cast<AOBAIController>(E->GetController());Ready &= AI && AI->HasReachedStagingPoint(); }
                if (!Ready && World->GetTimeSeconds()-S->StepTime<35) return;
                Check(Ready,TEXT("Zone1 real movers reach their staging positions"));
                for (auto E : S->Wave) if (E.IsValid())
                    Check(FMath::Abs(FMath::FindDeltaAngleDegrees(E->GetActorRotation().Yaw,(Player->GetActorLocation()-E->GetActorLocation()).Rotation().Yaw))<3.f,TEXT("Arrived combatant faces the player"));
                F->RemainingSeconds=.05f;S->Step=3;S->StepTime=World->GetTimeSeconds();return;
            }
            if (S->Step==3)
            {
                if (!WaveDead() || F->Phase==EOBStagePhase::SupportingFire || F->Phase==EOBStagePhase::AwaitingDeparture) return;
                Check(F->Phase==EOBStagePhase::Travel,TEXT("Zone1 timeout support kills the full wave and advances"));
                Check(F->LastSupportScript>=2019 && F->LastSupportScript<=2021,TEXT("Support selects a worksheet row 23-25"));
                StartZone(World,Player,19);CaptureWave();Player->SetCanBeDamaged(true);
                FDamageEvent Damage;for (int I=0;I<5;++I) Player->TakeDamage(20.f,Damage,nullptr,nullptr);
                Player->SetCanBeDamaged(false);
                Check(F->Phase==EOBStagePhase::SupportingFire && F->RemainingLives==0,TEXT("Life exhaustion starts squad support"));
                S->Step=31;return;
            }
            if (S->Step==31)
            {
                if (!WaveDead() || F->Phase==EOBStagePhase::SupportingFire || F->Phase==EOBStagePhase::AwaitingDeparture) return;
                Check(F->Phase==EOBStagePhase::Travel,TEXT("Zone2 life exhaustion support clears and advances"));
                StartZone(World,Player,85);S->Step=4;S->StepTime=World->GetTimeSeconds();return;
            }
            if (S->Step==4)
            {
                Check(F->Phase==EOBStagePhase::Combat && F->CurrentZone==6,TEXT("Zone6 uses a separate combat phase"));
                if (!S->bHostagePoseCaptured) Check(F->RemainingSeconds>28.f && F->RemainingSeconds<=30.f,TEXT("Final deadline starts at 30 real game seconds"));
                Check(World->GetWorldSettings()->GetEffectiveTimeDilation()==1.f && Player->CustomTimeDilation==1.f,TEXT("World and player speed remain normal"));
                for (AOBEnemyCharacter* E : A->GetEncounterEnemies()) Check(FMath::IsNearlyEqual(E->CustomTimeDilation,.35f),TEXT("Final-zone enemies use local slow motion"));
                Check(A->GetEncounterEnemies().Num()==4,TEXT("Only four captors count as targets"));
                if (S->Mode==TEXT("Timeout")) F->RemainingSeconds=.01f;
                else if (S->Mode==TEXT("Hostage"))
                {
                    AOBHostageCharacter* Hostage=nullptr;int32 HostageCount=0;TSet<FString> HostageLabels;
                    for (TActorIterator<AOBEncounterSpawner> It(World);It;++It)
                    {
                        if (It->GetPreparedPointIndex()!=85 || It->bParticipatesInCombat) continue;
                        auto* Actor=It->GetSpawnedEnemy();auto* Candidate=Cast<AOBHostageCharacter>(Actor);
                        HostageLabels.Add(It->GetActorLabel());
                        ++HostageCount;
                        Check(Candidate!=nullptr,TEXT("Final noncombat spawner uses the dedicated hostage class"));
                        if (!Candidate) continue;
                        Check(Candidate->GetWeaponMesh()==nullptr && Candidate->GetController()==nullptr,TEXT("Dedicated hostage has neither a weapon nor an AI controller"));
                        Check(!Candidate->IsEncounterCombatant() && !A->GetEncounterEnemies().Contains(Candidate),TEXT("Dedicated hostage remains outside the combat target list"));
                        if (!Hostage) Hostage=Candidate;
                    }
                    Check(HostageCount==2 && Hostage!=nullptr,TEXT("Both real final-zone noncombat hostages are present"));
                    Check(HostageLabels.Num()==2 && HostageLabels.Contains(TEXT("Zone6_Spawner_Hostage_01")) && HostageLabels.Contains(TEXT("Zone6_Spawner_Hostage_02")),TEXT("Reloaded map preserves the two explicit hostage spawner labels"));
                    if (!Hostage) { Finish();return; }
                    if (!S->bHostagePoseCaptured)
                    {
                        Hostage->GetMesh()->RefreshBoneTransforms();
                        const FVector Target=Hostage->GetMesh()->DoesSocketExist(TEXT("spine_02"))
                            ? Hostage->GetMesh()->GetSocketLocation(TEXT("spine_02")) : Hostage->GetActorLocation()+FVector(0,0,30);
                        FCollisionQueryParams CameraQuery(SCENE_QUERY_STAT(OBHostagePoseCamera),true);
                        for (TActorIterator<APawn> It(World);It;++It) CameraQuery.AddIgnoredActor(*It);
                        FVector Location=FVector::ZeroVector;bool bClearView=false;int32 CandidateCount=0;
                        // A clear Camera channel alone is insufficient for meshes that ignore camera collision.
                        // Inspect both directions on Visibility so a one-sided wall cannot hide the hostage.
                        for (float Radius : {180.f,240.f,120.f})
                        {
                            for (float Height : {30.f,70.f})
                            {
                                for (int32 Direction=0;Direction<8;++Direction)
                                {
                                    ++CandidateCount;
                                    const FVector Offset=FRotator(0,Hostage->GetActorRotation().Yaw+45.f*Direction,0).Vector()*Radius;
                                    const FVector Candidate=Target+Offset+FVector(0,0,Height);
                                    if (World->OverlapBlockingTestByChannel(Candidate,FQuat::Identity,ECC_Camera,FCollisionShape::MakeSphere(12.f),CameraQuery)
                                        || World->OverlapBlockingTestByChannel(Candidate,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(12.f),CameraQuery)) continue;
                                    bool bObstructed=false;
                                    for (float BodyOffset : {0.f,40.f,-30.f})
                                    {
                                        const FVector BodyTarget=Target+FVector(0,0,BodyOffset);FHitResult Obstacle;
                                        if (World->LineTraceSingleByChannel(Obstacle,Candidate,BodyTarget,ECC_Visibility,CameraQuery)
                                            || World->LineTraceSingleByChannel(Obstacle,BodyTarget,Candidate,ECC_Visibility,CameraQuery))
                                        { bObstructed=true;break; }
                                    }
                                    if (!bObstructed) { Location=Candidate;bClearView=true;break; }
                                }
                                if (bClearView) break;
                            }
                            if (bClearView) break;
                        }
                        Check(bClearView,TEXT("Pose inspection has a collision-free camera and unobstructed Visibility lines to the hostage body"));
                        S->Log+=FString::Printf(TEXT("INFO HostagePoseCamera Candidates=%d ClearView=%d Location=%s Focus=%s\n"),CandidateCount,bClearView,*Location.ToCompactString(),*Target.ToCompactString());
                        if (!bClearView) { Check(false,TEXT("Hostage pose could not be observed without wall occlusion"));Finish();return; }
                        auto* Camera=World->SpawnActor<ACameraActor>(Location,(Target-Location).Rotation());
                        Check(Camera!=nullptr,TEXT("Temporary pose inspection camera is available"));
                        if (!Camera) { Finish();return; }
                        Camera->GetCameraComponent()->SetFieldOfView(90.f);
                        S->HostagePoseCamera=Camera;S->OriginalViewTarget=Controller->GetViewTarget();
                        S->PoseHostage=Hostage;S->HostagePoseTransform=Hostage->GetActorTransform();
                        Controller->SetViewTarget(Camera);
                        S->Step=41;S->StepTime=World->GetTimeSeconds();return;
                    }
                    Hostage->GetMesh()->RefreshBoneTransforms();
                    TInlineComponentArray<UBoxComponent*> Boxes;Hostage->GetComponents(Boxes);
                    FHitResult ShotHit;FVector ShotDirection=FVector::ForwardVector;
                    bool bWorldTrace=false,bComponentTrace=false;
                    const FCollisionQueryParams Query(SCENE_QUERY_STAT(OBHostagePointShot),true,Player);
                    // Near-box rays avoid depending on camera FOV or a wall between the player and hostage.
                    // Prefer a real world Visibility hit; isolate the actual component query if the level obstructs it.
                    for (const TCHAR* Name : {TEXT("HeadHitBox"),TEXT("BodyHitBox")})
                    {
                        for (UBoxComponent* Box : Boxes)
                        {
                            if (Box->GetFName()!=FName(Name) || Box->GetCollisionEnabled()==ECollisionEnabled::NoCollision) continue;
                            const FVector Center=Box->GetComponentLocation(),Extent=Box->GetScaledBoxExtent();
                            const FVector Axes[]={Box->GetForwardVector(),Box->GetRightVector(),Box->GetUpVector()};
                            const float Lengths[]={Extent.X+3.f,Extent.Y+3.f,Extent.Z+3.f};
                            for (int32 Axis=0;Axis<3 && !bWorldTrace;++Axis)
                                for (float Sign : {-1.f,1.f})
                                {
                                    const FVector Direction=Axes[Axis]*Sign;
                                    FHitResult Hit;
                                    if (World->LineTraceSingleByChannel(Hit,Center-Direction*Lengths[Axis],Center+Direction*Lengths[Axis],ECC_Visibility,Query)
                                        && Hit.GetActor()==Hostage && Cast<UBoxComponent>(Hit.GetComponent()))
                                    { ShotHit=Hit;ShotDirection=Direction;bWorldTrace=true;break; }
                                }
                            if (!bWorldTrace && !bComponentTrace)
                            {
                                FHitResult Hit;
                                if (Box->LineTraceComponent(Hit,Center-Axes[0]*Lengths[0],Center+Axes[0]*Lengths[0],Query))
                                { ShotHit=Hit;ShotDirection=Axes[0];bComponentTrace=true; }
                            }
                            if (bWorldTrace) break;
                        }
                        if (bWorldTrace) break;
                    }
                    auto* HitBox=Cast<UBoxComponent>(ShotHit.GetComponent());
                    Check((bWorldTrace || bComponentTrace) && HitBox && HitBox->GetOwner()==Hostage,TEXT("A real collision query supplies the hostage body-part UBoxComponent"));
                    S->Log+=FString::Printf(TEXT("INFO HostagePointShot WorldVisibilityTrace=%d ComponentTraceFallback=%d HitBox=%s\n"),bWorldTrace,bComponentTrace && !bWorldTrace,*GetNameSafe(HitBox));
                    if (!HitBox) { Finish();return; }
                    ShotHit.BoneName=HitBox->GetAttachSocketName();
                    const float Applied=UGameplayStatics::ApplyPointDamage(Hostage,1.f,ShotDirection,ShotHit,Controller,Player,UDamageType::StaticClass());
                    Check(Applied>0.f && Hostage->GetLastHitFeedback().HitBoxName==HitBox->GetFName(),TEXT("Production ApplyPointDamage accepts the actual hostage hit box and records hit feedback"));
                    Check(F->Phase==EOBStagePhase::Ending && !F->bSucceeded,TEXT("Player point damage on the dedicated hostage immediately selects the failure ending"));
                }
                else
                {
                    const auto Targets=A->GetEncounterEnemies();
                    for (AOBEnemyCharacter* E : Targets) { FDamageEvent Damage;E->TakeDamage(E->GetMaxHealth()+1.f,Damage,Controller,Player); }
                }
                S->Step=5;S->StepTime=World->GetTimeSeconds();return;
            }
            if (S->Step==41)
            {
                const FString PosePath=FPaths::ProjectDir()/S->Output/TEXT("HostagePose.png");
                if (!S->bHostagePoseCaptured)
                {
                    if (World->GetTimeSeconds()-S->StepTime<1.f) return;
                    IFileManager::Get().MakeDirectory(*FPaths::GetPath(PosePath),true);
                    FScreenshotRequest::RequestScreenshot(PosePath,false,false);
                    S->bHostagePoseCaptured=true;S->StepTime=World->GetTimeSeconds();return;
                }
                // Let a rendered frame consume the screenshot request before restoring the gameplay camera.
                if (World->GetTimeSeconds()-S->StepTime<.2f) return;
                if (IFileManager::Get().FileSize(*PosePath)<=0 && World->GetTimeSeconds()-S->StepTime<3.f) return;
                Check(IFileManager::Get().FileSize(*PosePath)>0,TEXT("Dedicated hostage hands-tied pose was captured before point damage"));
                Check(S->PoseHostage.IsValid() && S->PoseHostage->GetActorTransform().Equals(S->HostagePoseTransform,.1f),TEXT("Pose inspection preserves the hostage's authored transform"));
                Controller->SetViewTarget(S->OriginalViewTarget.IsValid() ? S->OriginalViewTarget.Get() : Player);
                if (S->HostagePoseCamera.IsValid()) { S->HostagePoseCamera->Destroy();S->HostagePoseCamera.Reset(); }
                S->Step=4;return;
            }
            if (S->Step==5)
            {
                if (F->Phase!=EOBStagePhase::Ending && F->Phase!=EOBStagePhase::Complete) return;
                Check(F->bSucceeded==(S->Mode==TEXT("Clear")),TEXT("Outcome matches clear/deadline/hostage-hit condition"));
                if (F->Phase==EOBStagePhase::Ending)
                {
                    auto* HUD=Controller->GetOBHUD();auto* Overlay=HUD ? HUD->GetPlayerOverlayWidget() : nullptr;
                    Check(!Overlay || !Overlay->IsTrainingResultVisible(),TEXT("Training result stays hidden while ending video plays"));
                }
                S->Step=6;return;
            }
            if (S->Step==6 && F->Phase==EOBStagePhase::Complete)
            {
                auto* HUD=Controller->GetOBHUD();auto* Overlay=HUD ? HUD->GetPlayerOverlayWidget() : nullptr;
                Check(Overlay && Overlay->IsTrainingResultVisible(),TEXT("Ending video completes before the training-ended notice"));
                Check(F->OriginalDilations.IsEmpty(),TEXT("Actor slow-motion overrides are restored"));
                FScreenshotRequest::RequestScreenshot(FPaths::ProjectDir()/S->Output/(S->Mode+TEXT("-Result.png")),true,false);
                S->Step=7;return;
            }
            if (S->Step==7) Finish();
        }),.1f,true);
    }
};
static FAutoConsoleCommand GOBStageFlowPIE(TEXT("OB.Stage.FlowPIE"),TEXT("Disposable Stage_01 cinematic/support/hostage verification. Exits this editor. -StageFlowCase=Clear|Timeout|Hostage"),FConsoleCommandDelegate::CreateStatic(&FOBStageFlowMapSmoke::Start));
static FAutoConsoleCommand GOBStageAudioPreview(TEXT("OB.Stage.AudioPreview"),TEXT("Keep Stage_01 open in native first person for listening to live enemy audio. Optional zone 1-6; Esc ends PIE."),FConsoleCommandWithArgsDelegate::CreateStatic(&FOBStageFlowMapSmoke::AudioPreview));
static FAutoConsoleCommand GOBStageDamagePreview(TEXT("OB.Stage.DamagePreview"),TEXT("Preview real life loss, half-second fracture and support heartbeat in native first person. Keeps PIE open."),FConsoleCommandDelegate::CreateStatic(&FOBStageFlowMapSmoke::DamagePreview));
#endif

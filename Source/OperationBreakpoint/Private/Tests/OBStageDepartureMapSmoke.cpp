#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Editor.h"
#include "LevelEditor.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/DamageEvents.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "Components/AudioComponent.h"
#include "Components/SplineComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Character/OBActionComponent.h"
#include "Character/OBEnemyCharacter.h"
#include "Character/OBAllyCharacter.h"
#include "Character/OBAllySplineComponent.h"
#include "Core/OBPlayerController.h"
#include "Stage/OBStageFlowComponent.h"
#include "World/OBEncounterTrigger.h"
#include "World/OBRailPath.h"
#include "World/OBAllyRoute.h"
#include "Widget/OBHUD.h"
#include "Widget/OBPlayerOverlayWidget.h"
#include "Widget/OBHudPanelWidget.h"

// Actual Stage_01 actors and audio, with one disposable ally route to observe the departure event.
// This fixture changes only PIE state; no level or asset package is saved.
class FOBStageDepartureMapSmoke
{
public:
    static void Start()
    {
        FRequestPlaySessionParams Params;Params.GlobalMapOverride=TEXT("/Game/Maps/Stage_01");Params.bAllowOnlineSubsystem=false;
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        GEditor->RequestPlaySession(Params);
        const double Began=FPlatformTime::Seconds();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Began](float) {
            auto* W=GEditor ? GEditor->PlayWorld.Get() : nullptr;
            auto* PC=W ? Cast<AOBPlayerController>(W->GetFirstPlayerController()) : nullptr;
            auto* P=PC ? Cast<AOBPlayerCharacter>(PC->GetPawn()) : nullptr;
            if (P && W->GetTimeSeconds()>.7f && P->GetPlayerSplineComponent()->GetRailPath()) { Run(W,P);return false; }
            if (FPlatformTime::Seconds()-Began>100) { FPlatformMisc::RequestExitWithStatus(false,1);return false; }
            return true;
        }),.1f);
    }
private:
    static bool PanelsVisible(UOBPlayerOverlayWidget* Overlay)
    {
        auto* Timer=Overlay ? Overlay->GetWidgetFromName(TEXT("Timer")) : nullptr;
        auto* Life=Overlay ? Overlay->GetWidgetFromName(TEXT("Life")) : nullptr;
        return Overlay && Overlay->IsVisible() && Timer && Life && Timer->IsVisible() && Life->IsVisible();
    }
    static bool PanelsHidden(UOBPlayerOverlayWidget* Overlay)
    {
        auto* Timer=Overlay ? Overlay->GetWidgetFromName(TEXT("Timer")) : nullptr;
        auto* Life=Overlay ? Overlay->GetWidgetFromName(TEXT("Life")) : nullptr;
        return Timer && Life && !Timer->IsVisible() && !Life->IsVisible();
    }
    static void Run(UWorld* W,AOBPlayerCharacter* P)
    {
        struct FState
        {
            int32 Step=0,TravelCount=0;double Began=0;
            bool OK=true,bSawArrivalAudio=false,bSawDepartureAudio=false,bHoldValid=true,bTravelHUDHidden=true;
            bool bTravelAfterNarration=true;
            float HeldDistance=0,MaxPlayerDrift=0,MaxRailDrift=0,MaxAllyDrift=0;
            FVector HeldPosition;TMap<TWeakObjectPtr<AOBAllyCharacter>,FVector> Allies;
            TWeakObjectPtr<AOBAllyCharacter> StagedAlly;TWeakObjectPtr<AOBAllyRoute> Route;
            TWeakObjectPtr<UOBPlayerSplineComponent> Rail;FDelegateHandle TravelHandle;
            FString Log;FTimerHandle Timer;
        };
        auto S=MakeShared<FState>();S->Began=W->GetTimeSeconds();S->Rail=P->GetPlayerSplineComponent();
        const FString Output=FPaths::ProjectDir()/TEXT("Build/Testing/Verification/StageDepartureCamera");
        IFileManager::Get().MakeDirectory(*Output,true);
        TWeakObjectPtr<UWorld> Weak=W;
        W->GetTimerManager().SetTimer(S->Timer,FTimerDelegate::CreateLambda([S,Weak,Output]() {
            auto* World=Weak.Get();if (!World) return;
            auto* PC=Cast<AOBPlayerController>(World->GetFirstPlayerController());
            auto* Player=PC ? Cast<AOBPlayerCharacter>(PC->GetPawn()) : nullptr;if (!Player) return;
            auto* F=Player->GetStageFlowComponent();auto* C=Player->GetPlayerSplineComponent();auto* A=Player->GetActionComponent();
            auto* HUD=PC->GetOBHUD();auto* Overlay=HUD ? HUD->GetPlayerOverlayWidget() : nullptr;
            const auto KeepAlive=S;
            auto Check=[&](bool Good,const TCHAR* Label) { S->OK &= Good;S->Log+=FString::Printf(TEXT("%s %s\n"),Good ? TEXT("PASS") : TEXT("FAIL"),Label); };
            auto Finish=[&]() {
                if (auto* Rail=S->Rail.Get()) Rail->OnTravelStarted.Remove(S->TravelHandle);
                if (S->StagedAlly.IsValid()) S->StagedAlly->GetRouteComponent()->StopRoute();
                if (S->Route.IsValid()) S->Route->Destroy();
                S->Log+=FString::Printf(TEXT("MaxPlayerDrift=%.3fcm MaxRailDrift=%.3fcm MaxAllyDrift=%.3fcm TravelEvents=%d\noutcome=%s\n"),
                    S->MaxPlayerDrift,S->MaxRailDrift,S->MaxAllyDrift,S->TravelCount,S->OK ? TEXT("PASS") : TEXT("FAIL"));
                FFileHelper::SaveStringToFile(S->Log,*(Output/TEXT("departure-pie.txt")));
                UE_LOG(LogTemp,Display,TEXT("STAGE_DEPARTURE_PIE\n%s"),*S->Log);
                const bool OK=S->OK;World->GetTimerManager().ClearTimer(S->Timer);GEditor->RequestEndPlayMap();
                FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([OK](float) {
                    if (GEditor && GEditor->PlayWorld) return true;
                    FPlatformMisc::RequestExitWithStatus(false,OK ? 0 : 1);return false;
                }),.5f);
            };
            if (World->GetTimeSeconds()-S->Began>80) { Check(false,TEXT("Departure scenario deadline"));Finish();return; }
            if (S->Step==0)
            {
                Check(F->IsStageActive(),TEXT("Real Stage_01 native flow is active"));
                F->RestoreSlowMotion();F->ClearPresentation();F->RestoreOpeningMovement();F->AutomaticScriptIds.Reset();
                F->Phase=EOBStagePhase::Travel;F->CurrentZone=0;C->SetStageMovementBlocked(false);A->ResetEncounterForQA();
                Player->SetCanBeDamaged(false);
                if (Overlay) { Overlay->SetVisibility(ESlateVisibility::HitTestInvisible);Overlay->SetStageMode(); }
                AOBEncounterTrigger* Selected=nullptr;
                for (TActorIterator<AOBEncounterTrigger> It(World);It;++It) { It->ResetForQA();if (It->GetPointIndex()==7) Selected=*It; }
                Check(Selected!=nullptr,TEXT("Saved Zone1 encounter trigger exists"));
                if (!Selected) { Finish();return; }
                C->CurrentPointIndex=7;C->NextSplineCuePoint=8;C->TargetPointIndex=INDEX_NONE;
                C->CurrentDistance=C->TargetDistance=C->GetRailPath()->GetSpline()->GetDistanceAlongSplineAtSplinePoint(7);
                C->ApplySplineTransform(C->CurrentDistance);Selected->ActivateForPlayer(Player);C->BeginStopAlignment(7);C->BeginAction();
                Check(F->GetCurrentZone()==1 && F->GetPhase()==EOBStagePhase::Combat,TEXT("Real Zone1 enters combat"));
                Check(PanelsVisible(Overlay),TEXT("Zone1 combat shows actual Timer and Life widgets"));
                S->Step=1;return;
            }
            if (S->Step==1)
            {
                if (F->ActiveScriptId!=2006 || !F->NarrationVoice || !F->NarrationVoice->IsPlaying())
                {
                    if (!F->ActiveScriptId) { Check(false,TEXT("Arrival AudioComponent was observed playing"));Finish(); }
                    return;
                }
                S->bSawArrivalAudio=true;
                const auto Targets=A->GetEncounterEnemies();Check(!Targets.IsEmpty(),TEXT("Saved Zone1 contains actual spawned enemies"));
                for (AOBEnemyCharacter* E : Targets) if (IsValid(E)) { FDamageEvent Damage;E->TakeDamage(E->GetMaxHealth()+1.f,Damage,PC,Player); }
                Check(!A->HasEncounterEnemies() && F->GetPhase()==EOBStagePhase::AwaitingDeparture,TEXT("Normal last kill defers CompleteAction until narration drains"));
                Check(F->ActiveScriptId==2006,TEXT("Last kill preserves the playing arrival line"));
                const int32 Pending=F->NarrationQueue.Num();A->CompleteAction();A->CompleteAction();
                Check(Pending==1 && F->NarrationQueue.Num()==Pending,TEXT("Repeated clear requests enqueue exactly one departure order"));
                AOBAllyCharacter* Ally=nullptr;for (TActorIterator<AOBAllyCharacter> It(World);It;++It) { Ally=*It;break; }
                Check(Ally!=nullptr,TEXT("An existing level squad member is available"));
                if (!Ally) { Finish();return; }
                auto* Route=World->SpawnActor<AOBAllyRoute>();S->Route=Route;
                const FVector Start=Player->GetActorLocation()+FVector(0,500,2500);
                auto* Spline=Route->GetSpline();Spline->ClearSplinePoints(false);
                Spline->AddSplinePoint(Start,ESplineCoordinateSpace::World,false);
                Spline->AddSplinePoint(Start+FVector(5000,0,0),ESplineCoordinateSpace::World,true);
                auto* AllyControl=Ally->GetRouteComponent();AllyControl->bSweepMovement=false;AllyControl->MoveSpeed=150.f;
                Check(AllyControl->StageRouteForPlayerDeparture(Route,Player),TEXT("Actual ally waits on a disposable route for the real player travel event"));
                S->StagedAlly=Ally;
                S->TravelHandle=C->OnTravelStarted.AddLambda([S,Weak]() {
                    ++S->TravelCount;
                    if (auto* CurrentWorld=Weak.Get())
                        if (auto* CurrentPC=Cast<AOBPlayerController>(CurrentWorld->GetFirstPlayerController()))
                            if (auto* CurrentPlayer=Cast<AOBPlayerCharacter>(CurrentPC->GetPawn()))
                            {
                                auto* Stage=CurrentPlayer->GetStageFlowComponent();
                                S->bTravelAfterNarration &= Stage->ActiveScriptId==0 && Stage->NarrationQueue.IsEmpty() && !Stage->bDeparturePending;
                            }
                });
                S->HeldPosition=Player->GetActorLocation();S->HeldDistance=C->GetDistanceAlongRoute();
                for (TActorIterator<AOBAllyCharacter> It(World);It;++It) S->Allies.Add(*It,It->GetActorLocation());
                S->Step=2;return;
            }
            if (S->Step==2)
            {
                if (F->GetPhase()==EOBStagePhase::AwaitingDeparture)
                {
                    S->MaxPlayerDrift=FMath::Max(S->MaxPlayerDrift,float(FVector::Distance(Player->GetActorLocation(),S->HeldPosition)));
                    S->MaxRailDrift=FMath::Max(S->MaxRailDrift,FMath::Abs(C->GetDistanceAlongRoute()-S->HeldDistance));
                    for (const auto& Ally : S->Allies) if (Ally.Key.IsValid()) S->MaxAllyDrift=FMath::Max(S->MaxAllyDrift,float(FVector::Distance(Ally.Key->GetActorLocation(),Ally.Value)));
                    S->bHoldValid &= F->IsInputBlocked() && Player->GetCharacterMovement()->MovementMode==MOVE_None && PanelsHidden(Overlay) && S->TravelCount==0;
                    S->bHoldValid &= S->StagedAlly.IsValid() && S->StagedAlly->GetRouteComponent()->IsWaitingForPlayerDeparture();
                    if (F->ActiveScriptId==2007 && F->NarrationVoice && F->NarrationVoice->IsPlaying())
                    {
                        if (!S->bSawDepartureAudio) FScreenshotRequest::RequestScreenshot(Output/TEXT("DepartureWait.png"),true,false);
                        S->bSawDepartureAudio=true;
                    }
                    return;
                }
                Check(S->bSawArrivalAudio && S->bSawDepartureAudio,TEXT("Arrival and departure both used actual playing AudioComponents"));
                Check(S->bHoldValid && S->MaxPlayerDrift<=1.f && S->MaxRailDrift<=.01f && S->MaxAllyDrift<=1.f,
                    TEXT("Dialogue hold freezes player physics rail and level allies; hides Timer/Life and blocks input"));
                Check(F->GetPhase()==EOBStagePhase::Travel && A->GetActionState()==EOBActionState::Prepare,TEXT("Dialogue completion resets the encounter and releases departure alignment"));
                S->Step=3;return;
            }
            if (S->Step==3)
            {
                S->bTravelHUDHidden &= PanelsHidden(Overlay);
                if (S->TravelCount==0 || C->GetDistanceAlongRoute()<=S->HeldDistance+5.f || !S->StagedAlly.IsValid() || S->StagedAlly->GetRouteComponent()->GetDistanceAlongRoute()<=5.f) return;
                Check(S->TravelCount==1 && S->bTravelAfterNarration,TEXT("The real travel event fires once after every queued line has finished"));
                // Multicast delegates do not promise callback ordering. Inspect after the broadcast,
                // once both routes have advanced, instead of racing the ally's own event callback.
                Check(S->TravelCount==1 && !S->StagedAlly->GetRouteComponent()->IsWaitingForPlayerDeparture(),TEXT("The ally releases on the same completed travel broadcast as the player"));
                Check(S->bTravelHUDHidden,TEXT("Actual Timer/Life widgets stay hidden during movement"));
                FScreenshotRequest::RequestScreenshot(Output/TEXT("TravelHUD.png"),true,false);
                S->Step=4;return;
            }
            if (S->Step==4)
            {
                if (F->GetCurrentZone()!=2 || F->GetPhase()!=EOBStagePhase::Combat) return;
                Check(S->TravelCount==1,TEXT("Departure produces no duplicate travel event"));
                Check(PanelsVisible(Overlay),TEXT("Actual Timer and Life widgets reappear on Zone2 combat arrival"));
                FScreenshotRequest::RequestScreenshot(Output/TEXT("Zone2HUD.png"),true,false);
                S->Step=5;return;
            }
            if (S->Step==5) Finish();
        }),.05f,true);
    }
};
static FAutoConsoleCommand GOBStageDeparturePIE(TEXT("OB.Stage.DeparturePIE"),
    TEXT("Disposable Stage_01 departure narration squad synchronization and HUD PIE verification. Exits this editor."),
    FConsoleCommandDelegate::CreateStatic(&FOBStageDepartureMapSmoke::Start));
#endif

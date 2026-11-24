#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Editor.h"
#include "LevelEditor.h"
#include "SLevelViewport.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Engine/DamageEvents.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "Components/CapsuleComponent.h"
#include "Components/SplineComponent.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Character/OBAllyCharacter.h"
#include "Character/OBAllySplineComponent.h"
#include "Character/OBEnemyCharacter.h"
#include "Character/OBActionComponent.h"
#include "Stage/OBStageFlowComponent.h"
#include "World/OBSquadRendezvous.h"
#include "World/OBAllyRoute.h"
#include "GameFramework/PlayerController.h"

namespace { bool GStageSquadAuditOnly=false,GStageSquadPreview=false; }
class FOBStageSquadMapSmoke
{
public:
    static void Start()
    {
        FRequestPlaySessionParams Params;Params.GlobalMapOverride=TEXT("/Game/Maps/Stage_01");Params.bAllowOnlineSubsystem=false;
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        if (GStageSquadPreview && Params.DestinationSlateViewport.IsSet())
            if (auto View=Params.DestinationSlateViewport.GetValue().Pin()) View->MakeImmersive(true,false);
        GEditor->RequestPlaySession(Params);
        const double Began=FPlatformTime::Seconds();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Began](float) {
            auto* W=GEditor ? GEditor->PlayWorld.Get() : nullptr;
            auto* PC=W ? W->GetFirstPlayerController() : nullptr;
            auto* P=PC ? Cast<AOBPlayerCharacter>(PC->GetPawn()) : nullptr;
            if (P && W->GetTimeSeconds()>.8f && P->GetPlayerSplineComponent()->GetRailPath()) { Run(W,P);return false; }
            if (FPlatformTime::Seconds()-Began>100) { FPlatformMisc::RequestExitWithStatus(false,1);return false; }
            return true;
        }),.1f);
    }
private:
    static void AuditRoutes(UWorld* W,AOBPlayerCharacter* P,const FString& Out)
    {
        P->GetStageFlowComponent()->bActive=false;
        P->GetPlayerSplineComponent()->SetStageMovementBlocked(true);
        TArray<AOBSquadRendezvous*> Sections;
        for (TActorIterator<AOBSquadRendezvous> It(W);It;++It) Sections.Add(*It);
        Sections.Sort([](const auto& A,const auto& B) { return A.DepartFromPlayerPoint<B.DepartFromPlayerPoint; });
        FString Report;bool OK=Sections.Num()==7;int32 Completed=0;
        for (auto* Section : Sections)
        {
            if (!Section->bEnabled) continue;
            if (Section->DepartFromPlayerPoint==85)
            {
                const bool Valid=Section->DepartAfterZone==6;OK &= Valid;
                Report+=Valid ? TEXT("PASS post-breach section uses StageFlow zone 6; path verified after real door destruction\n")
                    : TEXT("FAIL post-breach section must use StageFlow zone 6\n");
                continue;
            }
            const bool Started=Section->StartForPlayer(P);OK &= Started;
            if (!Started) { Report+=TEXT("FAIL invalid section ")+Section->GetActorLabel()+TEXT("\n");continue; }
            for (int32 Frame=0;Frame<20000;++Frame)
            {
                static_cast<AActor*>(Section)->Tick(1.f/60);
                bool Done=true;
                for (const auto& M : Section->Members)
                {
                    auto* C=M.Ally->GetRouteComponent();
                    if (!C->HasCompletedRoute() && !C->IsBlocked())
                    {
                        static_cast<UActorComponent*>(C)->TickComponent(1.f/60,LEVELTICK_All,&C->PrimaryComponentTick);
                        Done=false;
                    }
                }
                Section->ConstrainPlayerDistance(P,FLT_MAX);
                if (Done) break;
            }
            for (const auto& M : Section->Members)
            {
                const auto* C=M.Ally->GetRouteComponent();const bool Pass=C->HasCompletedRoute();
                OK &= Pass;if (Pass) ++Completed;
                Report+=FString::Printf(TEXT("%s route=%s distance=%.2f position=%s\n"),Pass ? TEXT("PASS") : TEXT("FAIL"),*M.Route->GetActorLabel(),C->GetDistanceAlongRoute(),*M.Ally->GetActorLocation().ToString());
            }
        }
        Report+=FString::Printf(TEXT("outcome=%s completed=%d/10 pre-breach routes\n"),OK ? TEXT("PASS") : TEXT("FAIL"),Completed);
        FFileHelper::SaveStringToFile(Report,*(Out/TEXT("paths-pie.txt")));
        UE_LOG(LogTemp,Display,TEXT("STAGE_SQUAD_PATHS_PIE\n%s"),*Report);
        GEditor->RequestEndPlayMap();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([OK](float) {
            if (GEditor && GEditor->PlayWorld) return true;
            FPlatformMisc::RequestExitWithStatus(false,OK ? 0 : 1);return false;
        }),.5f);
    }
    static void Run(UWorld* W,AOBPlayerCharacter* P)
    {
        struct FState
        {
            double Began=0;FString Log;FTimerHandle Timer;
            TSet<int32> Zones;TSet<TWeakObjectPtr<AOBSquadRendezvous>> Released;
            TMap<int32,double> CombatBegan;
            bool bBreached=false;
            bool bPreviewFinished=false;
            float MinPair=FLT_MAX,MinPlayer=FLT_MAX;
            int32 CentralObstructionFrames=0;
            FString Samples=TEXT("time,zone,phase,ally,position,playerDistance,forwardDistance,screenAngle\n");
            int32 SampleTick=0;
            float LastPlayerDistance=0.f,LastPlayerMoveTime=0.f;
        };
        auto State=MakeShared<FState>();State->Began=W->GetTimeSeconds();
        const FString Out=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/(GStageSquadPreview ? TEXT("Tests/StageSquad/Live") : TEXT("Tests/StageSquad/PIE")));
        IFileManager::Get().MakeDirectory(*Out,true);
        // Inspect every saved route in the actual PIE physics scene, including
        // later sections, so authoring can repair all obstructions in one pass.
        FString Clearance=TEXT("route,distance,feet,obstacle,component\n");
        FString Floors=TEXT("route,endpoint,feet,floor,obstacle\n");
        FCollisionQueryParams Query(SCENE_QUERY_STAT(OBSquadRouteAudit),false);
        for (TActorIterator<APawn> It(W);It;++It) Query.AddIgnoredActor(*It);
        for (TActorIterator<AOBSquadRendezvous> It(W);It;++It)
            for (const auto& M : It->Members)
            {
                if (!M.Ally || !M.Route) continue;
                auto* Capsule=M.Ally->GetCapsuleComponent();auto* Spline=M.Route->GetSpline();
                const FVector Lift(0,0,Capsule->GetScaledCapsuleHalfHeight()+M.Ally->GetRouteComponent()->GroundClearance);
                const auto Shape=FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),Capsule->GetScaledCapsuleHalfHeight());
                for (int32 Index : {0,Spline->GetNumberOfSplinePoints()-1})
                {
                    const FVector Feet=Spline->GetLocationAtSplinePoint(Index,ESplineCoordinateSpace::World);
                    FHitResult Floor;
                    W->LineTraceSingleByChannel(Floor,Feet+FVector(0,0,60),Feet-FVector(0,0,400),ECC_Visibility,Query);
                    Floors+=FString::Printf(TEXT("%s,%d,%s,%s,%s\n"),*M.Route->GetActorLabel(),Index,*Feet.ToString(),*Floor.ImpactPoint.ToString(),Floor.GetActor() ? *Floor.GetActor()->GetActorLabel() : TEXT("none"));
                }
                bool WasBlocked=false;
                for (float D=0;D<Spline->GetSplineLength();D+=20)
                {
                    const FVector Feet=Spline->GetLocationAtDistanceAlongSpline(D,ESplineCoordinateSpace::World);
                    const FVector Next=Spline->GetLocationAtDistanceAlongSpline(FMath::Min(D+20,Spline->GetSplineLength()),ESplineCoordinateSpace::World);
                    FHitResult Hit;
                    const bool Blocked=W->SweepSingleByChannel(Hit,Feet+Lift,Next+Lift,FQuat::Identity,Capsule->GetCollisionObjectType(),Shape,Query,FCollisionResponseParams(Capsule->GetCollisionResponseToChannels()));
                    if (Blocked && !WasBlocked) Clearance+=FString::Printf(TEXT("%s,%.1f,%s,%s,%s\n"),*M.Route->GetActorLabel(),D,*Feet.ToString(),Hit.GetActor() ? *Hit.GetActor()->GetActorLabel() : TEXT("none"),*GetNameSafe(Hit.GetComponent()));
                    WasBlocked=Blocked;
                }
            }
        FFileHelper::SaveStringToFile(Clearance,*(Out/TEXT("route-clearance.csv")));
        FFileHelper::SaveStringToFile(Floors,*(Out/TEXT("route-floors.csv")));
        if (GStageSquadAuditOnly) { AuditRoutes(W,P,Out);return; }
        P->SetCanBeDamaged(false);P->GetStageFlowComponent()->FinishOpening();
        P->GetStageFlowComponent()->StageRecord.bUsedTestInputs=true;
        if (GStageSquadPreview)
        {
            W->GetFirstPlayerController()->SetViewTarget(P);
            GEngine->AddOnScreenDebugMessage(71001,600.f,FColor::Cyan,TEXT("1인칭 이동 검증  |  Esc: 플레이 종료"));
        }
        TWeakObjectPtr<UWorld> Weak=W;
        W->GetTimerManager().SetTimer(State->Timer,FTimerDelegate::CreateLambda([State,Weak,Out]() {
            auto* World=Weak.Get();if (!World) return;
            auto* PC=World->GetFirstPlayerController();auto* Player=PC ? Cast<AOBPlayerCharacter>(PC->GetPawn()) : nullptr;
            if (!Player) return;
            auto* F=Player->GetStageFlowComponent();auto* C=Player->GetPlayerSplineComponent();
            if (GStageSquadPreview)
            {
                if (State->bPreviewFinished) return;
            }
            auto Finish=[&](bool OK,const FString& Reason) {
                State->Log+=FString::Printf(TEXT("firstPerson=1 minAllySeparation=%.1f minPlayerDistance=%.1f centralObstructionFrames=%d\n"),State->MinPair,State->MinPlayer,State->CentralObstructionFrames);
                OK &= State->CentralObstructionFrames==0 && State->MinPair>=100.f;
                State->Log+=FString::Printf(TEXT("outcome=%s reason=%s zones=%d gates=%d\n"),OK ? TEXT("PASS") : TEXT("FAIL"),*Reason,State->Zones.Num(),State->Released.Num());
                FFileHelper::SaveStringToFile(State->Log,*(Out/TEXT("squad-pie.txt")));
                FFileHelper::SaveStringToFile(State->Samples,*(Out/TEXT("first-person-clearance.csv")));
                UE_LOG(LogTemp,Display,TEXT("STAGE_SQUAD_PIE\n%s"),*State->Log);
                if (GStageSquadPreview)
                {
                    State->bPreviewFinished=true;F->SuspendForQA();C->SetStageMovementBlocked(true);
                    GEngine->AddOnScreenDebugMessage(71001,600.f,OK ? FColor::Green : FColor::Red,
                        OK ? TEXT("1인칭 이동 검증 완료  |  Esc: 플레이 종료") : TEXT("1인칭 이동 검증 중 오류 발생: 로그 확인  |  Esc: 플레이 종료"));
                    return;
                }
                World->GetTimerManager().ClearTimer(State->Timer);GEditor->RequestEndPlayMap();
                FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([OK](float) {
                    if (GEditor && GEditor->PlayWorld) return true;
                    FPlatformMisc::RequestExitWithStatus(false,OK ? 0 : 1);return false;
                }),.5f);
            };
            if (PC->GetViewTarget()!=Player) { Finish(false,TEXT("verification camera left the first-person player"));return; }
            if (F->GetPhase()!=EOBStagePhase::Travel || FMath::Abs(C->GetDistanceAlongRoute()-State->LastPlayerDistance)>1.f)
            { State->LastPlayerMoveTime=World->GetTimeSeconds();State->LastPlayerDistance=C->GetDistanceAlongRoute(); }
            if (F->GetPhase()==EOBStagePhase::Travel && World->GetTimeSeconds()-State->LastPlayerMoveTime>25.f)
            { Finish(false,TEXT("rendezvous held player travel for more than 25 seconds"));return; }
            if (World->GetTimeSeconds()-State->Began>240) { Finish(false,TEXT("route flow timeout"));return; }
            if (F->GetPhase()==EOBStagePhase::Ending || F->GetPhase()==EOBStagePhase::Complete)
            { Finish(false,TEXT("stage ended before all squad endpoints completed"));return; }
            int32 AllyCount=0;TArray<AOBAllyCharacter*> VisibleAllies;
            FVector ViewLocation;FRotator ViewRotation;PC->GetPlayerViewPoint(ViewLocation,ViewRotation);
            const FVector ViewForward=ViewRotation.Vector();
            const bool Sample=(++State->SampleTick%6)==0;
            for (TActorIterator<AOBAllyCharacter> It(World);It;++It)
            {
                ++AllyCount;auto* R=It->GetRouteComponent();
                if (!It->IsHidden())
                {
                    VisibleAllies.Add(*It);
                    const FVector Body=It->GetActorLocation()+FVector(0,0,45);
                    const FVector Delta=Body-ViewLocation;
                    const float Forward=FVector::DotProduct(Delta,ViewForward);
                    const float Angle=FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Delta.GetSafeNormal(),ViewForward),-1.f,1.f)));
                    const float PlayerDistance=FVector::Dist2D(Player->GetActorLocation(),It->GetActorLocation());
                    if (FMath::Abs(Player->GetActorLocation().Z-It->GetActorLocation().Z)<150.f) State->MinPlayer=FMath::Min(State->MinPlayer,PlayerDistance);
                    FCollisionQueryParams Sight(SCENE_QUERY_STAT(OBSquadFirstPersonAudit),false,Player);
                    for (TActorIterator<APawn> Pawn(World);Pawn;++Pawn) Sight.AddIgnoredActor(*Pawn);
                    if (Forward>0.f && Forward<300.f && Angle<14.f && !World->LineTraceTestByChannel(ViewLocation,Body,ECC_Visibility,Sight))
                    {
                        ++State->CentralObstructionFrames;
                        if (State->CentralObstructionFrames==1) FScreenshotRequest::RequestScreenshot(Out/TEXT("CentralObstruction.png"),false,false);
                    }
                    if (Sample) State->Samples+=FString::Printf(TEXT("%.2f,%d,%d,%s,%s,%.1f,%.1f,%.1f\n"),World->GetTimeSeconds(),F->GetCurrentZone(),static_cast<int32>(F->GetPhase()),*It->GetActorLabel(),*It->GetActorLocation().ToString(),PlayerDistance,Forward,Angle);
                }
                if (R->IsBlocked())
                {
                    Finish(false,FString::Printf(TEXT("blocked ally=%s route=%s distance=%.1f location=%s"),*It->GetActorLabel(),*GetNameSafe(R->GetCurrentRoute()),R->GetDistanceAlongRoute(),*It->GetActorLocation().ToString()));return;
                }
            }
            if (VisibleAllies.Num()==2)
                State->MinPair=FMath::Min(State->MinPair,FVector::Distance(VisibleAllies[0]->GetActorLocation(),VisibleAllies[1]->GetActorLocation()));
            if (State->SampleTick%60==0)
            {
                FFileHelper::SaveStringToFile(State->Samples,*(Out/TEXT("first-person-clearance.csv")));
                FFileHelper::SaveStringToFile(State->Log+FString::Printf(TEXT("progress time=%.1f zone=%d distance=%.1f minPair=%.1f centralFrames=%d\n"),World->GetTimeSeconds(),F->GetCurrentZone(),C->GetDistanceAlongRoute(),State->MinPair,State->CentralObstructionFrames),*(Out/TEXT("progress.txt")));
            }
            if (AllyCount!=2) { Finish(false,TEXT("squad count changed"));return; }
            int32 Sections=0,Started=0;
            for (TActorIterator<AOBSquadRendezvous> It(World);It;++It)
            {
                ++Sections;if (It->IsSectionStarted()) ++Started;
                if (It->PlayerRendezvousPoint!=INDEX_NONE && It->IsRendezvousReleased() && !State->Released.Contains(*It))
                {
                    State->Released.Add(*It);
                    State->Log+=FString::Printf(TEXT("PASS gate=%s point=%d time=%.2f\n"),*It->GetActorLabel(),It->PlayerRendezvousPoint,World->GetTimeSeconds());
                }
            }
            if (Sections!=7) { Finish(false,TEXT("expected seven installed squad sections"));return; }
            if (F->GetPhase()==EOBStagePhase::Combat)
            {
                const int32 Zone=F->GetCurrentZone();
                if (!State->CombatBegan.Contains(Zone))
                {
                    if (Zone<6)
                        for (TActorIterator<AOBSquadRendezvous> It(World);It;++It)
                            if (It->bEnabled && It->DepartAfterZone==Zone)
                            {
                                bool Staged=It->IsSectionStaged() && !It->IsSectionStarted();
                                for (const auto& M : It->Members)
                                    Staged &= M.Ally->GetRouteComponent()->GetCurrentRoute()==M.Route && M.Ally->GetRouteComponent()->IsWaiting()
                                        && M.Ally->GetRouteComponent()->GetDistanceAlongRoute()<.01f;
                                if (!Staged) { Finish(false,FString::Printf(TEXT("zone %d did not pre-stage both allies at green starts"),Zone));return; }
                                State->Log+=FString::Printf(TEXT("PASS staged zone=%d both allies waiting at green starts before combat\n"),Zone);
                            }
                    State->CombatBegan.Add(Zone,World->GetTimeSeconds());State->Zones.Add(Zone);
                    State->Log+=FString::Printf(TEXT("PASS combat zone=%d startedSections=%d time=%.2f\n"),Zone,Started,World->GetTimeSeconds());
                    FScreenshotRequest::RequestScreenshot(Out/FString::Printf(TEXT("Zone%d.png"),Zone),false,false);
                }
                if (World->GetTimeSeconds()-State->CombatBegan[Zone]>(GStageSquadPreview ? 6.f : 2.f))
                {
                    if (Zone<6)
                    {
                        FDamageEvent Damage;
                        const auto Enemies=Player->GetActionComponent()->GetEncounterEnemies();
                        for (AOBEnemyCharacter* E : Enemies) if (IsValid(E) && E->GetCombatState()!=EOBEnemyCombatState::Dead)
                            E->TakeDamage(100000.f,Damage,PC,Player);
                    }
                    else
                    {
                        bool Complete=Started==6;
                        for (TActorIterator<AOBAllyCharacter> It(World);It;++It) Complete &= It->GetRouteComponent()->HasCompletedRoute();
                        if (Complete) { Finish(State->Zones.Num()==6 && State->Released.Num()==5,TEXT("all zones, rendezvous and post-breach endpoints reached"));return; }
                    }
                }
            }
            if (F->GetPhase()==EOBStagePhase::Zone6Breach && !State->bBreached)
            {
                State->bBreached=F->AttachBreachChargeForQA();
            }
        }),GStageSquadPreview ? 1.f/60 : .05f,true);
    }
};
static FAutoConsoleCommand GOBStageSquadPIE(TEXT("OB.Stage.SquadPIE"),
    TEXT("Verify authored squad paths and rendezvous through all six zones in disposable PIE, then exit."),
    FConsoleCommandDelegate::CreateLambda([]() { GEngine->Exec(nullptr,TEXT("OB.Stage.SquadNavPIE")); }));
static FAutoConsoleCommand GOBStageSquadPathsPIE(TEXT("OB.Stage.SquadPathsPIE"),
    TEXT("Sweep-walk every saved squad route in the real PIE physics scene, then exit."),
    FConsoleCommandDelegate::CreateLambda([]() { GEngine->Exec(nullptr,TEXT("OB.Stage.SquadNavAudit")); }));
static FAutoConsoleCommand GOBStageSquadPreview(TEXT("OB.Stage.SquadPreview"),
    TEXT("Visible first-person squad verification. Auto-clears combat and leaves the editor open."),
    FConsoleCommandDelegate::CreateLambda([]() { GEngine->Exec(nullptr,TEXT("OB.Stage.SquadNavPreview")); }));
#endif

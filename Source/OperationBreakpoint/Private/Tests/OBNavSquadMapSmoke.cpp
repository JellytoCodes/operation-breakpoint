#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Editor.h"
#include "LevelEditor.h"
#include "SLevelViewport.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/DamageEvents.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "NavigationData.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SplineComponent.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Character/OBAllyCharacter.h"
#include "Character/OBAllyNavigationComponent.h"
#include "Character/OBAllySplineComponent.h"
#include "Character/OBEnemyCharacter.h"
#include "Character/OBActionComponent.h"
#include "Stage/OBStageFlowComponent.h"
#include "World/OBSquadRendezvous.h"
#include "AI/OBAllyAIController.h"
#include "Widget/OBDamageScreenWidget.h"
#include "Components/Image.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/PlayerController.h"
#include "Components/SkeletalMeshComponent.h"
#include "Character/OBWeaponGripComponent.h"
#include "UObject/UnrealType.h"
#include "ImageUtils.h"
#include "Engine/GameViewportClient.h"

class FOBNavSquadMapSmoke
{
public:
    static void Start(bool Audit,bool Visible)
    {
        FRequestPlaySessionParams Params;Params.GlobalMapOverride=TEXT("/Game/Maps/Stage_01");Params.bAllowOnlineSubsystem=false;
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        if (Visible && Params.DestinationSlateViewport.IsSet())
            if (auto View=Params.DestinationSlateViewport.GetValue().Pin()) View->MakeImmersive(true,false);
        GEditor->RequestPlaySession(Params);const double Began=FPlatformTime::Seconds();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Began,Audit,Visible](float) {
            auto* W=GEditor ? GEditor->PlayWorld.Get() : nullptr;auto* PC=W ? W->GetFirstPlayerController() : nullptr;
            auto* P=PC ? Cast<AOBPlayerCharacter>(PC->GetPawn()) : nullptr;
            if (P && W->GetTimeSeconds()>.8f && P->GetPlayerSplineComponent()->GetRailPath())
            { Run(W,P,Audit,Visible);return false; }
            if (FPlatformTime::Seconds()-Began>100) { FPlatformMisc::RequestExitWithStatus(false,1);return false; }
            return true;
        }),.1f);
    }
private:
    static void ExitPIE(bool OK)
    {
        GEditor->RequestEndPlayMap();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([OK](float) {
            if (GEditor && GEditor->PlayWorld) return true;
            FPlatformMisc::RequestExitWithStatus(false,OK ? 0 : 1);return false;
        }),.25f);
    }
    static void Run(UWorld* W,AOBPlayerCharacter* P,bool Audit,bool Visible)
    {
        FString Out=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Tests/StageSquad/Navigation"));
        FParse::Value(FCommandLine::Get(),TEXT("StageNavOutput="),Out);
        IFileManager::Get().MakeDirectory(*Out,true);
        if (Audit)
        {
            FString Report;bool OK=true;int32 Count=0;
            auto* NS=FNavigationSystem::GetCurrent<UNavigationSystemV1>(W);
            for (TActorIterator<AOBSquadRendezvous> It(W);It;++It)
                if (It->bEnabled && It->bUseNavigation)
                    for (const auto& M : It->Members)
                    {
                        ++Count;bool Pass=NS && M.Ally && M.StartPoint && M.EndPoint;
                        FNavLocation A,B;UNavigationPath* Path=nullptr;bool StartOnNav=false,EndOnNav=false;const ANavigationData* Data=nullptr;
                        if (Pass)
                        {
                            auto* Move=M.Ally->GetCharacterMovement();Move->SetUpdateNavAgentWithOwnersCollisions(true);Move->UpdateNavAgent(*M.Ally->GetCapsuleComponent());
                            const auto& Props=Move->GetNavAgentPropertiesRef();
                            Data=NS->GetNavDataForProps(Props,M.StartPoint->GetActorLocation());
                            StartOnNav=NS->ProjectPointToNavigation(M.StartPoint->GetActorLocation(),A,FVector(100,100,100),&Props);
                            EndOnNav=NS->ProjectPointToNavigation(M.EndPoint->GetActorLocation(),B,FVector(100,100,100),&Props);
                            Pass=StartOnNav && EndOnNav;
                            if (Pass)
                            {
                                auto* DefaultPath=UNavigationSystemV1::FindPathToLocationSynchronously(W,A.Location,B.Location,M.Ally);
                                const auto Raw=DefaultPath ? DefaultPath->GetPath() : FNavPathSharedPtr();
                                Report+=FString::Printf(TEXT("default_search zone=%d ally=%s complete=%d exhausted=%d nodes=%d\n"),It->DepartAfterZone,*GetNameSafe(M.Ally),Raw.IsValid() && !Raw->IsPartial(),Raw.IsValid() && Raw->DidSearchReachedLimit(),Data ? Data->GetDefaultQueryFilter()->GetMaxSearchNodes() : 0);
                                Path=UNavigationSystemV1::FindPathToLocationSynchronously(W,A.Location,B.Location,M.Ally,UOBAllyNavigationFilter::StaticClass());
                            }
                            Pass=Pass && Path && Path->IsValid() && !Path->IsPartial();
                        }
                        OK &= Pass;
                        Report+=FString::Printf(TEXT("%s zone=%d ally=%s start=%s end=%s length=%.1f projected=%d/%d nav=%s radius=%.1f from=%s to=%s\n"),Pass ? TEXT("PASS") : TEXT("FAIL"),It->DepartAfterZone,*GetNameSafe(M.Ally),*GetNameSafe(M.StartPoint),*GetNameSafe(M.EndPoint),Path ? Path->GetPathLength() : 0.,StartOnNav,EndOnNav,*GetNameSafe(Data),Data ? Data->GetConfig().AgentRadius : 0.,*A.Location.ToString(),*B.Location.ToString());
                        if (!Pass && Path) for (const FVector& Point : Path->PathPoints) Report+=TEXT("  path ")+Point.ToString()+TEXT("\n");
                    }
            OK &= Count==14;
            Report+=FString::Printf(TEXT("outcome=%s paths=%d\n"),OK ? TEXT("PASS") : TEXT("FAIL"),Count);
            FFileHelper::SaveStringToFile(Report,*(Out/TEXT("audit.txt")));UE_LOG(LogTemp,Display,TEXT("SQUAD_NAV_AUDIT\n%s"),*Report);ExitPIE(OK);return;
        }
        struct FState
        {
            FTimerHandle Timer;TSet<int32> Zones;TMap<int32,float> CombatBegan;bool Breached=false,Finished=false;
            FString Report,Samples=TEXT("time,section,jindo1,jindo2,player\n");float LastMoveTime=0,LastDistance=0;
            double DamageAt=0;int32 DamageStep=0;
            uint64 LastCaptureFrame=MAX_uint64;
            bool RevisionOK=true,DoorFacingChecked=false;
        };
        auto State=MakeShared<FState>();TWeakObjectPtr<UWorld> WeakW=W;TWeakObjectPtr<AOBPlayerCharacter> WeakP=P;
        const bool RevisionCheck=FParse::Param(FCommandLine::Get(),TEXT("SquadRevisionCheck"));
        if (RevisionCheck)
        {
            auto Check=[&](bool Good,const TCHAR* Message) { State->RevisionOK &= Good;State->Report+=FString::Printf(TEXT("%s %s\n"),Good ? TEXT("PASS") : TEXT("FAIL"),Message); };
            auto* Speed=FindFProperty<FFloatProperty>(P->GetPlayerSplineComponent()->GetClass(),TEXT("MoveSpeed"));
            Check(Speed && FMath::IsNearlyEqual(Speed->GetPropertyValue_InContainer(P->GetPlayerSplineComponent()),500.f),TEXT("Player rail speed is 500 cm/s"));
            for (TActorIterator<AOBSquadRendezvous> It(W);It;++It) if (It->bEnabled && It->DepartAfterZone==0)
                for (const auto& M:It->Members)
                {
                    Check(M.Ally && !M.Ally->IsHidden() && M.StartPoint && FVector::Dist2D(M.Ally->GetActorLocation(),M.StartPoint->GetActorLocation())<5.f,TEXT("Ally is already visible at Zone1 Start during opening, before departure preparation"));
                    Check(M.Ally && M.Ally->GetNavigationComponent()->MoveSpeed==500.f,TEXT("Placed ally navigation speed is 500 cm/s"));
                    Check(M.Ally && GetNameSafe(M.Ally->GetWeaponMesh()->GetSkeletalMeshAsset())==TEXT("SK_AR4") && M.Ally->GetWeaponGripComponent()->ShouldApplyGrip(),TEXT("Placed ally visibly carries the AR4 with native hand grip enabled"));
                }
            auto* View=W->GetGameViewport()->Viewport;TArray<FColor> Pixels;TArray64<uint8> PNG;
            if (View && View->ReadPixels(Pixels)) { const auto Size=View->GetSizeXY();FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(Out/TEXT("Initial-Allies.png"))); }
            FFileHelper::SaveStringToFile(State->Report,*(Out/TEXT("revision-progress.txt")));
        }
        P->SetCanBeDamaged(false);P->GetStageFlowComponent()->FinishOpening();P->GetStageFlowComponent()->StageRecord.bUsedTestInputs=true;
        W->GetFirstPlayerController()->SetViewTarget(P);
        if (Visible) GEngine->AddOnScreenDebugMessage(71001,600,FColor::Cyan,TEXT("NavMesh 이동 검증 | 진돗개 1 → 2 → 3 | 1인칭 유지 | Esc 종료"));
        W->GetTimerManager().SetTimer(State->Timer,FTimerDelegate::CreateLambda([State,WeakW,WeakP,Out,Visible,RevisionCheck]() {
            auto* World=WeakW.Get();auto* Player=WeakP.Get();if (!World || !Player || State->Finished) return;
            auto* Flow=Player->GetStageFlowComponent();auto* Rail=Player->GetPlayerSplineComponent();auto* PC=World->GetFirstPlayerController();
            auto Finish=[&](bool OK,const FString& Why) {
                if (RevisionCheck) OK &= State->RevisionOK && State->DoorFacingChecked;
                State->Finished=true;State->Report+=FString::Printf(TEXT("outcome=%s zones=%d firstPerson=1 reason=%s\n"),OK ? TEXT("PASS") : TEXT("FAIL"),State->Zones.Num(),*Why);
                FFileHelper::SaveStringToFile(State->Report,*(Out/TEXT("flow.txt")));FFileHelper::SaveStringToFile(State->Samples,*(Out/TEXT("order.csv")));
                UE_LOG(LogTemp,Display,TEXT("SQUAD_NAV_FLOW\n%s"),*State->Report);
                if (!Visible) { World->GetTimerManager().ClearTimer(State->Timer);ExitPIE(OK); }
                else { Flow->SuspendForQA();Rail->SetStageMovementBlocked(true);GEngine->AddOnScreenDebugMessage(71001,600,OK ? FColor::Green : FColor::Red,OK ? TEXT("NavMesh 순서 검증 완료 | 1 → 2 → 3 | Esc 종료") : TEXT("NavMesh 검증 중 확인 필요: 로그 참고 | Esc 종료")); }
            };
            if (!PC || PC->GetPawn()!=Player || PC->GetViewTarget()!=Player || GEditor->IsSimulatingInEditor()) { Finish(false,TEXT("first-person control changed"));return; }
            if (RevisionCheck && Flow->GetPhase()==EOBStagePhase::Zone6Dialogue && !State->DoorFacingChecked)
            {
                for (TActorIterator<AOBSquadRendezvous> It(World);It;++It) if (It->bEnabled && It->DepartAfterZone==5)
                {
                    bool Arrived=true;for (const auto& M:It->Members) Arrived &= M.Ally && M.Ally->GetNavigationComponent()->HasArrived();
                    if (!Arrived) continue;
                    bool Good=It->bUseArrivalRotation;
                    for (const auto& M:It->Members)
                    {
                        const FVector ToPlayer=(Player->GetActorLocation()-M.Ally->GetActorLocation()).GetSafeNormal2D();
                        const float Dot=FVector::DotProduct(M.Ally->GetActorForwardVector().GetSafeNormal2D(),ToPlayer);
                        Good &= Dot>.95f;
                        State->Report+=FString::Printf(TEXT("door_facing ally=%s yaw=%.2f dot_to_player=%.4f\n"),*M.Ally->GetName(),M.Ally->GetActorRotation().Yaw,Dot);
                    }
                    State->RevisionOK &= Good;State->DoorFacingChecked=true;
                    State->Report+=(Good ? TEXT("PASS ") : TEXT("FAIL "))+FString(TEXT("Both allies face the player during the Zone6 hostage dialogue\n"));
                    auto* View=World->GetGameViewport()->Viewport;TArray<FColor> Pixels;TArray64<uint8> PNG;
                    if (View && View->ReadPixels(Pixels)) { const auto Size=View->GetSizeXY();FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(Out/TEXT("Zone6-Wait-Facing.png"))); }
                    FFileHelper::SaveStringToFile(State->Report,*(Out/TEXT("revision-progress.txt")));
                }
            }
            if (World->GetTimeSeconds()>260) { Finish(false,TEXT("navigation flow timeout"));return; }
            if (Flow->GetPhase()==EOBStagePhase::Ending || Flow->GetPhase()==EOBStagePhase::Complete) { Finish(false,TEXT("stage ended before navigation completion"));return; }
            if (FParse::Param(FCommandLine::Get(),TEXT("StageNavZone3Check")))
            {
                if (State->DamageStep==1 && World->GetRealTimeSeconds()-State->DamageAt>=.1)
                { FScreenshotRequest::RequestScreenshot(Out/TEXT("Hit-Impact.png"),true,false);State->LastCaptureFrame=GFrameCounter;State->DamageStep=2; }
                else if (State->DamageStep==2 && GFrameCounter!=State->LastCaptureFrame && World->GetRealTimeSeconds()-State->DamageAt>=.65)
                { FScreenshotRequest::RequestScreenshot(Out/TEXT("Hit-Clear.png"),true,false);State->DamageStep=3; }
                if (Flow->GetPhase()!=EOBStagePhase::Travel || FMath::Abs(Rail->GetDistanceAlongRoute()-State->LastDistance)>.5f)
                { State->LastDistance=Rail->GetDistanceAlongRoute();State->LastMoveTime=World->GetTimeSeconds(); }
                if (Flow->GetPhase()==EOBStagePhase::Travel && World->GetTimeSeconds()-State->LastMoveTime>20.f)
                {
                    State->Report+=FString::Printf(TEXT("STALLED zone=%d point=%d distance=%.2f target=%.2f\n"),Flow->GetCurrentZone(),Rail->GetCurrentPointIndex(),Rail->GetDistanceAlongRoute(),Rail->GetTravelTargetDistance());
                    for (TActorIterator<AOBSquadRendezvous> It(World);It;++It)
                        if (It->bEnabled && It->DepartAfterZone==Flow->GetCurrentZone())
                            for (const auto& Member:It->Members) if (Member.Ally)
                            {
                                auto* Nav=Member.Ally->GetNavigationComponent();
                                State->Report+=FString::Printf(TEXT("section=%s started=%d ally=%s progress=%.4f length=%.1f arrived=%d failed=%d position=%s velocity=%s\n"),*It->GetActorLabel(),It->IsSectionStarted(),*Member.Ally->GetActorLabel(),Nav->GetProgress(),Nav->GetPathLength(),Nav->HasArrived(),Nav->HasPathFailure(),*Member.Ally->GetActorLocation().ToString(),*Member.Ally->GetVelocity().ToString());
                            }
                    FScreenshotRequest::RequestScreenshot(Out/TEXT("Travel-Stalled.png"),true,false);Finish(false,TEXT("player rail made no progress for twenty seconds"));return;
                }
            }
            for (TActorIterator<AOBSquadRendezvous> It(World);It;++It)
            {
                if (!It->bEnabled || !It->bUseNavigation || !It->IsSectionStarted() || It->Members.Num()!=2) continue;
                auto* One=It->Members[0].Ally->GetNavigationComponent();auto* Two=It->Members[1].Ally->GetNavigationComponent();
                if (!One->BelongsTo(*It) || !Two->BelongsTo(*It)) continue;
                const float PlayerProgress=FMath::Clamp((Rail->GetDistanceAlongRoute()-It->PlayerSectionStart)/FMath::Max(1.f,It->PlayerSectionEnd-It->PlayerSectionStart),0.f,1.f);
                State->Samples+=FString::Printf(TEXT("%.2f,%d,%.4f,%.4f,%.4f\n"),World->GetTimeSeconds(),It->DepartAfterZone,One->GetProgress(),Two->GetProgress(),PlayerProgress);
                if (Two->GetProgress()>One->GetProgress()+.035f || PlayerProgress>FMath::Min(One->GetProgress(),Two->GetProgress())+.035f)
                { Finish(false,TEXT("Jindo travel order was violated"));return; }
                if (It->Members[0].Ally->GetRouteComponent()->HasRouteControl() || It->Members[1].Ally->GetRouteComponent()->HasRouteControl())
                { Finish(false,TEXT("legacy ally spline still owns movement"));return; }
            }
            FFileHelper::SaveStringToFile(State->Samples,*(Out/TEXT("order.csv")));
            if (Flow->GetPhase()==EOBStagePhase::Combat)
            {
                const int32 Zone=Flow->GetCurrentZone();
                if (!State->Zones.Contains(Zone))
                {
                    State->Zones.Add(Zone);State->CombatBegan.Add(Zone,World->GetTimeSeconds());
                    State->Report+=FString::Printf(TEXT("PASS zone=%d reached using nav followers\n"),Zone);
                    FFileHelper::SaveStringToFile(State->Report,*(Out/TEXT("progress.txt")));
                    FScreenshotRequest::RequestScreenshot(Out/FString::Printf(TEXT("Zone%d.png"),Zone),false,false);
                    if (FParse::Param(FCommandLine::Get(),TEXT("StageNavZone3Check")))
                    {
                        if (Zone==2)
                        {
                            Player->SetCanBeDamaged(true);FDamageEvent Damage;const float Applied=Player->TakeDamage(20,Damage,nullptr,nullptr);Player->SetCanBeDamaged(false);
                            State->Report+=FString::Printf(TEXT("Actual player damage=%.1f lives=%d\n"),Applied,Flow->GetRemainingLives());
                            if (auto* FX=Flow->GetDamageScreenWidget())
                                if (auto* Image=Cast<UImage>(FX->GetWidgetFromName(TEXT("HitFracture"))))
                                    State->Report+=TEXT("Fracture brush=")+GetPathNameSafe(Image->GetBrush().GetResourceObject())+TEXT("\n");
                            State->DamageAt=World->GetRealTimeSeconds();State->DamageStep=1;
                        }
                        if (Zone==3) { Finish(true,TEXT("natural Zone1 to Zone2 to Zone3 rail progression reached"));return; }
                    }
                }
                if (World->GetTimeSeconds()-State->CombatBegan[Zone]>(Visible ? 6.f : 2.f))
                {
                    if (Zone<6)
                    {
                        FDamageEvent Damage;const auto Enemies=Player->GetActionComponent()->GetEncounterEnemies();
                        for (AOBEnemyCharacter* E : Enemies) if (IsValid(E) && E->GetCombatState()!=EOBEnemyCombatState::Dead) E->TakeDamage(100000,Damage,PC,Player);
                    }
                    else
                    {
                        bool Done=true;for (TActorIterator<AOBAllyCharacter> A(World);A;++A) Done &= A->GetNavigationComponent()->HasArrived();
                        if (Done) Finish(State->Zones.Num()==6,TEXT("all six zones and NavMesh endpoints reached in order"));
                    }
                }
            }
            if (Flow->GetPhase()==EOBStagePhase::Zone6Breach && !State->Breached)
            { State->Breached=Flow->AttachBreachChargeForQA(); }
        }),.05f,true);
    }
};
static FAutoConsoleCommand GOBNavSquadAudit(TEXT("OB.Stage.SquadNavAudit"),TEXT("Validate the fourteen active NavMesh Start/End connections, then exit."),FConsoleCommandDelegate::CreateLambda([](){FOBNavSquadMapSmoke::Start(true,false);}));
static FAutoConsoleCommand GOBNavSquadPIE(TEXT("OB.Stage.SquadNavPIE"),TEXT("Verify NavMesh allies and Jindo 1/2/player order through Stage_01 in first person, then exit."),FConsoleCommandDelegate::CreateLambda([](){FOBNavSquadMapSmoke::Start(false,false);}));
static FAutoConsoleCommand GOBNavSquadPreview(TEXT("OB.Stage.SquadNavPreview"),TEXT("Visible first-person NavMesh squad playback; leave the final scene open."),FConsoleCommandDelegate::CreateLambda([](){FOBNavSquadMapSmoke::Start(false,true);}));
#endif

#if WITH_DEV_AUTOMATION_TESTS && !WITH_EDITOR && !UE_BUILD_SHIPPING && !UE_BUILD_TEST
#include "AI/OBAIController.h"
#include "Character/OBActionComponent.h"
#include "Character/OBAllyCharacter.h"
#include "Character/OBEnemyCharacter.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Core/OBPlayerController.h"
#include "Training/OBTrainingDirector.h"
#include "Training/OBTrainingTarget.h"
#include "World/OBEncounterSpawner.h"
#include "World/OBRailPath.h"
#include "Components/CapsuleComponent.h"
#include "Components/SplineComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "PipelineStateCache.h"
#include "RenderTimer.h"
#include "RHI.h"
#include "Scalability.h"
#include "TimerManager.h"
#include "UnrealClient.h"

// Explicitly opt-in, cooked Development only. Samples stay in memory until the run ends.
// Route passes use the production spline/camera component. Tutorial pauses narration only
// for continuous route coverage; Stage combat is a labelled three-enemy fixture, not authored encounters.
class FOBPackagePerformance : public TSharedFromThis<FOBPackagePerformance>
{
    struct FSample
    {
        int32 Phase; double Time,Frame,Game,Draw,GPU; FVector Location;
        uint32 PSOs,GraphicsHitches,ComputeHitches; int32 Focus,Moving,Shots,EnemyShots;
    };
    TArray<FSample> Samples;
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<AOBPlayerCharacter> Player;
    TWeakObjectPtr<AOBPlayerController> Controller;
    TWeakObjectPtr<AOBTrainingDirector> Director;
    TWeakObjectPtr<UOBPlayerSplineComponent> Spline;
    TArray<TWeakObjectPtr<AOBEnemyCharacter>> Enemies;
    FDelegateHandle FrameHandle;
    double Started=FPlatformTime::Seconds(),LastFrame=0,PhaseStart=0,NextShot=0;
    int32 Phase=-1,Shots=0,EnemyShots=0,ResolvedHits=0;
    bool bTutorial=false,bFinished=false;
    FString Map,Metadata;
    static int32 CVarInt(const TCHAR* Name)
    { auto* V=IConsoleManager::Get().FindConsoleVariable(Name); return V ? V->GetInt() : -1; }
    static const TCHAR* PhaseName(int32 P)
    {
        switch(P) { case 0:return TEXT("route_first"); case 1:return TEXT("route_repeat");
        case 2:return TEXT("combat_first"); case 3:return TEXT("combat_repeat"); default:return TEXT("setup"); }
    }
    void BeginPhase(int32 P)
    {
        Phase=P; PhaseStart=FPlatformTime::Seconds();
        // Retain the preceding end-frame timestamp so combat entry/asset loading is
        // measured too. Teleport/reset work is not silently dropped as a warmup.
        NextShot=PhaseStart+.5;
        UE_LOG(LogTemp,Display,TEXT("PACKAGE_PERF phase=%s time=%.3f"),PhaseName(P),PhaseStart-Started);
    }
    void BeginRoute()
    {
        auto* S=Spline.Get(); auto* D=Director.Get();
        if (D)
        {
            D->SetActorTickEnabled(false);
            D->GetWorldTimerManager().ClearAllTimersForObject(D);
            D->StopPrototypeNarration(); D->StopPrototypeTargets();
        }
        S->GetRailPath()->bPreviewEntireRoute=true;
        S->StartRoutePreview();
        if (Controller->PlayerCameraManager) Controller->PlayerCameraManager->StopCameraFade();
    }
    bool BeginCombat()
    {
        auto* S=Spline.Get(); auto* PC=Controller.Get(); auto* P=Player.Get();
        S->GetRailPath()->bPreviewEntireRoute=false;
        S->RailState=UOBPlayerSplineComponent::EOBRailState::Action;
        P->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        PC->ResetIgnoreMoveInput(); PC->ResetIgnoreLookInput();
        if (bTutorial)
        {
            auto* D=Director.Get(); D->SetActorTickEnabled(true);
            FString Error;
            if (!D->EnterQACheckpoint(TEXT("Shooting1"),Error)) { Finish(false,*Error); return false; }
            PC->SetTrainingControls(true,true,true,false);
        }
        else
        {
            // A fixed reproducible combat load in the saved Stage map. It is NOT a
            // performance claim for the still-unconfigured production enemy waves.
            S->CurrentDistance=S->TargetDistance=500.f;
            S->ApplySplineTransform(500.f);
            P->GetActionComponent()->SetExternalActionState(true);
            PC->SetTrainingControls(true,true,true,false);
            PC->SetViewTarget(P);
            if (Enemies.IsEmpty())
            {
                UClass* Class=LoadClass<AOBEnemyCharacter>(nullptr,TEXT("/Game/Blueprints/Enemy/BP_EnemyCharacter.BP_EnemyCharacter_C"));
                if (!Class) { Finish(false,TEXT("enemy class not cooked")); return false; }
                const FVector Forward=P->GetActorForwardVector(),Right=P->GetActorRightVector();
                auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World.Get());
                for(int32 I=0;I<3;++I)
                {
                    FVector Position=P->GetActorLocation()+Forward*500.f+Right*((I-1)*180.f);
                    FNavLocation N;
                    if (Nav && Nav->ProjectPointToNavigation(Position,N,FVector(200,200,300)))
                        Position=N.Location+FVector(0,0,Class->GetDefaultObject<AOBEnemyCharacter>()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3);
                    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
                    auto* E=World->SpawnActor<AOBEnemyCharacter>(Class,Position,(P->GetActorLocation()-Position).Rotation(),Params);
                    if (!E) { Finish(false,TEXT("combat fixture spawn failed")); return false; }
                    E->SetCombatState(EOBEnemyCombatState::Attacking);
                    Enemies.Add(E);
                }
                Metadata+=TEXT("combat_fixture=3 original BP_EnemyCharacter; timed FireBullet every 0.7s; no authored wave or StateTree claim\n");
            }
        }
        PC->SetTrainingAmmo(30,30);
        return true;
    }
    void Shoot()
    {
        auto* PC=Controller.Get(); auto* P=Player.Get();
        if (PC->GetCurrentAmmo()<=0) PC->CompleteReload();
        FVector Start; FRotator Rot; PC->GetPlayerViewPoint(Start,Rot);
        FVector Aim=Start+Rot.Vector()*10000;
        if (bTutorial)
        {
            auto* D=Director.Get();
            // Hit only one board: the other two stay active, preserving the actual
            // target rail/popup exercise for a full timed sample without advancing.
            if (D->TimedTargets.Num()>0 && D->TimedTargets[0])
                Aim=D->TimedTargets[0]->BoardMesh->Bounds.Origin;
        }
        else
        {
            // Aim adjacent to the enemies to retain all three for comparable passes.
            if (Enemies.Num()) Aim=Enemies[0]->GetActorLocation()+FVector(0,0,180);
            for(auto Ref:Enemies) if (auto* E=Ref.Get())
            { E->SetCombatState(EOBEnemyCombatState::Attacking); E->FireBullet(); ++EnemyShots; }
        }
        PC->FireAlongRay(Start,Aim+(Aim-Start).GetSafeNormal()*100);
        ++Shots;
    }
    void Sample()
    {
        if (bFinished || Phase<0 || !Player.IsValid()) return;
        const double Now=FPlatformTime::Seconds();
        const auto Stats=PipelineStateCache::GetPSORuntimeCreationStats();
        if(LastFrame>0)
            Samples.Add({Phase,Now-Started,(Now-LastFrame)*1000,FPlatformTime::ToMilliseconds(GGameThreadTime),
                FPlatformTime::ToMilliseconds(GRenderThreadTime),FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0)),
                Player->GetActorLocation(),Stats.TotalPSOCreations,Stats.GraphicsPSOHitches,Stats.ComputePSOHitches,
                GEngine->GameViewport->Viewport->HasFocus()?1:0,Spline->IsMoving()?1:0,Shots,EnemyShots});
        LastFrame=Now;
    }
    void Finish(bool OK,const TCHAR* Reason)
    {
        if(bFinished)return; bFinished=true;
        FCoreDelegates::OnEndFrame.Remove(FrameHandle);
        const FString Dir=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Tests/PackagePerformance")/Map);
        IFileManager::Get().MakeDirectory(*Dir,true);
        FString CSV=TEXT("phase,time_s,frame_ms,game_ms,draw_ms,gpu_ms,x,y,z,pso_creations,pso_graphics_hitches,pso_compute_hitches,focused,moving,player_shots,enemy_shots\n");
        for(const auto& S:Samples)
            CSV+=FString::Printf(TEXT("%s,%.6f,%.6f,%.6f,%.6f,%.6f,%.2f,%.2f,%.2f,%u,%u,%u,%d,%d,%d,%d\n"),
                PhaseName(S.Phase),S.Time,S.Frame,S.Game,S.Draw,S.GPU,S.Location.X,S.Location.Y,S.Location.Z,
                S.PSOs,S.GraphicsHitches,S.ComputeHitches,S.Focus,S.Moving,S.Shots,S.EnemyShots);
        FFileHelper::SaveStringToFile(CSV,*(Dir/TEXT("frames.csv")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        Metadata+=FString::Printf(TEXT("completed=%d\nreason=%s\nplayer_shot_requests=%d\nenemy_shot_requests=%d\nresolved_hits=%d\nframes=%d\n"),OK,Reason,Shots,EnemyShots,ResolvedHits,Samples.Num());
        FFileHelper::SaveStringToFile(Metadata,*(Dir/TEXT("metadata.txt")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        UE_LOG(LogTemp,Display,TEXT("PACKAGE_PERF outcome=%s reason=%s frames=%d output=%s"),OK?TEXT("PASS"):TEXT("FAIL"),Reason,Samples.Num(),*Dir);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([OK](float){FPlatformMisc::RequestExitWithStatus(false,OK?0:1);return false;}),1.f);
    }
    bool Tick(float)
    {
        if(bFinished)return false;
        const double Now=FPlatformTime::Seconds();
        if(Now-Started>360){Finish(false,TEXT("timeout"));return false;}
        if(Phase<0)
        {
            UWorld* W=nullptr;
            for(const auto& C:GEngine->GetWorldContexts()) if(C.WorldType==EWorldType::Game && C.World() && C.World()->HasBegunPlay())W=C.World();
            auto* PC=W?Cast<AOBPlayerController>(W->GetFirstPlayerController()):nullptr;
            auto* P=PC?Cast<AOBPlayerCharacter>(PC->GetPawn()):nullptr;
            if(!P || !P->GetPlayerSplineComponent()->GetRailPath())return true;
            Map=W->GetMapName(); bTutorial=Map.Contains(TEXT("TutorialMap"));
            if(!bTutorial && !Map.Contains(TEXT("Stage_01"))){Finish(false,TEXT("launch TutorialMap or Stage_01 directly"));return false;}
            AOBTrainingDirector* D=nullptr;
            for(TActorIterator<AOBTrainingDirector> It(W);It;++It)D=*It;
            if(bTutorial && (!D || !D->IsReadyForQA()))return true;
            World=W;Controller=PC;Player=P;Spline=P->GetPlayerSplineComponent();Director=D;
            auto* Settings=GEngine->GetGameUserSettings();
            const auto Quality=Scalability::GetQualityLevels();
            const auto Window=GEngine->GameViewport->GetWindow();
            const FIntPoint Size=GEngine->GameViewport->Viewport->GetSizeXY();
            if(!Window.IsValid() || Window->GetWindowMode()!=EWindowMode::Fullscreen || Quality.GetSingleQualityLevel()!=1 || CVarInt(TEXT("r.PSOPrecache.GlobalShaders"))!=0)
            {Finish(false,TEXT("requires fullscreen / Medium / global PSO precache off"));return false;}
            Metadata=FString::Printf(TEXT("map=%s\nresolution=%dx%d\nresolution_quality=%.3f\nmedium=%d\nfullscreen=%d\nglobal_shader_pso_precache=%d\nmaterial_pso_precache=%d\nvsync=%d\nmaxfps=%d\nframe_limit=%.2f\nroute_length_cm=%.2f\nroute_speed_cm_s=%.2f\nGPU=%s\nCPU=%s\n"),
                *Map,Size.X,Size.Y,Quality.ResolutionQuality,Quality.GetSingleQualityLevel(),int32(Window->GetWindowMode()),
                CVarInt(TEXT("r.PSOPrecache.GlobalShaders")),CVarInt(TEXT("r.PSOPrecaching")),CVarInt(TEXT("r.VSync")),CVarInt(TEXT("t.MaxFPS")),Settings->GetFrameRateLimit(),
                Spline->GetRailPath()->GetSpline()->GetSplineLength(),Spline->GetRailPath()->RoutePreviewSpeed,*FPlatformMisc::GetPrimaryGPUBrand(),*FPlatformMisc::GetCPUBrand());
            int32 Allies=0,Spawners=0,EnemyCount=0;
            for(TActorIterator<AOBAllyCharacter> It(W);It;++It)++Allies;
            for(TActorIterator<AOBEncounterSpawner> It(W);It;++It)++Spawners;
            for(TActorIterator<AOBEnemyCharacter> It(W);It;++It)++EnemyCount;
            Metadata+=FString::Printf(TEXT("initial_allies=%d\ninitial_enemy_spawners=%d\ninitial_enemies=%d\nPSO_hitch_threshold_ms=%d\ncache_condition=existing driver cache retained; fresh process and UserDir; repeat pass in same process\n"),Allies,Spawners,EnemyCount,CVarInt(TEXT("r.PSO.RuntimeCreationHitchThreshold")));
            UE_LOG(LogTemp,Display,TEXT("PACKAGE_PERF settings %s"),*Metadata);
            PC->OnShotResolved.AddLambda([Weak=TWeakPtr<FOBPackagePerformance>(AsShared())](const FHitResult& Hit){if(auto Self=Weak.Pin())if(Hit.bBlockingHit)++Self->ResolvedHits;});
            Samples.Reserve(100000);
            BeginRoute(); BeginPhase(0);
            FrameHandle=FCoreDelegates::OnEndFrame.AddSP(AsShared(),&FOBPackagePerformance::Sample);
            return true;
        }
        if(Phase<2 && Spline->HasFinishedRoutePreview())
        {
            if(Phase==0){BeginRoute();BeginPhase(1);}
            else {BeginPhase(2); if(!BeginCombat())return false;}
        }
        else if(Phase>=2)
        {
            if(Now>=NextShot){Shoot();NextShot=Now+.7;}
            if(Now-PhaseStart>=30)
            {
                if(Phase==2){BeginPhase(3); if(!BeginCombat())return false;}
                else {Finish(true,TEXT("two route passes and two 30-second combat samples"));return false;}
            }
        }
        return true;
    }
public:
    static void Start()
    {
        static bool Running=false;if(Running)return;Running=true;
        auto S=MakeShared<FOBPackagePerformance>();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float DT){return S->Tick(DT);}),0.f);
    }
};
static FAutoConsoleCommand GOBPackagePerformance(TEXT("OB.QA.PackagePerformance"),
    TEXT("Opt-in cooked Development benchmark: two spline passes and two combat samples; writes CSV and exits."),
    FConsoleCommandDelegate::CreateStatic(&FOBPackagePerformance::Start));
#endif

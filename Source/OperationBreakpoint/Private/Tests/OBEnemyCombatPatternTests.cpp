#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "AI/OBAIController.h"
#include "AI/OBEnemyCombatPattern.h"
#include "Animation/AnimSequence.h"
#include "Animation/OBEnemyAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Character/OBEnemyCharacter.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBActionComponent.h"
#include "Core/OBPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Stage/OBStageFlowComponent.h"
#include "StateTree.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeInstanceData.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBEnemyCombatPatternTest,"OperationBreakpoint.Combat.FiveStateTreePatterns",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOBEnemyCombatPatternTest::RunTest(const FString&)
{
    TGuardValue<bool> ScriptGuard(GAllowActorScriptExecutionInEditor,true);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));World->SetGameMode(URL);
    ON_SCOPE_EXIT { World->DestroyWorld(false);GEngine->DestroyWorldContext(World); };
    auto* Set=LoadObject<UOBEnemyCombatPatternSet>(nullptr,TEXT("/Game/Blueprints/Enemy/Patterns/DA_EnemyCombatPatterns.DA_EnemyCombatPatterns"));
    if (!TestNotNull(TEXT("Saved pattern set loads"),Set)) return false;
    TestEqual(TEXT("Five independently editable StateTrees are saved"),Set->Trees.Num(),5);
    TestEqual(TEXT("The saved rifle burst has no inter-shot wait"),Set->RifleShotInterval,0.f);
    for (auto* Sequence : {Set->StandAim.Get(),Set->CrouchAim.Get(),Set->ProneAim.Get(),Set->CrouchEntry.Get(),Set->CrouchExit.Get(),Set->RifleFire.Get(),Set->RifleReload.Get()})
    {
        if (!TestNotNull(TEXT("Each required pose / action animation is assigned"),Sequence)) return false;
        TestTrue(TEXT("Every pattern uses MM_ soldier animations"),Sequence->GetName().StartsWith(TEXT("MM_")));
        TestTrue(TEXT("All pattern animations share the soldier skeleton"),Sequence->GetSkeleton()==Set->StandAim->GetSkeleton());
    }
    auto* PC=World->SpawnActor<AOBPlayerController>();World->AddController(PC);
    auto* Player=World->SpawnActor<AOBPlayerCharacter>(FVector(0,0,100),FRotator::ZeroRotator);PC->Possess(Player);
    Player->GetActionComponent()->SetExternalActionState(true);
    auto* Stage=Player->GetStageFlowComponent();Stage->Player=Player;Stage->bActive=true;Stage->Phase=EOBStagePhase::Combat;
    int32 Fixture=0;
    for (const auto& Pair : Set->Trees)
    {
        const bool RPG=Pair.Key==EOBEnemyCombatPattern::RPG;
        const auto* Path=RPG ? TEXT("/Game/Blueprints/Enemy/BP_RPGTrooper.BP_RPGTrooper_C") : TEXT("/Game/Blueprints/Enemy/BP_EnemyCharacter.BP_EnemyCharacter_C");
        auto* Class=LoadClass<AOBEnemyCharacter>(nullptr,Path);
        if (!TestNotNull(TEXT("Production enemy Blueprint loads"),Class)) return false;
        auto* Enemy=World->SpawnActor<AOBEnemyCharacter>(Class,FVector(2000.f,500.f*Fixture++,100.f),FRotator::ZeroRotator);
        Enemy->SetCanBeDamaged(true);Enemy->CombatPatterns=Set;
        Enemy->EncounterBehavior.EntryMode=EOBEnemyEntryMode::WaitAtSpawn;
        Enemy->EncounterBehavior.CombatPattern=Pair.Key;
        Enemy->EncounterBehavior.bFacePlayer=false;
        if (!Enemy->GetController()) Enemy->SpawnDefaultController();
        auto* AI=Cast<AOBAIController>(Enemy->GetController());
        if (!TestNotNull(TEXT("Production AI controller exists"),AI)) return false;
        Enemy->StartEncounterBehavior();
        TestFalse(TEXT("Spawning before arrival does not start a pattern"),Enemy->IsCombatPatternActive());
        AI->PrepareEncounterEntry(Enemy->GetActorTransform(),Enemy->GetActorTransform());
        auto* Tree=Pair.Value.Get();
        TestTrue(TEXT("Saved tree is compiled and executable"),Tree && Tree->IsReadyToRun());
        if (!Tree || !Tree->IsReadyToRun()) return false;
        Enemy->BeginCombatPattern();
        FStateTreeInstanceData Instance;
        FStateTreeExecutionContext Context(*Enemy,*Tree,Instance);
        Context.SetContextDataByName(TEXT("Actor"),FStateTreeDataView(Enemy));
        Context.SetContextDataByName(TEXT("AIController"),FStateTreeDataView(AI));
        TestTrue(TEXT("The actual saved StateTree starts"),Context.Start()==EStateTreeRunStatus::Running);
        auto* Body=Enemy->GetMesh();
        Body->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        Body->bEnableUpdateRateOptimizations=false;
        auto* Anim=Cast<UOBEnemyAnimInstance>(Body->GetAnimInstance());
        if (!TestNotNull(TEXT("Production enemy animation instance evaluates the shot pose"),Anim)) return false;
        // This isolated world has no BeginPlay/physics tick. Explicitly establish the same
        // initialized, grounded staging state that the real Sandbox preview already provides.
        Enemy->GetCharacterMovement()->StopMovementImmediately();Enemy->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        Anim->NativeInitializeAnimation();
        int32 Bursts=0,BurstStart=0,ObservedShots=0,ShotIntervals=0,AnimationStarts=0,AnimationCompletions=0;
        bool SawCrouch=false,SawStand=false,SawBetweenShotsAim=false;
        float TreeSeconds=0,PreviousShotTime=-1;
        auto Previous=Enemy->GetPatternAction();
        for (int32 Tick=0;Tick<1600 && Bursts<2;++Tick)
        {
            const auto PreviousPose=Anim->GetPatternPose();
            Context.Tick(.05f);
            TreeSeconds+=.05f;
            const auto Action=Enemy->GetPatternAction();
            const int32 CurrentShots=Enemy->GetPatternShotsFired();
            if (CurrentShots!=ObservedShots)
            {
                TestEqual(TEXT("A tick cannot release several queued shots together"),CurrentShots-ObservedShots,1);
                if (!RPG && ObservedShots>BurstStart && ObservedShots<BurstStart+5)
                {
                    const float Gap=TreeSeconds-PreviousShotTime;
                    TestTrue(TEXT("A rifle animation finishes before the next projectile, with no additional one-second wait"),Gap>=Set->RifleFire->GetPlayLength()-.001f && Gap<=Set->RifleFire->GetPlayLength()+.051f);
                    TestTrue(TEXT("The preceding evaluated shot animation reached its end before being restarted"),PreviousPose.Action==Set->RifleFire && PreviousPose.ActionTime>=Set->RifleFire->GetPlayLength()-.051f);
                    ++AnimationCompletions;
                    ++ShotIntervals;
                }
                if (!RPG)
                {
                    const auto ShotPose=Anim->GetPatternPose();
                    TestTrue(TEXT("Each projectile starts exactly one fresh rifle animation playback"),ShotPose.Action==Set->RifleFire && FMath::IsNearlyZero(ShotPose.ActionTime));
                    ++AnimationStarts;
                }
                TestEqual(TEXT("Each real projectile uses its fixed role chance"),Enemy->GetLastShotAimChancePercent(),RPG ? 25 : 2);
                PreviousShotTime=TreeSeconds;ObservedShots=CurrentShots;
            }
            if (!RPG && Action==EOBEnemyPatternAction::Aim && CurrentShots>BurstStart && CurrentShots<BurstStart+5) SawBetweenShotsAim=true;
            const auto Posture=Enemy->GetCombatPosture();
            SawCrouch|=Posture==EOBEnemyPosture::Crouch;SawStand|=Posture==EOBEnemyPosture::Stand;
            if (Pair.Key==EOBEnemyCombatPattern::Crouch || RPG) TestTrue(TEXT("Crouch remains active while aiming, firing and reloading"),Posture==EOBEnemyPosture::Crouch);
            if (Pair.Key==EOBEnemyCombatPattern::Prone) TestTrue(TEXT("Prone remains active while aiming, firing and reloading"),Posture==EOBEnemyPosture::Prone);
            if (Pair.Key==EOBEnemyCombatPattern::Stand) TestTrue(TEXT("Stand remains active throughout its loop"),Posture==EOBEnemyPosture::Stand);
            if (Action==EOBEnemyPatternAction::Fire)
            {
                const int32 Count=Enemy->GetPatternShotsFired();Enemy->FireBullet();
                TestEqual(TEXT("An animation notify cannot add a sixth rifle shot / duplicate rocket"),Enemy->GetPatternShotsFired(),Count);
            }
            if (Previous==EOBEnemyPatternAction::Fire && Action==EOBEnemyPatternAction::Reload)
            {
                TestEqual(TEXT("Each real projectile burst contains exactly the requested shot count"),Enemy->GetPatternShotsFired()-BurstStart,RPG ? 1 : 5);
                if (!RPG)
                {
                    TestTrue(TEXT("The final recoil completes before reload"),TreeSeconds-PreviousShotTime>=Set->RifleFire->GetPlayLength()-.051f);
                    TestTrue(TEXT("The fifth evaluated shot animation reaches its end before reload"),PreviousPose.Action==Set->RifleFire && PreviousPose.ActionTime>=Set->RifleFire->GetPlayLength()-.051f);
                    ++AnimationCompletions;
                }
                BurstStart=Enemy->GetPatternShotsFired();++Bursts;
            }
            Previous=Action;
            ++GFrameCounter;Body->TickAnimation(.05f,false);Body->RefreshBoneTransforms();
        }
        TestEqual(TEXT("The living enemy completes at least two real StateTree attack loops"),Bursts,2);
        if (!RPG)
        {
            TestFalse(TEXT("Rifle bursts never insert an aim wait between their five rounds"),SawBetweenShotsAim);
            TestTrue(TEXT("Rifle timing was measured across repeated bursts"),ShotIntervals>=8);
            TestEqual(TEXT("Two five-round bursts start ten distinct animation playbacks"),AnimationStarts,10);
            TestEqual(TEXT("All ten animation playbacks finish without being cut off"),AnimationCompletions,10);
        }
        if (Pair.Key==EOBEnemyCombatPattern::WindowCover) TestTrue(TEXT("Window cover alternates crouching and standing between bursts"),SawCrouch && SawStand);
        Stage->Phase=EOBStagePhase::SupportingFire;
        Context.Tick(.05f);
        TestFalse(TEXT("Leaving combat stops the pattern immediately"),Enemy->IsCombatPatternActive());
        const int32 Before=Enemy->GetPatternShotsFired();Context.Tick(5.f);
        TestEqual(TEXT("No shot survives the stage combat gate"),Enemy->GetPatternShotsFired(),Before);
        Context.Stop();Stage->Phase=EOBStagePhase::Combat;
        Enemy->BeginCombatPattern();Context.Start();
        Enemy->SetCombatState(EOBEnemyCombatState::Dead);Context.Tick(.1f);
        TestFalse(TEXT("Death interrupts every pattern, including reload"),Enemy->IsCombatPatternActive());
        Context.Stop();AI->StopStateTree();Enemy->Destroy();
    }
    // Run the same saved tree concurrently. Each enemy must draw fresh waits on each loop.
    TestTrue(TEXT("Production patterns enable per-enemy timing variation"),Set->bRandomizeTiming);
    struct FTimedEnemy
    {
        AOBEnemyCharacter* Enemy=nullptr;
        FStateTreeInstanceData Instance;
        TUniquePtr<FStateTreeExecutionContext> Context;
        TArray<float> BurstStarts;
    };
    TArray<TUniquePtr<FTimedEnemy>> Cohort;
    auto* RifleClass=LoadClass<AOBEnemyCharacter>(nullptr,TEXT("/Game/Blueprints/Enemy/BP_EnemyCharacter.BP_EnemyCharacter_C"));
    auto* StandTree=Set->Trees[EOBEnemyCombatPattern::Stand].Get();
    for (int32 I=0;I<8;++I)
    {
        auto FixtureState=MakeUnique<FTimedEnemy>();
        auto* E=World->SpawnActor<AOBEnemyCharacter>(RifleClass,FVector(3000,I*300,100),FRotator::ZeroRotator);
        FixtureState->Enemy=E;E->CombatPatterns=Set;E->SetCanBeDamaged(true);
        E->EncounterBehavior.EntryMode=EOBEnemyEntryMode::WaitAtSpawn;
        if (!E->GetController()) E->SpawnDefaultController();
        auto* Controller=CastChecked<AOBAIController>(E->GetController());
        Controller->PrepareEncounterEntry(E->GetActorTransform(),E->GetActorTransform());E->BeginCombatPattern();
        FixtureState->Context=MakeUnique<FStateTreeExecutionContext>(*E,*StandTree,FixtureState->Instance);
        FixtureState->Context->SetContextDataByName(TEXT("Actor"),FStateTreeDataView(E));
        FixtureState->Context->SetContextDataByName(TEXT("AIController"),FStateTreeDataView(Controller));
        FixtureState->Context->Start();Cohort.Add(MoveTemp(FixtureState));
    }
    for (int32 Tick=1;Tick<1600;++Tick)
    {
        bool Complete=true;
        for (const auto& Entry : Cohort)
        {
            Entry->Context->Tick(.05f);
            const int32 Shots=Entry->Enemy->GetPatternShotsFired();
            if ((Shots==1 && Entry->BurstStarts.IsEmpty()) || (Shots==6 && Entry->BurstStarts.Num()==1)) Entry->BurstStarts.Add(Tick*.05f);
            Complete &= Entry->BurstStarts.Num()==2;
        }
        if (Complete) break;
    }
    float FirstMin=MAX_flt,FirstMax=0,LoopMin=MAX_flt,LoopMax=0;
    for (const auto& Entry : Cohort)
    {
        if (TestEqual(TEXT("Each concurrent enemy completes two burst starts"),Entry->BurstStarts.Num(),2))
        {
            FirstMin=FMath::Min(FirstMin,Entry->BurstStarts[0]);FirstMax=FMath::Max(FirstMax,Entry->BurstStarts[0]);
            const float Loop=Entry->BurstStarts[1]-Entry->BurstStarts[0];
            LoopMin=FMath::Min(LoopMin,Loop);LoopMax=FMath::Max(LoopMax,Loop);
        }
        Entry->Context->Stop();Entry->Enemy->StopCombatPattern();
    }
    TestTrue(TEXT("Identical enemies do not begin firing together"),FirstMax-FirstMin>.01f);
    TestTrue(TEXT("Subsequent attack loops have different lengths, not just a one-time offset"),LoopMax-LoopMin>.01f);
    AddInfo(FString::Printf(TEXT("Eight same-tree enemies: first shot %.2f..%.2fs; next burst gap %.2f..%.2fs"),FirstMin,FirstMax,LoopMin,LoopMax));
    Stage->bActive=false;
    return !HasAnyErrors();
}
#endif

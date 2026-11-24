#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Character/OBActionComponent.h"
#include "Character/OBEnemyCharacter.h"
#include "Stage/OBStageFlowComponent.h"
#include "AI/OBAIController.h"
#include "World/OBEncounterSpawner.h"
#include "World/OBSquadRendezvous.h"
#include "World/OBAllyRoute.h"
#include "Character/OBAllyCharacter.h"
#include "Character/OBAllySplineComponent.h"
#include "Sound/SoundWave.h"
#include "Core/OBPlayerController.h"
#include "StateTree.h"
#include "Components/StateTreeAIComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBStageFlowTest,"OperationBreakpoint.Stage.FlowAndData",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOBStageFlowTest::RunTest(const FString&)
{
    auto* W=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
    W->SetGameInstance(NewObject<UGameInstance>(GEngine));
    FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));W->SetGameMode(URL);
    ON_SCOPE_EXIT { W->DestroyWorld(false);GEngine->DestroyWorldContext(W); };
    auto* PC=W->SpawnActor<AOBPlayerController>();W->AddController(PC);auto* Player=W->SpawnActor<AOBPlayerCharacter>();PC->Possess(Player);
    auto* Stage=Player->GetStageFlowComponent();Stage->Player=Player;Stage->bActive=true;
    Stage->LoadedScripts=Stage->ScriptTable.LoadSynchronous();
    TestNotNull(TEXT("Stage script asset loads"),Stage->LoadedScripts.Get());
    if (Stage->LoadedScripts)
    {
        TestEqual(TEXT("All 32 worksheet IDs preserved including empty reserved rows"),Stage->LoadedScripts->GetRowNames().Num(),32);
        float PreviousEnd=0;
        for (int Id=2001;Id<=2004;++Id)
        {
            const auto* Row=Stage->FindScript(Id);
            TestTrue(TEXT("Opening subtitle has nonoverlapping explicit voice-clock timing"),Row && !Row->Text.IsEmpty() && Row->OpeningStartSeconds>=PreviousEnd && Row->OpeningEndSeconds>Row->OpeningStartSeconds);
            if (Row) PreviousEnd=Row->OpeningEndSeconds;
        }
        for (int Id : {2019,2020,2021})
        { const auto* Row=Stage->FindScript(Id);TestTrue(TEXT("Support text exists with an editable optional voice"),Row && !Row->Text.IsEmpty()); }
    }
    // Exercise the actual queue without an audio device: delay, context changes, final sequencing,
    // and outcome playback must all finish rather than overlap or hang in headless runs.
    auto* RealScripts=Stage->LoadedScripts.Get();
    auto* TestScripts=NewObject<UDataTable>();TestScripts->RowStruct=FOBStageScriptRow::StaticStruct();
    for (int Id : {2005,2006,2007,2008,2016,2017,2018,2022,2023})
    {
        FOBStageScriptRow Row;Row.Text=FText::FromString(FString::FromInt(Id));Row.Display_Time=.2f;Row.Delay_Time=Id==2005 ? .5f : 0.f;
        TestScripts->AddRow(FName(*FString::FromInt(Id)),Row);
    }
    Stage->LoadedScripts=TestScripts;Stage->Phase=EOBStagePhase::Travel;
    Stage->HandleApproachToStop(7,2.1f);
    TestEqual(TEXT("Arrival stays silent outside the lead window"),Stage->ActiveScriptId,0);
    Stage->HandleApproachToStop(8,1.f);
    TestEqual(TEXT("Passing an unrelated spline point stays silent"),Stage->ActiveScriptId,0);
    Stage->HandleApproachToStop(7,1.9f);
    TestEqual(TEXT("The arrival sentence starts before reaching its stop"),Stage->ActiveScriptId,2006);
    Stage->HandleApproachToStop(7,1.f);Stage->HandleActionPoint(7);
    TestTrue(TEXT("Approach and arrival share one automatic sentence"),Stage->ActiveScriptId==2006 && Stage->NarrationQueue.IsEmpty());
    Stage->CancelNarration();Stage->AutomaticScriptIds.Reset();Stage->Phase=EOBStagePhase::ShortcutTransition;
    Stage->HandleApproachToStop(19,1.f);
    TestEqual(TEXT("Shortcut blackout cannot play a skipped arrival"),Stage->ActiveScriptId,0);
    Stage->Phase=EOBStagePhase::Travel;
    Stage->PlayAutomaticScript(2005,1);Stage->PlayAutomaticScript(2006,1);Stage->PlayAutomaticScript(2006,1);
    TestEqual(TEXT("Repeated automatic callbacks do not duplicate pending dialogue"),Stage->NarrationQueue.Num(),1);
    Stage->TickNarration(.25f);TestFalse(TEXT("Delay_Time delays both voice and subtitle start"),Stage->bNarrationStarted);
    Stage->TickNarration(.3f);TestTrue(TEXT("Narration begins after its actual delay expires"),Stage->bNarrationStarted);
    Stage->PlayAutomaticScript(2007,2);Stage->PlayAutomaticScript(2008,1);Stage->DiscardOldNarration(2);
    TestEqual(TEXT("Changing zones does not cut the active spoken sentence"),Stage->ActiveScriptId,2005);
    TestEqual(TEXT("Unstarted dialogue from a passed zone is discarded"),Stage->NarrationQueue.Num(),1);
    Stage->TickNarration(.21f);TestEqual(TEXT("Voice-less fallback drains to the next current-context line"),Stage->ActiveScriptId,2007);
    Stage->TickNarration(.21f);TestEqual(TEXT("Voice-less lines finish without an audio callback"),Stage->ActiveScriptId,0);
    Stage->PlayAutomaticScript(2017,6);Stage->PlayAutomaticScript(2018,6);Stage->PlayAutomaticScript(2016,6);Stage->PlayAutomaticScript(2017,6);
    TestEqual(TEXT("Queued hostage line begins without cutting another line"),Stage->ActiveScriptId,2017);
    TestEqual(TEXT("The generic narration queue preserves both remaining lines"),Stage->NarrationQueue.Num(),2);
    Stage->TickNarration(.21f);TestEqual(TEXT("Terrorist follows hostage without interrupting it"),Stage->ActiveScriptId,2018);
    Stage->TickNarration(.21f);TestEqual(TEXT("Arrival follows the hostage and terrorist lines"),Stage->ActiveScriptId,2016);
    Stage->CancelNarration();TestEqual(TEXT("Cancellation removes active and queued narration"),Stage->ActiveScriptId+Stage->NarrationQueue.Num(),0);
    Stage->Phase=EOBStagePhase::Ending;Stage->bSucceeded=true;Stage->bEndingVideoOpened=false;Stage->FinishEndingVideo();
    TestTrue(TEXT("An unavailable ending video uses its outcome caption fallback"),Stage->Phase==EOBStagePhase::Ending && Stage->ActiveScriptId==2022);
    Stage->TickNarration(.21f);TestTrue(TEXT("The fallback caption completes before the results notice"),Stage->Phase==EOBStagePhase::Complete);
    Stage->Phase=EOBStagePhase::Ending;Stage->bSucceeded=false;Stage->FinishEndingVideo();
    TestEqual(TEXT("Failure uses its own outcome line"),Stage->ActiveScriptId,2023);
    Stage->TickNarration(.21f);TestTrue(TEXT("Missing outcome audio cannot block the results notice"),Stage->Phase==EOBStagePhase::Complete);
    Stage->Phase=EOBStagePhase::Ending;Stage->bSucceeded=true;Stage->bEndingVideoOpened=true;Stage->FinishEndingVideo();
    TestTrue(TEXT("A played ending movie proceeds straight to results without repeating its caption"),Stage->Phase==EOBStagePhase::Complete && Stage->ActiveScriptId==0);
    // The following cases model a fresh in-progress mission, independently of the completed success movie above.
    Stage->ClearPresentation();Stage->AutomaticScriptIds.Reset();Stage->CurrentZone=0;Stage->Phase=EOBStagePhase::Travel;Stage->bSucceeded=false;
    const auto InitialMovementMode=Player->GetCharacterMovement()->MovementMode;
    TestTrue(TEXT("The initial move is deferred while its departure order plays"),Stage->DeferInitialTravelUntilNarrationDone());
    TestTrue(TEXT("First departure waits with action input and character physics blocked"),Stage->Phase==EOBStagePhase::AwaitingDeparture && Stage->IsInputBlocked() && Player->GetCharacterMovement()->MovementMode==MOVE_None);
    TestEqual(TEXT("The first order is spoken before travel"),Stage->ActiveScriptId,2005);
    Stage->DeferInitialTravelUntilNarrationDone();TestEqual(TEXT("Duplicate initial requests do not enqueue the first order again"),Stage->NarrationQueue.Num(),0);
    auto* Squad=W->SpawnActor<AOBSquadRendezvous>();Squad->bUseNavigation=false;
    Squad->bDepartDuringNarration=true;Squad->DepartAfterZone=0;Squad->bStageOnArrival=false;
    auto& Member=Squad->Members.AddDefaulted_GetRef();Member.Ally=W->SpawnActor<AOBAllyCharacter>();Member.Route=W->SpawnActor<AOBAllyRoute>();
    Member.Ally->GetRouteComponent()->bSweepMovement=false;
    static_cast<AActor*>(Squad)->Tick(.01f); // Bind to the actual player departure delegate.
    TestTrue(TEXT("Squad can be staged before the delayed departure dialogue"),Squad->StageForPlayer(Player));
    const FVector BeforeDialogue=Member.Ally->GetActorLocation();
    static_cast<AActor*>(Squad)->Tick(.1f);
    static_cast<UActorComponent*>(Member.Ally->GetRouteComponent())->TickComponent(.1f,LEVELTICK_All,&Member.Ally->GetRouteComponent()->PrimaryComponentTick);
    TestFalse(TEXT("Old move-during-narration flags cannot start the squad"),Squad->IsSectionStarted());
    TestTrue(TEXT("Squad stays in place while departure dialogue is pending"),Member.Ally->GetActorLocation().Equals(BeforeDialogue));
    Stage->TickNarration(.51f);Stage->TickNarration(.21f);
    TestTrue(TEXT("First departure releases only after the delayed line finishes"),Stage->Phase==EOBStagePhase::Travel && !Stage->bDeparturePending);
    TestEqual(TEXT("Departure release restores character movement mode"),Player->GetCharacterMovement()->MovementMode,InitialMovementMode);
    Player->GetPlayerSplineComponent()->OnTravelStarted.Broadcast();
    TestTrue(TEXT("The squad starts from the player rail departure event after dialogue"),Squad->IsSectionStarted());
    Squad->SetActorTickEnabled(false);Squad->bEnabled=false;
    Stage->CurrentZone=1;Stage->Phase=EOBStagePhase::Combat;
    auto* DepartureAction=Player->GetActionComponent();DepartureAction->SetExternalActionState(true);
    Stage->PlayAutomaticScript(2006,1);DepartureAction->CompleteAction();
    TestTrue(TEXT("CompleteAction preserves the encounter state while dialogue is pending"),Stage->Phase==EOBStagePhase::AwaitingDeparture && DepartureAction->GetActionState()==EOBActionState::Action);
    TestEqual(TEXT("Clear does not interrupt the current arrival sentence"),Stage->ActiveScriptId,2006);
    DepartureAction->CompleteAction();TestEqual(TEXT("Repeated CompleteAction cannot duplicate the departure order"),Stage->NarrationQueue.Num(),1);
    Stage->TickNarration(.21f);TestEqual(TEXT("The clear/departure order follows the active sentence"),Stage->ActiveScriptId,2007);
    TestTrue(TEXT("Action state stays intact through the departure order"),DepartureAction->GetActionState()==EOBActionState::Action);
    Stage->TickNarration(.21f);
    TestTrue(TEXT("Narration completion resumes CompleteAction once and resets its encounter"),Stage->Phase==EOBStagePhase::Travel && DepartureAction->GetActionState()==EOBActionState::Prepare);
    Stage->CurrentZone=2;Stage->Phase=EOBStagePhase::Combat;DepartureAction->SetExternalActionState(true);DepartureAction->CompleteAction();
    TestTrue(TEXT("Missing optional departure rows release instead of deadlocking"),Stage->Phase==EOBStagePhase::Travel && DepartureAction->GetActionState()==EOBActionState::Prepare);
    Stage->CancelNarration();Stage->AutomaticScriptIds.Reset();Stage->LoadedScripts=RealScripts;
    auto* Class=LoadClass<AOBEnemyCharacter>(nullptr,TEXT("/Game/Blueprints/Enemy/BP_EnemyCharacter.BP_EnemyCharacter_C"));
    if (!TestNotNull(TEXT("Real enemy class loads"),Class)) return false;
    auto* Enemy=W->SpawnActor<AOBEnemyCharacter>(Class);Enemy->DispatchBeginPlay();auto* AI=W->SpawnActor<AOBAIController>();AI->Possess(Enemy);
    auto* Spawn=W->SpawnActor<AOBEncounterSpawner>();Spawn->PlacedEnemy=Enemy;Spawn->bMoveToStagingPoint=false;
    Player->SetActorLocation(FVector(0,1000,0));Enemy->SetActorLocation(FVector::ZeroVector);
    Spawn->PrepareEncounter(7);
    TestTrue(TEXT("Stationary arrivals face the player instead of their authored marker yaw"),FMath::Abs(FMath::FindDeltaAngleDegrees(Enemy->GetActorRotation().Yaw,90))<.1f);
    auto* Tree=NewObject<UStateTree>();
    TestFalse(TEXT("Invalid StateTree schema is rejected rather than replacing combat logic"),AI->SetEncounterStateTree(Tree));
    auto* ValidTree=LoadObject<UStateTree>(nullptr,TEXT("/Game/Blueprints/Enemy/ST_EnemyAttackCoverTest.ST_EnemyAttackCoverTest"));
    TestTrue(TEXT("Existing combat StateTree can be assigned per encounter"),AI->SetEncounterStateTree(ValidTree));
    auto* Action=Player->GetActionComponent();Action->BeginEncounter(7);Action->RegisterEnemy(Enemy);Action->FinalizeEncounterSetup();Action->SetExternalActionState(true);Action->bAutoAdvanceOnEnemyClear=false;
    Stage->CurrentZone=1;Stage->Phase=EOBStagePhase::Combat;Stage->RemainingLives=5;
    FDamageEvent Damage;
    for (int I=0;I<5;++I) Player->TakeDamage(20,Damage,nullptr,nullptr);
    TestEqual(TEXT("Five hits exhaust the zone's five lives"),Stage->RemainingLives,0);
    TestTrue(TEXT("Life exhaustion requests support instead of mission failure"),Stage->Phase==EOBStagePhase::SupportingFire && !Stage->bSucceeded);
    TestTrue(TEXT("Only rows 2019-2021 are selected"),Stage->LastSupportScript>=2019 && Stage->LastSupportScript<=2021);
    TestEqual(TEXT("Urgent support voice takes priority over routine narration"),Stage->ActiveScriptId,Stage->LastSupportScript);
    Stage->FireSupportShot();
    TestTrue(TEXT("Support uses normal lethal damage/death and clears the registered combatant"),Enemy->GetCombatState()==EOBEnemyCombatState::Dead && !Action->HasEncounterEnemies());
    TestTrue(TEXT("Support-cleared zones wait for support/departure voices before resetting the action"),Stage->Phase==EOBStagePhase::AwaitingDeparture && Action->GetActionState()==EOBActionState::Action);
    Stage->ClearPresentation();Stage->RestoreOpeningMovement();
    Stage->Phase=EOBStagePhase::Combat;Stage->CurrentZone=3;Stage->RemainingSeconds=.01f;
    Stage->TickComponent(.1f,LEVELTICK_All,&Stage->PrimaryComponentTick);
    TestTrue(TEXT("A deadline with no surviving targets waits on its support and departure narration"),Stage->Phase==EOBStagePhase::AwaitingDeparture && Stage->bDeparturePending);
    TestTrue(TEXT("Deadline support selects an urgent support row"),Stage->LastSupportScript>=2019 && Stage->LastSupportScript<=2021);
    TestEqual(TEXT("The urgent support voice still plays before the queued departure order"),Stage->ActiveScriptId,Stage->LastSupportScript);
    Stage->Phase=EOBStagePhase::Combat;Stage->CurrentZone=6;Stage->RemainingLives=5;
    TestFalse(TEXT("Zone6 never consumes lives"),Stage->ApplyPlayerHit());
    TestEqual(TEXT("Zone6 timer default follows the 10-second design"),Stage->HostageTimeLimit,10.f);
    TestEqual(TEXT("No global time dilation is used"),W->GetWorldSettings()->GetEffectiveTimeDilation(),1.f);
    Stage->OriginalDilations.Add(Player,Player->CustomTimeDilation);Player->CustomTimeDilation=.35f;
    Stage->RestoreSlowMotion();TestEqual(TEXT("Slow motion cleanup restores original per-actor rates"),Player->CustomTimeDilation,1.f);
    Stage->CurrentZone=0;Stage->Phase=EOBStagePhase::Opening;Stage->bVideoFinished=false;
    Stage->bVoiceStarted=true;Stage->bVoiceFinished=false;Stage->Voice=nullptr;
    Stage->VoiceElapsed=Stage->VoiceWatchdogElapsed=Stage->VoicePlaybackCallbackAge=0;
    Stage->TickOpeningVoiceClock(.6f);
    TestTrue(TEXT("Disabled PIE audio still advances the briefing caption clock"),Stage->VoiceElapsed>=.6f && !Stage->bVoiceFinished);
    if (auto* Wave=Cast<USoundWave>(Stage->OpeningVoice.LoadSynchronous()))
    {
        Stage->HandleVoicePlayback(Wave,.1f);const float Reported=Stage->VoiceElapsed;
        Stage->TickOpeningVoiceClock(.1f);
        TestEqual(TEXT("Live audio callbacks retain authority over subtitle timing"),Stage->VoiceElapsed,Reported);
        Stage->TickOpeningVoiceClock(5.f);
        TestTrue(TEXT("A stalled audio callback cannot freeze subtitles indefinitely"),Stage->VoiceElapsed>Reported);
    }
    Stage->bVoiceFinished=true;
    Stage->FinishOpeningIfReady();TestTrue(TEXT("Briefing finishing first does not skip the video"),Stage->Phase==EOBStagePhase::Opening);
    Stage->bVideoFinished=true;Stage->bVoiceFinished=false;Stage->FinishOpeningIfReady();
    TestTrue(TEXT("Video finishing first waits on black for the briefing"),Stage->Phase==EOBStagePhase::Opening);
    Stage->bVoiceFinished=true;Stage->FinishOpeningIfReady();
    TestTrue(TEXT("Opening waits for the first departure order after both media finish"),Stage->Phase==EOBStagePhase::AwaitingDeparture && Stage->ActiveScriptId==2005);
    Stage->SuspendForQA();Stage->HandleActionPoint(Stage->Zone6EncounterPoint);
    TestTrue(TEXT("Camera QA cannot start a deadline/ending"),Stage->Phase==EOBStagePhase::QA && !Stage->HandleEncounterCleared());
    TestTrue(TEXT("QA cancels the pending departure and restores physical movement"),!Stage->bDeparturePending && !Stage->bOpeningMovementSaved && !Stage->ActiveScriptId);
    TestFalse(TEXT("QA input remains governed by the existing camera editor"),Stage->IsInputBlocked());
    Stage->bActive=false;
    return !HasAnyErrors();
}
#endif

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBEnemyCharacter.h"
#include "Character/OBHostageCharacter.h"
#include "Core/OBPlayerController.h"
#include "Stage/OBStageFlowComponent.h"
#include "Engine/DamageEvents.h"
#include "Components/BoxComponent.h"
#include "Training/OBTrainingResultWidget.h"
#include "Components/TextBlock.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBStageResultTest,"OperationBreakpoint.Stage.ResultTracking",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOBStageResultTest::RunTest(const FString&)
{
    auto* W=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
    W->SetGameInstance(NewObject<UGameInstance>(GEngine));
    FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));W->SetGameMode(URL);
    ON_SCOPE_EXIT { W->DestroyWorld(false); GEngine->DestroyWorldContext(W); };
    auto* PC=W->SpawnActor<AOBPlayerController>(); auto* P=W->SpawnActor<AOBPlayerCharacter>(); PC->Possess(P);
    auto* Flow=P->GetStageFlowComponent(); Flow->Player=P; Flow->bActive=true; Flow->Phase=EOBStagePhase::Combat;
    Flow->StartResultTracking(); Flow->StageRecord.bUsedTestInputs=true; Flow->BindResultTracking(); Flow->BindResultTracking();
    TestTrue(TEXT("Stage tracking starts its own session"),Flow->StageRecord.SessionId.IsValid());
    PC->OnShotResolved.Broadcast(FHitResult());
    auto* Enemy=W->SpawnActor<AOBEnemyCharacter>(); Enemy->SetEncounterCombatant(true); FHitResult Hit(Enemy,nullptr,FVector::ZeroVector,FVector::UpVector);
    PC->OnShotResolved.Broadcast(Hit);
    Enemy->SetCanBeDamaged(false); PC->OnShotResolved.Broadcast(Hit);
    Enemy->SetCanBeDamaged(true); Enemy->SetCombatState(EOBEnemyCombatState::Dead); PC->OnShotResolved.Broadcast(Hit);
    auto* Hostage=W->SpawnActor<AOBHostageCharacter>();
    PC->OnShotResolved.Broadcast(FHitResult(Hostage,nullptr,FVector::ZeroVector,FVector::UpVector));
    TestEqual(TEXT("Repeated binding does not duplicate shots"),Flow->StageRecord.ShotsFired,5);
    TestEqual(TEXT("Only damageable living enemy hits count"),Flow->StageRecord.ValidHits,1);
    for (auto Phase:{EOBStagePhase::Opening,EOBStagePhase::Ending,EOBStagePhase::Complete,EOBStagePhase::QA})
    { Flow->Phase=Phase; PC->OnShotResolved.Broadcast(Hit); }
    TestEqual(TEXT("Presentation and QA input is excluded"),Flow->StageRecord.ShotsFired,5);
    Flow->StageRecord.TotalSeconds=378; Flow->StageRecord.bFinished=true;
    Flow->StageRecord.CompletedAtUtc=FDateTime::UtcNow();
    for (bool bRescued:{true,false})
    {
        Flow->bSucceeded=bRescued; Flow->Phase=EOBStagePhase::Ending;
        Flow->ShowCompletion();
        TestEqual(TEXT("Both rescue outcomes end training"),Flow->Phase,EOBStagePhase::Complete);
        FString Text;
        const FString File=FPaths::ProjectSavedDir()/TEXT("Tests/StageResults/Records")/(Flow->StageRecord.SessionId.ToString(EGuidFormats::Digits)+TEXT(".json"));
        TestTrue(TEXT("Details are saved in an isolated test record"),FFileHelper::LoadFileToString(Text,*File));
        TSharedPtr<FJsonObject> Json;
        if (!TestTrue(TEXT("Stage record parses"),FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Json))) return false;
        TestEqual(TEXT("Saved status is neutral completion"),Json->GetStringField(TEXT("status")),FString(TEXT("completed")));
        TestEqual(TEXT("Underlying rescue outcome is preserved"),Json->GetBoolField(TEXT("hostage_rescued")),bRescued);
        TestEqual(TEXT("Saved hit rate excludes hostage hits"),Json->GetNumberField(TEXT("accuracy_percent")),20.);
        TestEqual(TEXT("Score is serialized as a number"),Json->GetNumberField(TEXT("score")),Flow->StageRecord.StageScore);
        TestEqual(TEXT("All six zone slots are preserved"),Json->GetArrayField(TEXT("zones")).Num(),6);
    }
    // The design's worked example, exact time boundaries and support override.
    FOBStageZoneResult Z;Z.Zone=1;Z.bStarted=true;Z.PlannedEnemies=5;
    Z.KillAssessmentSum=3.975;Z.ElapsedSeconds=28;Z.RemainingHearts=4;
    FOBStageScoring::Evaluate(Z);
    TestTrue(TEXT("Design example is 55.65 + 15 + 8 = 78.65"),FMath::IsNearlyEqual(Z.Score,78.65,1.e-8));
    for (const auto& Pair:TArray<TPair<double,double>>{{25,20},{25.0001,15},{30,15},{30.0001,10},{34,10},{34.0001,5},{60,5}})
    { Z.ElapsedSeconds=Pair.Key;FOBStageScoring::Evaluate(Z);TestEqual(TEXT("Unrounded normal-zone time boundary"),Z.TimeScore,Pair.Value); }
    Z.SupportReason=TEXT("time_expired");FOBStageScoring::Evaluate(Z);
    TestTrue(TEXT("Support preserves direct kills and zeros time/hearts"),Z.TimeScore==0 && Z.HeartScore==0 && FMath::IsNearlyEqual(Z.Score,55.65,1.e-8));
    Z=FOBStageZoneResult();Z.Zone=6;Z.bStarted=true;Z.PlannedEnemies=5;Z.KillAssessmentSum=5;Z.bRescued=true;
    for (const auto& Pair:TArray<TPair<double,double>>{{10,30},{7.0001,30},{7,22.5},{5,15},{2,7.5},{.0001,7.5}})
    { Z.RemainingSeconds=Pair.Key;FOBStageScoring::Evaluate(Z);TestEqual(TEXT("Unrounded rescue time boundary"),Z.TimeScore,Pair.Value); }
    Z.bRescued=false;FOBStageScoring::Evaluate(Z);TestEqual(TEXT("Rescue failure zeros even full kill credit"),Z.Score,0.);
    for (const auto& Pair:TArray<TPair<double,FString>>{{90,TEXT("S")},{89.999,TEXT("A")},{80,TEXT("A")},{70,TEXT("B")},{60,TEXT("C")},{59.999,TEXT("D")}})
        TestEqual(TEXT("Rank uses the unrounded average"),FOBStageScoring::Rank(Pair.Key),Pair.Value);

    Flow->Phase=EOBStagePhase::Combat;Flow->CurrentZone=1;Flow->StageRecord.StageZones[0]=FOBStageZoneResult();
    Flow->StartZoneRecord(1,7);Flow->StageRecord.StageZones[0].PlannedEnemies=3;
    auto Shot=[&](AOBEnemyCharacter* Target,const TCHAR* BoxName)
    {
        UBoxComponent* Box=nullptr;TInlineComponentArray<UBoxComponent*> Boxes;Target->GetComponents(Boxes);
        for (auto* B:Boxes) if (B->GetFName()==BoxName) Box=B;
        if (!Box) { AddError(TEXT("Missing native hitbox"));return; }
        FHitResult H(Target,Box,FVector::ZeroVector,FVector::UpVector);
        PC->OnShotResolved.Broadcast(H);
        FPointDamageEvent D(100,H,FVector::ForwardVector,nullptr);Target->TakeDamage(100,D,PC,P);
    };
    auto* Mixed=W->SpawnActor<AOBEnemyCharacter>();Mixed->SetEncounterCombatant(true);
    Shot(Mixed,TEXT("BodyHitBox"));TestEqual(TEXT("Stage body damage is exactly 50"),Mixed->GetCurrentHealth(),50.f);
    Shot(Mixed,TEXT("HeadHitBox"));
    TestTrue(TEXT("Overkill uses only 50 head HP; mixed kill evaluation is 87.5%"),FMath::IsNearlyEqual(Flow->StageRecord.StageZones[0].KillAssessmentSum,.875,1.e-8));
    Shot(Mixed,TEXT("HeadHitBox"));TestEqual(TEXT("Corpse cannot credit a second kill"),Flow->StageRecord.StageZones[0].PlayerKills,1);
    auto* Limb=W->SpawnActor<AOBEnemyCharacter>();Limb->SetEncounterCombatant(true);
    for (int32 I=0;I<3;++I) Shot(Limb,TEXT("LeftUpperArmHitBox"));
    TestTrue(TEXT("Three limb shots leave 1 HP"),FMath::IsNearlyEqual(Limb->GetCurrentHealth(),1.f,.001f));
    Shot(Limb,TEXT("LeftUpperArmHitBox"));
    TestTrue(TEXT("Fourth limb shot caps its HP credit"),FMath::IsNearlyEqual(Flow->StageRecord.StageZones[0].KillAssessmentSum,1.225,1.e-6));
    auto* RPG=W->SpawnActor<AOBEnemyCharacter>();RPG->SetEncounterCombatant(true);RPG->ConfigureEnemyArchetype(EOBEnemyArchetype::RPGTrooper);
    Shot(RPG,TEXT("BodyHitBox"));Shot(RPG,TEXT("HeadHitBox"));
    TestTrue(TEXT("HP150 uses its own max HP denominator"),FMath::IsNearlyEqual(Flow->StageRecord.StageZones[0].KillAssessmentSum,1.225+137.5/150.,1.e-6));
    Flow->RemainingLives=4;Flow->RemainingSeconds=32;Flow->ZoneElapsedSeconds=28;Flow->FinalizeZoneRecord();
    const double Before=Flow->StageRecord.StageZones[0].Score;Flow->FinalizeZoneRecord();
    TestEqual(TEXT("Repeated encounter callbacks cannot change finalized scores"),Flow->StageRecord.StageZones[0].Score,Before);
    for (auto& Zone:Flow->StageRecord.StageZones) { Zone.bFinalized=true;Zone.Score=80; }
    Flow->StageRecord.StageZones[5].Score=92.5;
    TestTrue(TEXT("All six zones have equal weight"),FMath::IsNearlyEqual(FOBStageScoring::Average(Flow->StageRecord.StageZones),82.0833333333333,1.e-9));
    Flow->StageRecord.bHasStageScore=true;Flow->StageRecord.StageScore=82.0833333333333;Flow->StageRecord.StageRank=TEXT("A");
    auto* ResultClass=LoadClass<UOBTrainingResultWidget>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/Components/WBP_TrainingResult.WBP_TrainingResult_C"));
    auto* Widget=CreateWidget<UOBTrainingResultWidget>(W,ResultClass);
    if (TestNotNull(TEXT("Authored stage report loads"),Widget))
    {
        Widget->TakeWidget();Widget->SetResult(Flow->StageRecord);Widget->SetStageCompletion(true);
        TestEqual(TEXT("Rank reaches the existing result UI"),CastChecked<UTextBlock>(Widget->GetWidgetFromName(TEXT("StageRankText")))->GetText().ToString(),FString(TEXT("A")));
        TestEqual(TEXT("Only the display rounds the score"),CastChecked<UTextBlock>(Widget->GetWidgetFromName(TEXT("StageScoreText")))->GetText().ToString(),FString(TEXT("82.08 / 100")));
    }
    return true;
}
#endif

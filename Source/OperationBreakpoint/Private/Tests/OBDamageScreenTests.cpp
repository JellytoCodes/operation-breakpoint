#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/Texture.h"
#include "Engine/LocalPlayer.h"
#include "Components/Image.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBActionComponent.h"
#include "Character/OBEnemyCharacter.h"
#include "Core/OBPlayerController.h"
#include "Stage/OBStageFlowComponent.h"
#include "Widget/OBDamageScreenWidget.h"
#include "Engine/DataTable.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBDamageScreenTest,"OperationBreakpoint.Stage.DamageScreen",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOBDamageScreenTest::RunTest(const FString&)
{
    auto* W=UWorld::CreateWorld(EWorldType::Game,false);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
    W->SetGameInstance(NewObject<UGameInstance>(GEngine));FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));W->SetGameMode(URL);
    ON_SCOPE_EXIT { W->DestroyWorld(false);GEngine->DestroyWorldContext(W); };
    auto* PC=W->SpawnActor<AOBPlayerController>();W->AddController(PC);PC->SetPlayer(NewObject<ULocalPlayer>(GEngine));
    auto* Player=W->SpawnActor<AOBPlayerCharacter>();PC->Possess(Player);
    auto* Stage=Player->GetStageFlowComponent();Stage->Player=Player;Stage->bActive=true;Stage->CurrentZone=1;Stage->Phase=EOBStagePhase::Combat;Stage->RemainingLives=5;
    Stage->EnsureDamageScreen();auto* FX=Stage->GetDamageScreenWidget();
    if (!TestNotNull(TEXT("Stage creates the local damage screen"),FX)) return false;
    if (!TestNotNull(TEXT("Fracture dynamic material resolves"),FX->HitMID.Get()) || !TestNotNull(TEXT("Injured dynamic material resolves"),FX->InjuredMID.Get())) return false;
    for (auto* MID : {FX->HitMID.Get(),FX->InjuredMID.Get()})
        TestTrue(TEXT("Both effects use translucent UI materials"),MID->GetMaterial()->MaterialDomain==MD_UI && MID->GetBlendMode()==BLEND_Translucent);
    TestTrue(TEXT("The exact supplied fracture texture is used"),GetPathNameSafe(FX->HitMID->K2_GetTextureParameterValue(TEXT("FXTexture")))==TEXT("/Game/Assets/FX/Hit.Hit"));
    TestTrue(TEXT("The exact supplied injured texture is used"),GetPathNameSafe(FX->InjuredMID->K2_GetTextureParameterValue(TEXT("FXTexture")))==TEXT("/Game/Assets/FX/Injured.Injured"));
    TestFalse(TEXT("No feedback before damage"),FX->IsHitActive() || FX->IsInjuredActive());
    TestFalse(TEXT("Zero damage does not trigger feedback"),Stage->ApplyPlayerHit(0));
    TestTrue(TEXT("Actual life loss is accepted"),Stage->ApplyPlayerHit(1));TestEqual(TEXT("One life is lost"),Stage->RemainingLives,4);
    TestTrue(TEXT("Life loss starts fracture, without injured pulse"),FX->IsHitActive() && !FX->IsInjuredActive());
    FX->NativeTick(FGeometry(),10.f);
    TestTrue(TEXT("A loading-frame Slate delta cannot consume a newly requested hit"),FX->IsHitActive() && FX->HitElapsed==0.f);
    FX->UpdateForTime(FX->HitStartedAt+.49,16.f/9.f);TestTrue(TEXT("Hit exists just before half a second"),FX->IsHitActive());
    FX->UpdateForTime(FX->HitStartedAt+.5,16.f/9.f);TestFalse(TEXT("Hit ends at exactly half a second"),FX->IsHitActive());
    TestTrue(TEXT("Expired fracture image is collapsed"),FX->HitImage->GetVisibility()==ESlateVisibility::Collapsed);
    Stage->ApplyPlayerHit(1);FX->UpdateForTime(FX->HitStartedAt+.3,16.f/9.f);Stage->ApplyPlayerHit(1);
    TestEqual(TEXT("Another actual hit restarts the half-second effect"),FX->HitElapsed,0.f);
    // Keep a real departure line pending to exercise the hold, independent of media/audio timing.
    Stage->LoadedScripts=NewObject<UDataTable>();Stage->LoadedScripts->RowStruct=FOBStageScriptRow::StaticStruct();
    FOBStageScriptRow Order;Order.Text=FText::FromString(TEXT("Departure test"));Order.Display_Time=5.f;
    Stage->LoadedScripts->AddRow(TEXT("2007"),Order);
    Player->GetActionComponent()->BeginEncounter(7);
    Player->GetActionComponent()->RegisterEnemy(W->SpawnActor<AOBEnemyCharacter>());
    Stage->ApplyPlayerHit(2);
    TestTrue(TEXT("Zero lives starts support and the injured pulse"),Stage->Phase==EOBStagePhase::SupportingFire && FX->IsInjuredActive());
    FX->UpdateForTime(FX->InjuredStartedAt+6.,16.f/9.f);
    TestTrue(TEXT("Injured survives beyond the half-second hit"),FX->IsInjuredActive() && !FX->IsHitActive());
    TestTrue(TEXT("Heartbeat clock wraps on the three-times-slower 2.7 second cycle"),FMath::IsNearlyEqual(FX->InjuredElapsed,.6f,.001f));
    TestFalse(TEXT("Support invulnerability cannot restart the fracture"),Stage->ApplyPlayerHit(1));
    Stage->BeginDeparture(true);
    TestTrue(TEXT("Injured remains throughout the departure narration hold"),Stage->Phase==EOBStagePhase::AwaitingDeparture && FX->IsInjuredActive());
    Stage->ReleaseDeparture();
    TestFalse(TEXT("Injured is removed immediately before travel"),FX->IsInjuredActive());
    FX->SetInjuredActive(true);Stage->SuspendForQA();TestFalse(TEXT("QA reset removes stale injury"),FX->IsInjuredActive());
    Stage->CurrentZone=1;Stage->Phase=EOBStagePhase::Combat;Stage->RemainingLives=2;Stage->StartSupport();
    TestFalse(TEXT("Time-expired support does not invent an injury"),FX->IsInjuredActive());
    FX->SetInjuredActive(true);Stage->AbortForNavigation();TestFalse(TEXT("Navigation abort cleans up injury"),FX->IsInjuredActive());
    FX->ResetEffects();FX->HitMID=nullptr;FX->InjuredMID=nullptr;
    FX->PlayHit();FX->SetInjuredActive(true);
    TestFalse(TEXT("A missing hit material cannot activate a white fallback image"),FX->IsHitActive());
    TestTrue(TEXT("Missing damage materials leave both white fallback brushes collapsed"),FX->HitImage->GetVisibility()==ESlateVisibility::Collapsed && FX->InjuredImage->GetVisibility()==ESlateVisibility::Collapsed);
    return !HasAnyErrors();
}
#endif

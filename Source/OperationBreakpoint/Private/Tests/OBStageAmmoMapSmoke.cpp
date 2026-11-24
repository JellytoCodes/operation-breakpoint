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
#include "UnrealClient.h"
#include "Core/OBPlayerController.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBEnemyCharacter.h"
#include "Character/OBActionComponent.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Stage/OBStageFlowComponent.h"
#include "Widget/OBPlayerOverlayWidget.h"

// Disposable actual-map run: skip the opening, then use the authored Zone1 -> Zone2 rail.
// Enemy clear is synthetic so this isolates ammo carry and moving reload, not combat performance.
class FOBStageAmmoMapSmoke
{
public:
    static void Start()
    {
        FRequestPlaySessionParams Params;Params.GlobalMapOverride=TEXT("/Game/Maps/Stage_01");Params.bAllowOnlineSubsystem=false;
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        GEditor->RequestPlaySession(Params);
        struct FState { int32 Step=0,Shots=0;bool OK=true;double Began=FPlatformTime::Seconds(),At=0;FString Log; };
        auto S=MakeShared<FState>();
        const FString Output=FPaths::ProjectSavedDir()/TEXT("Tests/StageAmmo/PIE");
        IFileManager::Get().MakeDirectory(*Output,true);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S,Output](float) {
            auto Check=[&](bool Good,const TCHAR* Label) { S->OK &= Good;S->Log+=FString::Printf(TEXT("%s %s\n"),Good ? TEXT("PASS") : TEXT("FAIL"),Label); };
            auto Finish=[&]() {
                S->Log+=S->OK ? TEXT("outcome=PASS\n") : TEXT("outcome=FAIL\n");
                FFileHelper::SaveStringToFile(S->Log,*(Output/TEXT("ammo-pie.txt")));
                UE_LOG(LogTemp,Display,TEXT("STAGE_AMMO_PIE\n%s"),*S->Log);
                GEditor->RequestEndPlayMap();const bool OK=S->OK;
                FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([OK](float) {
                    if (GEditor && GEditor->PlayWorld) return true;
                    FPlatformMisc::RequestExitWithStatus(false,OK ? 0 : 1);return false;
                }),.5f);
            };
            if (FPlatformTime::Seconds()-S->Began>150) { Check(false,TEXT("Actual map scenario deadline"));Finish();return false; }
            auto* W=GEditor ? GEditor->PlayWorld.Get() : nullptr;
            auto* PC=W ? Cast<AOBPlayerController>(W->GetFirstPlayerController()) : nullptr;
            auto* P=PC ? Cast<AOBPlayerCharacter>(PC->GetPawn()) : nullptr;
            if (!P || W->GetTimeSeconds()<1 || !P->GetPlayerSplineComponent()->GetRailPath()) return true;
            auto* F=P->GetStageFlowComponent();auto* Rail=P->GetPlayerSplineComponent();auto* O=F->GetOverlay();
            auto Clear=[&]() {
                const auto Enemies=P->GetActionComponent()->GetEncounterEnemies();
                for (AOBEnemyCharacter* Enemy:Enemies)
                    if (IsValid(Enemy) && Enemy->GetCombatState()!=EOBEnemyCombatState::Dead) { FDamageEvent Damage;Enemy->TakeDamage(100000,Damage,PC,P); }
            };
            const FVector Start(0,0,100000),End(1000,0,100000);
            if (S->Step==0)
            {
                Check(PC->GetCurrentAmmo()==20 && PC->GetMagazineCapacity()==20,TEXT("Actual Stage_01 starts at 20/20 after Blueprint defaults"));
                F->StageRecord.bUsedTestInputs=true;P->SetCanBeDamaged(false);
                F->ClearPresentation();F->RestoreOpeningMovement();F->Phase=EOBStagePhase::Travel;
                if (O) { O->CancelTrainingDialogue();O->SetVisibility(ESlateVisibility::HitTestInvisible);O->SetStageMode(); }
                PC->OnShotResolved.AddLambda([S](const FHitResult&){++S->Shots;});
                FString Error;Check(Rail->JumpToStagePointForQA(7,Error),TEXT("Enter the real Zone1 encounter"));S->Step=1;
            }
            else if (S->Step==1 && PC->GetGunInputMode()==EOBGunInputMode::Shooting && F->GetCurrentZone()==1)
            {
                for (int32 I=0;I<17;++I) PC->FireAlongRay(Start,End);
                Check(S->Shots==17 && PC->GetCurrentAmmo()==3,TEXT("Seventeen production shots leave 3/20 in Zone1"));
                FScreenshotRequest::RequestScreenshot(Output/TEXT("zone1-three-rounds.png"),true,false);
                Clear();S->Step=2;
            }
            else if (S->Step==2 && Rail->IsMoving())
            {
                Check(PC->GetCurrentAmmo()==3,TEXT("Actual travel retains three rounds without a reload"));
                PC->FireAlongRay(Start,End);Check(S->Shots==17,TEXT("Actual moving input cannot fire"));S->Step=3;
            }
            else if (S->Step==3 && F->GetPhase()==EOBStagePhase::Combat && F->GetCurrentZone()==2 && PC->GetGunInputMode()==EOBGunInputMode::Shooting)
            {
                Check(PC->GetCurrentAmmo()==3 && O && O->CurrentAmmo==3 && O->MaxAmmo==20,TEXT("Actual Zone2 and overlay data start at 3/20 without reset"));
                FScreenshotRequest::RequestScreenshot(Output/TEXT("zone2-three-rounds.png"),true,false);
                for (int32 I=0;I<8;++I) PC->FireAlongRay(Start,End);
                Check(S->Shots==20 && PC->GetCurrentAmmo()==0 && F->GetStageRecord().ShotsFired==20,TEXT("Empty pulls after shot twenty produce no trace or score event"));
                Clear();S->Step=4;
            }
            else if (S->Step==4 && Rail->IsMoving())
            {
                Check(PC->CanReloadInCurrentMode(),TEXT("Actual Stage travel permits reload"));
                for (int32 I=0;I<4;++I) PC->CompleteReload();
                Check(PC->GetCurrentAmmo()==20 && O && O->CurrentAmmo==20 && O->MaxAmmo==20,TEXT("Actual repeated moving reloads fill to exactly 20/20"));
                FScreenshotRequest::RequestScreenshot(Output/TEXT("travel-reloaded.png"),true,false);
                S->At=FPlatformTime::Seconds();S->Step=5;
            }
            else if (S->Step==5 && FPlatformTime::Seconds()-S->At>1) { Finish();return false; }
            return true;
        }),.1f);
    }
};
static FAutoConsoleCommand GOBStageAmmoPIESmoke(TEXT("OB.Stage.AmmoPIE"),TEXT("Disposable Stage_01 ammunition carry/reload run; exits its editor."),FConsoleCommandDelegate::CreateStatic(&FOBStageAmmoMapSmoke::Start));
#endif

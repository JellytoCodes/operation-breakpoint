#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Editor.h"
#include "LevelEditor.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Core/OBPlayerController.h"
#include "Character/OBPlayerCharacter.h"
#include "Training/OBTrainingDirector.h"
#include "Widget/OBHUD.h"
#include "Widget/OBPlayerOverlayWidget.h"

// Checkpoints only alter disposable PIE state; no map or training result is saved.
class FOBTrainingAmmoMapSmoke
{
public:
    static void Start()
    {
        FRequestPlaySessionParams Params;Params.GlobalMapOverride=TEXT("/Game/Maps/TutorialMap");Params.bAllowOnlineSubsystem=false;
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        GEditor->RequestPlaySession(Params);
        struct FState { int32 Step=0,Shots=0;bool OK=true;double Began=FPlatformTime::Seconds(),At=0;FString Log; };
        auto S=MakeShared<FState>();
        const FString Output=FPaths::ProjectSavedDir()/TEXT("Tests/TrainingAmmo/PIE");
        IFileManager::Get().MakeDirectory(*Output,true);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S,Output](float) {
            auto Check=[&](bool Good,const TCHAR* Label) { S->OK &= Good;S->Log+=FString::Printf(TEXT("%s %s\n"),Good ? TEXT("PASS") : TEXT("FAIL"),Label); };
            auto Finish=[&]() {
                S->Log+=S->OK ? TEXT("outcome=PASS\n") : TEXT("outcome=FAIL\n");
                FFileHelper::SaveStringToFile(S->Log,*(Output/TEXT("ammo-pie.txt")));
                UE_LOG(LogTemp,Display,TEXT("TRAINING_AMMO_PIE\n%s"),*S->Log);
                GEditor->RequestEndPlayMap();const bool OK=S->OK;
                FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([OK](float) {
                    if (GEditor && GEditor->PlayWorld) return true;
                    FPlatformMisc::RequestExitWithStatus(false,OK ? 0 : 1);return false;
                }),.5f);
            };
            if (FPlatformTime::Seconds()-S->Began>100) { Check(false,TEXT("Actual TutorialMap scenario deadline"));Finish();return false; }
            auto* W=GEditor ? GEditor->PlayWorld.Get() : nullptr;
            auto* PC=W ? Cast<AOBPlayerController>(W->GetFirstPlayerController()) : nullptr;
            if (!PC || !PC->GetPawn() || W->GetTimeSeconds()<1) return true;
            AOBTrainingDirector* D=nullptr;
            for (TActorIterator<AOBTrainingDirector> It(W);It;++It) if (It->IsReadyForQA()) { D=*It;break; }
            if (!D) return true;
            auto* HUD=PC->GetOBHUD();auto* O=HUD ? HUD->GetPlayerOverlayWidget() : nullptr;
            const FVector Start(0,0,100000),End(1000,0,100000);
            if (S->Step==0)
            {
                FString Error;Check(D->EnterQACheckpoint(TEXT("Reload"),Error),TEXT("Enter the actual basic reload exercise"));
                Check(PC->GetCurrentAmmo()==0 && PC->GetMagazineCapacity()==20,TEXT("Actual basic exercise starts at 0/20"));
                PC->CompleteReload();
                Check(PC->GetCurrentAmmo()==3 && PC->GetMagazineCapacity()==20 && O && O->CurrentAmmo==3 && O->MaxAmmo==20,TEXT("Actual basic reload and zeroing use 3/20"));
                for (int32 I=0;I<4;++I) PC->CompleteReload();
                Check(PC->GetCurrentAmmo()==3,TEXT("Further basic reload inputs cannot accumulate rounds"));
                FScreenshotRequest::RequestScreenshot(Output/TEXT("basic-three-of-twenty.png"),true,false);
                S->At=FPlatformTime::Seconds();S->Step=1;
            }
            else if (S->Step==1 && FPlatformTime::Seconds()-S->At>1)
            {
                FString Error;Check(D->EnterQACheckpoint(TEXT("Shooting1"),Error),TEXT("Enter the actual mock shooting exercise"));
                Check(PC->GetCurrentAmmo()==20 && PC->GetMagazineCapacity()==20,TEXT("Actual mock exercise starts at 20/20"));
                PC->SetCrouchMode(false);
                PC->OnShotResolved.AddLambda([S](const FHitResult&){++S->Shots;});
                for (int32 I=0;I<25;++I) PC->FireAlongRay(Start,End);
                Check(S->Shots==20 && PC->GetCurrentAmmo()==0 && D->GetTrainingResult().ShotsFired==20,TEXT("Actual mock shooting stops at twenty with no extra score events"));
                Check(O && O->CurrentAmmo==0 && O->MaxAmmo==20,TEXT("Actual empty mock overlay data is 0/20"));
                FScreenshotRequest::RequestScreenshot(Output/TEXT("mock-empty.png"),true,false);
                S->At=FPlatformTime::Seconds();S->Step=2;
            }
            else if (S->Step==2 && FPlatformTime::Seconds()-S->At>1)
            {
                PC->CompleteReload();
                Check(PC->GetCurrentAmmo()==20 && O && O->CurrentAmmo==20 && O->MaxAmmo==20,TEXT("Actual mock reload restores exactly 20/20"));
                PC->FireAlongRay(Start,End);
                Check(S->Shots==21 && PC->GetCurrentAmmo()==19,TEXT("Actual mock firing resumes after reload"));
                FScreenshotRequest::RequestScreenshot(Output/TEXT("mock-reloaded-and-fired.png"),true,false);
                S->At=FPlatformTime::Seconds();S->Step=3;
            }
            else if (S->Step==3 && FPlatformTime::Seconds()-S->At>1) { Finish();return false; }
            return true;
        }),.1f);
    }
};
static FAutoConsoleCommand GOBTrainingAmmoPIESmoke(TEXT("OB.Training.AmmoPIE"),TEXT("Disposable TutorialMap basic/mock ammunition check; exits its editor."),FConsoleCommandDelegate::CreateStatic(&FOBTrainingAmmoMapSmoke::Start));
#endif

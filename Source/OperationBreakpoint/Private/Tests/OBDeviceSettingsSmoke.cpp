#if WITH_DEV_AUTOMATION_TESTS
#include "Input/OBDeviceSettingsSubsystem.h"
#include "Input/OBOpenFireSubsystem.h"
#include "Core/OBPlayerController.h"
#include "Widget/OBTitleWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Components/AudioComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformMisc.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/SWidget.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SEditableText.h"

class FOBDeviceSettingsSmoke
{
public:
    static void Start()
    {
        struct FState { int32 Step=0; double At=FPlatformTime::Seconds(); bool OK=true; FString Log; };
        auto S=MakeShared<FState>();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float)
        {
            const double Now=FPlatformTime::Seconds();
            if (Now-S->At<1.0) return true;
            UWorld* W=nullptr;
            for (const auto& C:GEngine->GetWorldContexts()) if (C.World() && C.World()->IsGameWorld() && C.World()->HasBegunPlay()) {W=C.World();break;}
            if (!W || !W->GetFirstPlayerController()) return true;
            auto* PC=Cast<AOBPlayerController>(W->GetFirstPlayerController());
            auto* Settings=W->GetGameInstance()->GetSubsystem<UOBDeviceSettingsSubsystem>();
            auto Check=[S](bool OK,const TCHAR* What) { S->OK &= OK; S->Log+=FString::Printf(TEXT("%s %s\n"),OK?TEXT("PASS"):TEXT("FAIL"),What); };
            const FString Folder=FPaths::ProjectSavedDir()/TEXT("Tests/DeviceSettings");
            IFileManager::Get().MakeDirectory(*Folder,true);
            if (S->Step==0)
            {
                Check(PC && Settings,TEXT("Runtime player and settings subsystem exist"));
                if (!Settings || !PC) return false;
                Settings->Toggle(1);
                Check(Settings->GetMode()==1 && UGameplayStatics::IsGamePaused(W),TEXT("F6 target pauses the existing world"));
                Check(PC->GetGunInputMode()==EOBGunInputMode::Disabled,TEXT("Gameplay gun input is disabled inside settings"));
                if (FParse::Param(FCommandLine::Get(),TEXT("OBDeviceCalibrationOnly")))
                {
                    TSharedPtr<SExpandableArea> Advanced;
                    TFunction<void(TSharedRef<SWidget>)> FindAdvanced=[&](TSharedRef<SWidget> Widget) {
                        if (Advanced) return;
                        if (Widget->GetType()==TEXT("SExpandableArea")) { Advanced=StaticCastSharedRef<SExpandableArea>(Widget);return; }
                        auto* Children=Widget->GetChildren();
                        for (int32 I=0;I<Children->Num();++I) FindAdvanced(Children->GetChildAt(I));
                    };
                    FindAdvanced(Settings->Screen.ToSharedRef());
                    Check(Advanced && !Advanced->IsExpanded(),TEXT("F6 opens with advanced settings collapsed"));
                    if (Advanced) Advanced->SetExpanded(true);
                    TSharedPtr<SSpinBox<double>> Horizontal;
                    TFunction<void(TSharedRef<SWidget>)> Find=[&](TSharedRef<SWidget> Widget) {
                        if (Horizontal) return;
                        if (Widget->GetTypeAsString().Contains(TEXT("SSpinBox"))) { Horizontal=StaticCastSharedRef<SSpinBox<double>>(Widget);return; }
                        auto* Children=Widget->GetChildren();
                        for (int32 I=0;I<Children->Num();++I) Find(Children->GetChildAt(I));
                    };
                    Find(Settings->Screen.ToSharedRef());
                    Check(Horizontal.IsValid(),TEXT("Actual F6 horizontal correction spin box exists"));
                    if (Horizontal)
                    {
                        const auto Binding=Horizontal->GetValueAttribute();const double Original=Horizontal->GetValue();
                        // No Enter/commit event: this is the control's live value-change path.
                        Horizontal->SetValue(5.,false);
                        Check(FMath::IsNearlyEqual(Settings->ReadNumber(Settings->GunSection(),TEXT("x_shift"),0),.05),TEXT("F6 +5 percent is saved as +0.05 without Enter"));
                        Horizontal->SetValue(-7.5,false);
                        Check(FMath::IsNearlyEqual(Settings->ReadNumber(Settings->GunSection(),TEXT("x_shift"),0),-.075),TEXT("F6 -7.5 percent is saved as -0.075 without Enter"));
                        TSharedPtr<SEditableText> Field;
                        TFunction<void(TSharedRef<SWidget>)> FindText=[&](TSharedRef<SWidget> Widget) {
                            if (Field) return;
                            if (Widget->GetType()==TEXT("SEditableText")) { Field=StaticCastSharedRef<SEditableText>(Widget);return; }
                            auto* Children=Widget->GetChildren();
                            for (int32 I=0;I<Children->Num();++I) FindText(Children->GetChildAt(I));
                        };
                        FindText(Horizontal.ToSharedRef());Check(Field.IsValid(),TEXT("Actual F6 numeric text field exists"));
                        if (Field)
                        {
                            Field->SetEditableText(FText::GetEmpty());
                            for (TCHAR Character:FString(TEXT("2.5"))) static_cast<SWidget*>(Field.Get())->OnKeyChar(Field->GetCachedGeometry(),FCharacterEvent(Character,FModifierKeysState(),0,false));
                            Check(FMath::IsNearlyEqual(Settings->ReadNumber(Settings->GunSection(),TEXT("x_shift"),0),.025),TEXT("Typing 2.5 percent applies +0.025 without Enter or focus change"));
                        }
                        Horizontal->SetValue(Original,false);Horizontal->SetValue(Binding,false);
                    }
                    Settings->Close();S->Log+=S->OK?TEXT("RESULT PASS\n"):TEXT("RESULT FAIL\n");
                    FFileHelper::SaveStringToFile(S->Log,*(Folder/TEXT("x-calibration-smoke.txt")));
                    FPlatformMisc::RequestExitWithStatus(false,S->OK?0:1);return false;
                }
                TArray<UUserWidget*> Titles; UWidgetBlueprintLibrary::GetAllWidgetsOfClass(W,Titles,UOBTitleWidget::StaticClass(),false);
                for (auto* Widget:Titles) if (auto* Title=Cast<UOBTitleWidget>(Widget))
                {
                    FOBOpenFireState Input; Input.bHasFrame=true; Input.ReceivedAt=Now; Input.Frame.bConnected=true;
                    Input.Frame.Session=TEXT("settings-title-isolation"); Input.Frame.Sequence=1;
                    Title->HandleOpenFireStart(Input,true);
                    Input.Frame.Sequence=2; Input.Frame.bInteractionTrigger=true; Input.Events.bInteractionPressed=true;
                    Title->HandleOpenFireStart(Input,true);
                    Check(!Title->bStartRequested,TEXT("Settings test trigger cannot start the hidden title"));
                }
                auto* Receiver=W->GetGameInstance()->GetSubsystem<UOBOpenFireSubsystem>();
                auto& Input=const_cast<FOBOpenFireState&>(Receiver->GetState());
                Input.bHasFrame=true; Input.ReceivedAt=Now;
                Input.Frame.bConnected=Input.Frame.bAimValid=true;
                Input.Frame.Position=FVector2D(.7,.6);
                if (FParse::Param(FCommandLine::Get(),TEXT("OBDeviceTargetOnly")))
                { ++S->Step;S->At=Now;return true; }
                if (FParse::Param(FCommandLine::Get(),TEXT("OBDeviceSettingsAudioOnly")))
                {
                    Check(GEngine->UseSound(),TEXT("Audio verification runs with a real engine audio device"));
                    Check(Settings->GunFireSound && Settings->GunReloadSound && Settings->GunEmptySound,TEXT("Gun feedback is already loaded before the first input"));
                    Settings->Reload(); Settings->Shoot();
                    Check(Settings->GunFireSound && Settings->GunReloadSound && Settings->GunEmptySound,TEXT("Existing fire, reload and empty-magazine assets load"));
                    Settings->SuspendPresentation();
                    Check(Settings->GunFeedbackAudio.Num()==2,TEXT("Reload and shot each create one feedback sound"));
                    for (const auto& Weak:Settings->GunFeedbackAudio)
                    {
                        auto* Audio=Weak.Get();
                        Check(Audio && Audio->bIsUISound && Audio->GetPlayState()==EAudioComponentPlayState::Playing,
                            TEXT("Settings audio plays while the training world stays paused"));
                    }
                }
                else
                {
                    for (int32 I=0;I<22;++I) Settings->Shoot();
                    Check(Settings->GetAmmo()==0 && Settings->GetMarks().Num()==20,TEXT("20-round magazine stops at empty"));
                    for (int32 I=0;I<4;++I) { Settings->Reload(); for (int32 J=0;J<20;++J) Settings->Shoot(); }
                    Check(Settings->GetMarks().Num()==60,TEXT("Impact pool is capped at 60 across reloads"));
                    Settings->Reload();
                    Check(Settings->GetAmmo()==20,TEXT("Reload restores 20 rounds"));
                }
                FString Before; FFileHelper::LoadFileToString(Before,*Settings->SettingsPath());
                Check(Settings->WriteNumber(TEXT("Camera"),TEXT("minimum_visibility"),.7),TEXT("Unreal writes the common calibration INI"));
                Check(FMath::IsNearlyEqual(Settings->ReadNumber(TEXT("Camera"),TEXT("minimum_visibility"),0),.7),TEXT("Common calibration reload matches saved value"));
                FString After; FFileHelper::LoadFileToString(After,*Settings->SettingsPath());
                TArray<FString> Rows; Before.ParseIntoArrayLines(Rows);
                for (const FString& Row:Rows)
                    if (Row.StartsWith(TEXT("player_roi="))) Check(After.Contains(Row),TEXT("Unreal scalar save preserves camera ROI JSON"));
            }
            else if (S->Step==1)
            {
                auto& Input=const_cast<FOBOpenFireState&>(W->GetGameInstance()->GetSubsystem<UOBOpenFireSubsystem>()->GetState()); Input.ReceivedAt=Now;
                if (FParse::Param(FCommandLine::Get(),TEXT("OBDeviceTargetOnly")))
                {
                    const FVector2D Size=Settings->Screen->GetCachedGeometry().GetLocalSize();
                    Check(Size.X>1 && Size.Y>1,TEXT("Target uses the real painted F6 canvas"));
                    const FVector2D Before=Settings->TargetPosition;
                    Input.Frame.Position=FVector2D(.99,.01);Settings->Shoot();
                    Check(Settings->TargetPosition==Before && Settings->GetAmmo()==19,TEXT("A miss consumes a round and leaves the target in place"));
                    bool Moved=true,Visible=true;
                    for (int32 I=0;I<19;++I)
                    {
                        const FVector2D Previous=Settings->TargetPosition;
                        const FVector2D Center=Settings->GetTargetCenter(Size);const float R=Settings->GetTargetRadius(Size);
                        Visible &= Center.X-R>=355.f && Center.X+R<Size.X && Center.Y-R>=48.f && Center.Y+R<=Size.Y-48.f;
                        Input.Frame.Position=Center/Size;Settings->Shoot();
                        Moved &= Settings->TargetPosition!=Previous;
                    }
                    Check(Moved,TEXT("Each of nineteen real shots on the circle moves the single target"));
                    Check(Visible,TEXT("Random targets stay fully visible outside the settings panel and LED guides"));
                    const FVector2D EmptyTarget=Settings->TargetPosition;
                    Input.Frame.Position=Settings->GetTargetCenter(Size)/Size;Settings->Shoot();
                    Check(Settings->GetAmmo()==0 && Settings->TargetPosition==EmptyTarget,TEXT("An empty magazine cannot move the target"));
                    Settings->Reload();
                    Check(Settings->GetAmmo()==20 && Settings->TargetPosition==EmptyTarget,TEXT("Reload preserves the current target"));
                    Check(Settings->GetMarks().Num()==20,TEXT("Existing bullet marks remain after misses and hits"));
                    Input.Frame.Position=FVector2D(.5,.5);
                }
                FScreenshotRequest::RequestScreenshot(Folder/TEXT("gun-settings.png"),true,false);
            }
            else if (S->Step==2)
            {
                if (FParse::Param(FCommandLine::Get(),TEXT("OBDeviceTargetOnly")))
                {
                    Settings->Toggle(1);
                    Check(!Settings->IsOpen() && !UGameplayStatics::IsGamePaused(W),TEXT("Second F6 restores the same world"));
                    S->Log+=S->OK?TEXT("RESULT PASS\n"):TEXT("RESULT FAIL\n");
                    FFileHelper::SaveStringToFile(S->Log,*(Folder/TEXT("target-smoke.txt")));
                    UE_LOG(LogTemp,Display,TEXT("DEVICE_TARGET_SMOKE %s"),*S->Log);
                    FPlatformMisc::RequestExit(false);return false;
                }
                Settings->Toggle(2);
                Check(Settings->GetMode()==2 && UGameplayStatics::IsGamePaused(W),TEXT("F7 switches screens while preserving the paused world"));
            }
            else if (S->Step==3) FScreenshotRequest::RequestScreenshot(Folder/TEXT("camera-settings.png"),true,false);
            else if (S->Step==4)
            {
                Settings->Toggle(2);
                Check(!Settings->IsOpen() && !UGameplayStatics::IsGamePaused(W),TEXT("Second F7 restores the same live world"));
                UGameplayStatics::SetGamePaused(W,true);
                Settings->Toggle(1); Settings->Toggle(1);
                Check(UGameplayStatics::IsGamePaused(W),TEXT("Opening settings from an already paused screen preserves pause"));
                UGameplayStatics::SetGamePaused(W,false);
                S->Log+=S->OK?TEXT("RESULT PASS\n"):TEXT("RESULT FAIL\n");
                FFileHelper::SaveStringToFile(S->Log,*(Folder/TEXT("runtime-smoke.txt")));
                UE_LOG(LogTemp,Display,TEXT("DEVICE_SETTINGS_SMOKE %s"),*S->Log);
                FPlatformMisc::RequestExit(false); return false;
            }
            ++S->Step; S->At=Now; return true;
        }));
    }
};
static FAutoConsoleCommand GOBDeviceSettingsSmoke(TEXT("OB.DeviceSettings.Smoke"),TEXT("Validate runtime device settings in a disposable game instance."),FConsoleCommandDelegate::CreateStatic(&FOBDeviceSettingsSmoke::Start));
#endif

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Editor.h"
#include "LevelEditor.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "Engine/DamageEvents.h"
#include "EngineUtils.h"
#include "Core/OBGameInstance.h"
#include "Core/OBGameViewportClient.h"
#include "Core/OBPlayerController.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Character/OBActionComponent.h"
#include "Character/OBEnemyCharacter.h"
#include "Stage/OBStageFlowComponent.h"
#include "Widget/OBHUD.h"
#include "Widget/OBTitleWidget.h"
#include "World/OBBreachDoor.h"
#include "World/OBEncounterSpawner.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/TextBlock.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"
#include "Input/CursorReply.h"
#include "Widgets/SWidget.h"

// Title input and the actual course destination remain production behavior.
// Course actions/opening video are omitted by this fixture; rail, VO, squad,
// shortcut fade, Chaos breach and final outcome then run without teleport fixtures.
class FOBStageShortcutMapSmoke
{
public:
    static void Start()
    {
        FRequestPlaySessionParams Params;Params.GlobalMapOverride=TEXT("/Game/Maps/TitleMap");Params.bAllowOnlineSubsystem=false;
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        GEditor->RequestPlaySession(Params);
        struct FState
        {
            int32 Step=0;bool OK=true,SawBlack=false,WarpChecked=false,SawDialogue=false,BreachSent=false;
            double Started=FPlatformTime::Seconds(),At=0,LastProgress=0;float Distance=0;
            FString Log;TWeakObjectPtr<UWorld> Previous;
            TArray<int32> NarrationIds;
            TArray<TWeakObjectPtr<UAudioComponent>> NarrationChannels;
            bool NarrationExclusive=true,SkippedArrivalAbsent=true,BlackoutSilent=true;
            FString AudioSamples=TEXT("time,id,zone,phase,moving,playing_channels\n");
        };
        const bool AudioCheck=FParse::Param(FCommandLine::Get(),TEXT("StageShortcutAudioCheck"));
        auto S=MakeShared<FState>();const FString Out=FPaths::ProjectSavedDir()/(AudioCheck ? TEXT("Tests/ShortcutArrivalAudio/PIE") : TEXT("Tests/StageShortcut/PIE"));
        IFileManager::Get().MakeDirectory(*Out,true);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S,Out,AudioCheck](float) {
            auto Check=[&](bool Good,const TCHAR* Text) {
                S->OK &= Good;S->Log+=FString::Printf(TEXT("%s step=%d %s\n"),Good ? TEXT("PASS") : TEXT("FAIL"),S->Step,Text);
                FFileHelper::SaveStringToFile(S->Log,*(Out/TEXT("progress.txt")));
            };
            auto Finish=[&]() {
                S->Log+=S->OK ? TEXT("outcome=PASS\n") : TEXT("outcome=FAIL\n");
                FFileHelper::SaveStringToFile(S->Log,*(Out/TEXT("verification.txt")));
                if (AudioCheck) FFileHelper::SaveStringToFile(S->AudioSamples,*(Out/TEXT("narration.csv")));
                UE_LOG(LogTemp,Display,TEXT("STAGE_SHORTCUT_PIE\n%s"),*S->Log);
                GEditor->RequestEndPlayMap();const bool OK=S->OK;
                FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([OK](float) {
                    if (GEditor && GEditor->PlayWorld) return true;
                    FPlatformMisc::RequestExitWithStatus(false,OK ? 0 : 1);return false;
                }),.25f);
            };
            if (FPlatformTime::Seconds()-S->Started>420.) { Check(false,TEXT("Shortcut and ordinary route meet deadline"));Finish();return false; }
            auto* W=GEditor ? GEditor->PlayWorld.Get() : nullptr;
            auto* PC=W ? Cast<AOBPlayerController>(W->GetFirstPlayerController()) : nullptr;
            auto* GI=W ? W->GetGameInstance<UOBGameInstance>() : nullptr;
            if (!PC || !GI || W->GetTimeSeconds()<.3f) return true;
            auto Key=[&](FKey K,EInputEvent E=IE_Pressed) {
                auto* View=PC->GetLocalPlayer() ? PC->GetLocalPlayer()->ViewportClient.Get() : nullptr;
                if (!Cast<UOBGameViewportClient>(View)) { Check(false,TEXT("Actual viewport routes operator keys"));return; }
                View->InputKey(FInputKeyEventArgs(View->Viewport,FInputDeviceId::CreateFromInternalId(0),K,E,FPlatformTime::Cycles64()));
            };
            const FString Map=UGameplayStatics::GetCurrentLevelName(W,true);
            auto* Title=PC->GetOBHUD() ? PC->GetOBHUD()->GetTitleWidget() : nullptr;
            auto* P=Cast<AOBPlayerCharacter>(PC->GetPawn());
            auto* Flow=P ? P->GetStageFlowComponent() : nullptr;
            auto* Rail=P ? P->GetPlayerSplineComponent() : nullptr;
            auto ClearEnemies=[&]() {
                const auto Enemies=P->GetActionComponent()->GetEncounterEnemies();FDamageEvent Damage;
                for (AOBEnemyCharacter* Enemy:Enemies) if (IsValid(Enemy)) Enemy->TakeDamage(100000,Damage,PC,P);
            };
            if (S->Step==0)
            {
                if (!Title || Title->Clips.Num()!=3 || Title->ActiveClip==INDEX_NONE || Title->ClipStates[2]!=UOBTitleWidget::EClipState::Ready) return true;
                Check(GI->IsStageShortcutEnabled(),TEXT("Fresh application starts with shortcut ON"));
                Key(EKeys::F11);Key(EKeys::F11,IE_Repeat);
                Check(!GI->IsStageShortcutEnabled(),TEXT("F11 turns shortcut OFF without repeating"));
                Key(EKeys::F11);
                Check(GI->IsStageShortcutEnabled(),TEXT("F11 turns shortcut back ON"));
                Key(EKeys::F9);Key(EKeys::F9,IE_Repeat);
                Check(!GI->IsCrosshairEnabled(),TEXT("F9 hides the crosshair without repeating"));
                Key(EKeys::F8);S->Previous=W;S->At=FPlatformTime::Seconds();S->Step=1;return true;
            }
            if (S->Step==1 && FPlatformTime::Seconds()-S->At>.25)
            {
                Check(W==S->Previous.Get() && Map==TEXT("TitleMap") && Title && !Title->bStartRequested,TEXT("F9, F11 and old F8 never navigate or auto-start the title"));
                Check(Title && Title->ShortcutStateText && Title->ShortcutStateText->GetText().ToString()==TEXT("F11 : Shortcut Mode ON")
                    && Title->ShortcutStateText->GetFont().Size==10,TEXT("Main screen displays the 10-point shortcut label"));
                FPointerEvent Trigger(0,FVector2D(960,540),FVector2D(960,540),TSet<FKey>{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState());
                Title->NativeOnPreviewMouseButtonDown(FGeometry(),Trigger);
                Check(Title->bStartRequested && Title->bEntrancePlaying,TEXT("Normal trigger starts the original title entrance"));S->Step=2;return true;
            }
            if (S->Step==2)
            {
                if (Map==TEXT("TitleMap")) return true;
                Check(Map==TEXT("TutorialMap"),TEXT("Trigger still enters TutorialMap rather than skipping to Stage_01"));
                Check(GI->IsStageShortcutEnabled(),TEXT("Shortcut stays ON on entering the tutorial"));
                Check(!GI->IsCrosshairEnabled(),TEXT("Crosshair preference survives map travel"));
                auto* View = W->GetGameViewport();
                const auto Hidden = View->MapCursor(View->Viewport, FCursorReply::Cursor(EMouseCursor::Default));
                Check(Hidden.IsSet() && Hidden.GetValue()->GetType() != TEXT("SOBCrosshair"),TEXT("OFF uses a blank software cursor in the tutorial"));
                Key(EKeys::F9);
                Check(GI->IsCrosshairEnabled(),TEXT("F9 restores the crosshair in the tutorial"));
                const auto Visible = View->MapCursor(View->Viewport, FCursorReply::Cursor(EMouseCursor::Default));
                Check(Visible.IsSet() && Visible.GetValue()->GetType() == TEXT("SOBCrosshair"),TEXT("ON maps the aiming cursor to the native crosshair"));
                S->Previous=W;PC->NavigateToMap(TEXT("/Game/Maps/Stage_01"));S->Step=3;return true;
            }
            if (S->Step==8 && Map==TEXT("TitleMap"))
            {
                Check(GI->IsStageShortcutEnabled(),TEXT("Shortcut stays ON after returning to title"));
                S->Previous=W;Key(EKeys::F10);S->Step=9;return true;
            }
            if (!Flow || !Flow->IsStageActive() || !Rail || !Rail->GetRailPath()) return true;
            P->SetCanBeDamaged(false);
            if (AudioCheck && S->Step>=4 && S->Step<=6)
            {
                auto* Voice=Flow->NarrationVoice.Get();
                bool NewVoice=Voice && Voice->IsPlaying() && !S->NarrationChannels.Contains(Voice);
                if (NewVoice) { S->NarrationChannels.Add(Voice);S->NarrationIds.Add(Flow->ActiveScriptId); }
                int32 Playing=0;for (const auto& Channel:S->NarrationChannels) if (Channel.IsValid() && Channel->IsPlaying()) ++Playing;
                S->NarrationExclusive &= Playing<=1;
                auto IsSkippedArrival=[](int32 Id) { return Id==2008 || Id==2010 || Id==2012 || Id==2014; };
                S->SkippedArrivalAbsent &= !IsSkippedArrival(Flow->ActiveScriptId);
                for (const auto& Entry:Flow->NarrationQueue) S->SkippedArrivalAbsent &= !IsSkippedArrival(Entry.Id);
                if (Flow->GetPhase()==EOBStagePhase::ShortcutTransition)
                    S->BlackoutSilent &= !Flow->ActiveScriptId && Flow->NarrationQueue.IsEmpty() && Playing==0;
                if (NewVoice)
                {
                    const FString Sample=FString::Printf(TEXT("%.3f,%d,%d,%d,%d,%d\n"),W->GetTimeSeconds(),Flow->ActiveScriptId,Flow->GetCurrentZone(),int32(Flow->GetPhase()),Rail->IsMoving(),Playing);
                    S->AudioSamples+=Sample;
                    FFileHelper::SaveStringToFile(S->AudioSamples,*(Out/TEXT("narration.csv")));
                    UE_LOG(LogTemp,Display,TEXT("SHORTCUT_AUDIO_START %s"),*Sample);
                    if (Flow->ActiveScriptId==2016) FScreenshotRequest::RequestScreenshot(Out/TEXT("Shortcut-Zone6-Arrival.png"),true,false);
                }
            }
            if (S->Step==3)
            {
                if (W==S->Previous.Get() || Flow->GetPhase()!=EOBStagePhase::Opening) return true;
                Check(Flow->bStageShortcutDemo && Flow->GetCurrentZone()==0,TEXT("Stage_01 reads ON state without changing its original startup"));
                Flow->FinishOpening();S->Step=4;return true;
            }
            if (S->Step==4 && Flow->GetCurrentZone()==1 && Flow->GetPhase()==EOBStagePhase::Combat)
            {
                PC->SetTrainingAmmo(3,20);ClearEnemies();S->LastProgress=FPlatformTime::Seconds();S->Step=5;return true;
            }
            if (S->Step==5)
            {
                if (Flow->GetPhase()==EOBStagePhase::ShortcutTransition)
                {
                    Check(Flow->IsInputBlocked() && !Flow->CanActivateEncounter(19),TEXT("Shortcut transition blocks input and Zone2 activation"));
                    if (!S->SawBlack && PC->PlayerCameraManager && PC->PlayerCameraManager->FadeAmount>=.999f)
                    {
                        TArray<FColor> Pixels;auto* View=PC->GetLocalPlayer()->ViewportClient->Viewport;
                        bool Black=View && View->ReadPixels(Pixels) && !Pixels.IsEmpty();
                        for (int32 I=0;I<Pixels.Num();I+=97) Black &= Pixels[I].R<8 && Pixels[I].G<8 && Pixels[I].B<8;
                        if (Black)
                        {
                            Check(true,TEXT("Actual rendered viewport is black during the checkpoint transfer"));
                            FScreenshotRequest::RequestScreenshot(Out/TEXT("Shortcut-Black.png"),false,false);S->SawBlack=true;
                        }
                    }
                }
                if (Flow->bStageShortcutTransferred && !S->WarpChecked)
                {
                    Check(S->SawBlack && Rail->GetCurrentPointIndex()==72 && Flow->GetCurrentZone()==5,TEXT("Blackout transfers to the post-Zone5 stop at point72"));
                    Check(PC->GetCurrentAmmo()==3,TEXT("Zone1 remaining ammunition survives the shortcut"));
                    bool Unplayed=true;for (int32 I=1;I<5;++I) Unplayed &= !Flow->GetStageRecord().StageZones[I].bStarted;
                    Check(Unplayed,TEXT("Skipped Zones2-5 are not recorded as played or cleared"));
                    for (TActorIterator<AOBEncounterSpawner> It(W);It;++It) Check(It->GetPreparedPointIndex()!=72,TEXT("Warp does not spawn the skipped Zone5 encounter"));
                    S->WarpChecked=true;
                }
                if (Flow->GetPhase()==EOBStagePhase::Zone6Dialogue && !S->SawDialogue)
                { Check(S->WarpChecked && Rail->GetCurrentPointIndex()==83,TEXT("Normal squad travel reaches the Zone6 dialogue stop"));S->SawDialogue=true; }
                if (Flow->GetPhase()==EOBStagePhase::Zone6Breach && !S->BreachSent)
                {
                    Check(S->SawDialogue && Rail->GetCurrentPointIndex()==85 && Flow->ActiveBreachDoor && !Flow->ActiveBreachDoor->IsBreached(),TEXT("Shortcut preserves the intact door and real breach gate"));
                    S->BreachSent=Flow->AttachBreachChargeForQA();
                }
                if (Flow->GetPhase()==EOBStagePhase::Zone6Intro)
                {
                    Check(S->BreachSent && Flow->bBreachCompleted && Rail->GetCurrentPointIndex()==86,TEXT("Real Chaos breach and ordinary travel reach final rescue scene"));
                    FScreenshotRequest::RequestScreenshot(Out/TEXT("Shortcut-Zone6.png"),true,false);S->Step=6;return true;
                }
                if (Flow->GetPhase()==EOBStagePhase::Travel && FMath::Abs(Rail->GetDistanceAlongRoute()-S->Distance)>.5f)
                { S->Distance=Rail->GetDistanceAlongRoute();S->LastProgress=FPlatformTime::Seconds(); }
                if (Flow->GetPhase()==EOBStagePhase::Travel && FPlatformTime::Seconds()-S->LastProgress>30.)
                { Check(false,TEXT("Squad and player continue moving after shortcut"));Finish();return false; }
            }
            if (S->Step==6 && Flow->GetPhase()==EOBStagePhase::Combat)
            {
                if (AudioCheck)
                {
                    // Let the final real radio sentence finish instead of cutting it with fixture kills.
                    if (Flow->ActiveScriptId || !Flow->NarrationQueue.IsEmpty()) return true;
                    const TArray<int32> Expected={2005,2006,2007,2015,2017,2018,2016};
                    Check(S->NarrationIds==Expected,TEXT("Actual audio starts once each in order: Zone1 move, arrival, clear; Zone6 move, hostage, terrorist, arrival"));
                    Check(S->SkippedArrivalAbsent,TEXT("Skipped Zone2-5 arrival lines are neither queued nor played"));
                    Check(S->NarrationExclusive,TEXT("No two actual stage narration AudioComponents play together"));
                    Check(S->BlackoutSilent,TEXT("Shortcut blackout cancels old narration and leaves no queued or playing dialogue"));
                    Check(!Flow->NarrationVoice,TEXT("Final arrival sentence finishes and drains before the rescue fixture clears enemies"));
                }
                ClearEnemies();S->Step=7;return true;
            }
            if (S->Step==7 && Flow->GetPhase()==EOBStagePhase::Complete)
            {
                Check(Flow->DidSucceed(),TEXT("Shortcut route can finish with hostage rescue success"));
                if (AudioCheck) { Finish();return false; }
                Key(EKeys::F5);S->Step=8;return true;
            }
            if (S->Step==9 && W!=S->Previous.Get() && Flow->GetPhase()==EOBStagePhase::Zone6Intro)
            { Check(Rail->GetCurrentPointIndex()==86 && Flow->bBreachCompleted,TEXT("F10 opens post-breach replay"));Finish();return false; }
            return true;
        }),.05f);
    }
};
static FAutoConsoleCommand GOBStageShortcutPIE(TEXT("OB.Stage.ShortcutPIE"),TEXT("Verify crosshair and shortcut hotkeys, title label, course flow, Zone1 skip, breach and replay in PIE."),FConsoleCommandDelegate::CreateStatic(&FOBStageShortcutMapSmoke::Start));
#endif

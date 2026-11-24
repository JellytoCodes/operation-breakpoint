#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Editor.h"
#include "LevelEditor.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/DamageEvents.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "SkeletalRenderPublic.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PanelWidget.h"
#include "Components/SplineComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/GameViewportSubsystem.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "MediaPlayer.h"
#include "MediaSoundComponent.h"
#include "Core/OBPlayerController.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Character/OBActionComponent.h"
#include "Character/OBEnemyCharacter.h"
#include "Character/OBHostageCharacter.h"
#include "Stage/OBStageFlowComponent.h"
#include "Stage/OBStageCinematicWidget.h"
#include "World/OBRailPath.h"
#include "World/OBEncounterTrigger.h"
#include "World/OBEncounterSpawner.h"
#include "World/OBBreachDoor.h"
#include "MediaPipe/OBMediaPipeOSCSubsystem.h"
#include "Widget/OBHudPanelWidget.h"
#include "Widget/OBPlayerOverlayWidget.h"
#include "Training/OBTrainingResultWidget.h"
#include "Training/OBTrainingStatusWidget.h"
#include "OSCAddress.h"
#include "OSCManager.h"
#include "OSCMessage.h"

// Disposable PIE state only. Run separately with -Zone6Outcome=Success|Fail|Timeout.
// The saved rail, VO, encounter, GeometryCollection and movie remain production assets.
class FOBStageZone6MapSmoke
{
public:
    static void Start()
    {
        FRequestPlaySessionParams Params;
        Params.GlobalMapOverride = TEXT("/Game/Maps/Stage_01"); Params.bAllowOnlineSubsystem = false;
        Params.DestinationSlateViewport = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        GEditor->RequestPlaySession(Params);
        const double Started = FPlatformTime::Seconds();
        struct FOpeningCheck
        {
            bool OK=true, Captured=false;
            TSet<int32> Seen;
            TWeakObjectPtr<UWidget> Subtitle;
            int32 LastId=0, Request=0;
        };
        auto Opening=MakeShared<FOpeningCheck>();
        const bool FullOpening=FParse::Param(FCommandLine::Get(),TEXT("Zone6FullOpening"));
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Started,Opening,FullOpening](float) {
            auto* World = GEditor ? GEditor->PlayWorld.Get() : nullptr;
            auto* Controller = World ? Cast<AOBPlayerController>(World->GetFirstPlayerController()) : nullptr;
            auto* Player = Controller ? Cast<AOBPlayerCharacter>(Controller->GetPawn()) : nullptr;
            if (Player && World->GetTimeSeconds() > .8f && Player->GetPlayerSplineComponent()->GetRailPath())
            {
                auto* Flow=Player->GetStageFlowComponent();
                auto* Overlay=Flow->GetOverlay();
                if (FullOpening && Flow->GetPhase()==EOBStagePhase::Opening)
                {
                    if (Overlay && Flow->PresentationSubtitleId>=2001 && Flow->PresentationSubtitleId<=2004)
                    {
                        const int32 Id=Flow->PresentationSubtitleId;
                        Opening->OK &= CaptionMatches(Overlay,Flow->FindScript(Id));
                        if (!Opening->Subtitle.IsValid()) Opening->Subtitle=Overlay->GetSubtitleWidget();
                        Opening->OK &= Opening->Subtitle.Get()==Overlay->GetSubtitleWidget();
                        if (Opening->LastId==Id) Opening->OK &= Opening->Request==Overlay->SubtitleRequestNumber;
                        else { Opening->Seen.Add(Id);Opening->LastId=Id;Opening->Request=Overlay->SubtitleRequestNumber; }
                        for (const TCHAR* Name : {TEXT("Life"),TEXT("Timer"),TEXT("Mission"),TEXT("SystemMessage")})
                            if (auto* Panel=Overlay->GetWidgetFromName(FName(Name))) Opening->OK &= !Panel->IsVisible();
                        if (!Opening->Captured && Flow->VoiceElapsed>2.f)
                        {
                            const auto MovieSlot=UGameViewportSubsystem::Get()->GetWidgetSlot(Flow->Cinematic);
                            const auto HudSlot=UGameViewportSubsystem::Get()->GetWidgetSlot(Overlay);
                            Opening->OK &= MovieSlot.ZOrder!=INDEX_NONE && MovieSlot.ZOrder<HudSlot.ZOrder
                                && Overlay->GetSubtitleWidget()->GetRenderOpacity()>.95f
                                && Overlay->GetSubtitleWidget()->GetCachedGeometry().GetLocalSize().Y>0;
                            FString Output=TEXT("Build/Testing/Verification/Zone6Breach");
                            FParse::Value(FCommandLine::Get(),TEXT("Zone6Output="),Output);
                            FScreenshotRequest::RequestScreenshot(FPaths::ProjectDir()/Output/TEXT("opening-caption.png"),true,false);
                            FString Layout;
                            Overlay->WidgetTree->ForEachWidget([&](UWidget* Widget) {
                                const auto Geometry=Widget->GetCachedGeometry();
                                Layout+=FString::Printf(TEXT("%s parent=%s visibility=%d opacity=%.3f size=%s position=%s desired=%s\n"),
                                    *Widget->GetName(),*GetNameSafe(Widget->GetParent()),int32(Widget->GetVisibility()),Widget->GetRenderOpacity(),
                                    *Geometry.GetLocalSize().ToString(),*Geometry.GetAbsolutePosition().ToString(),*Widget->GetDesiredSize().ToString());
                            });
                            FFileHelper::SaveStringToFile(Layout,*(FPaths::ProjectDir()/Output/TEXT("opening-layout.txt")));
                            Opening->Captured=true;
                        }
                    }
                    return true;
                }
                if (FullOpening && Flow->GetPhase()==EOBStagePhase::AwaitingDeparture) return true;
                if (FullOpening)
                {
                    Opening->OK &= Opening->Seen.Num()==4 && Overlay && !Overlay->IsCinematicSubtitleMode()
                        && Opening->Subtitle.Get()==Overlay->GetSubtitleWidget();
                    FString Output=TEXT("Build/Testing/Verification/Zone6Breach");
                    FParse::Value(FCommandLine::Get(),TEXT("Zone6Output="),Output);
                    const FString Report=FString::Printf(TEXT("opening_rows_seen=%d shared_embedded_caption_and_no_restarted_fades=%d\noutcome=%s\n"),
                        Opening->Seen.Num(),Opening->OK,Opening->OK ? TEXT("PASS") : TEXT("FAIL"));
                    FFileHelper::SaveStringToFile(Report,*(FPaths::ProjectDir()/Output/TEXT("opening-overlay-pie.txt")));
                }
                Run(World,Player,Opening->OK);return false;
            }
            if (FPlatformTime::Seconds() - Started > 100.)
            { UE_LOG(LogTemp, Error, TEXT("ZONE6_PIE bootstrap timed out")); FPlatformMisc::RequestExitWithStatus(false, 1); return false; }
            return true;
        }), .1f);
    }
private:
    static void SendBreach(UOBMediaPipeOSCSubsystem* Motion, bool State)
    {
        if (!Motion) return;
        FOSCMessage Message; UOSCManager::SetOSCMessageAddress(Message, FOSCAddress(TEXT("/motion/breach")));
        UOSCManager::AddInt32(Message, State ? 1 : 0);
        Motion->HandleOSCMessage(Message, TEXT("127.0.0.1"), 8000);
    }
    static bool CaptionMatches(UOBPlayerOverlayWidget* Overlay, const FOBStageScriptRow* Row)
    {
        return Overlay && Overlay->IsVisible() && Overlay->IsCinematicSubtitleMode() && Row
            && Overlay->GetSubtitleWidget() && Overlay->GetSubtitleWidget()->IsVisible()
            && Overlay->GetSubtitleText().EqualTo(Row->Text);
    }
    static void Run(UWorld* World, AOBPlayerCharacter* Player, bool OpeningOK)
    {
        struct FState
        {
            int32 Step = 0, FinalCombatants = 0, Hostages = 0;
            bool GuideOnly=false,GuideInjected=false,GuideChecked=false,GuideHoldChecked=false;
            bool OK = true, bSawHostageAudio = false, bSawTerroristAudio = false, bResultCaptured=false, bHostagesCaptured=false;
            bool bHoldValid = true, bGapObserved = false, bSawMediaOpen = false;
            bool bCaptionAtOpen = false, bCaptionStayedVisible = true, bLateCaption = false, bBoundMediaOpen = false;
            bool bFlyingCaptured = false, bSettledCameraObserved = false, bMoviePictureCaptured = false;
            bool bNarrationGainValid = true, bMovieGainValid = false;
            bool bSameSubtitle=true, bNoRestartedFade=true, bSawOutcomeFadeOut=false;
            int32 MovieCaptionRequest=0;
            TWeakObjectPtr<UWidget> SharedSubtitle;
            double Started = 0, StepStarted = 0, GapStarted = 0, LastMotion = 0, OpenClock = -1, MaxMediaClock = 0, MediaDuration = 0;
            double RootBrokenAt = -1.;
            double FadeStartedAt = -1., FadeFinishedAt = -1.;
            double FullBlackStartedAt = -1., FullBlackFinishedAt = -1.;
            bool bExplosionOnlyWhileBlack = true, bBlackHoldStill = true;
            FVector BlackHoldPosition;
            TSet<TWeakObjectPtr<UAudioComponent>> ExplosionInstances;
            float HeldDistance = 0, MaxHoldDrift = 0, CombatSeconds = 0;
            float MaxFragmentTravel = 0, HeldCameraFOV = 0;
            int32 MovedRigidFragments = 0;
            FVector HeldPosition;
            TArray<int32> DialogueIds;
            TArray<FTransform3f> InitialFragmentTransforms;
            TArray<int32> RigidFragmentIndices;
            FTransform InitialCollectionWorld;
            TWeakObjectPtr<AOBBreachDoor> Door;
            TWeakObjectPtr<UOBMediaPipeOSCSubsystem> Motion;
            TArray<TWeakObjectPtr<AOBEnemyCharacter>> Combatants;
            TArray<TWeakObjectPtr<AOBHostageCharacter>> HostageActors;
            FString Mode = TEXT("Success"), Output = TEXT("Build/Testing/Verification/Zone6Breach"), Log;
            FTimerHandle Timer;
        };
        auto State = MakeShared<FState>(); State->OK=OpeningOK; State->Started = State->StepStarted = World->GetTimeSeconds();
        State->GuideOnly=FParse::Param(FCommandLine::Get(),TEXT("Zone6GuideOnly"));
        Player->GetStageFlowComponent()->StartResultTracking();
        Player->GetStageFlowComponent()->StageRecord.bUsedTestInputs=true;
        FParse::Value(FCommandLine::Get(), TEXT("Zone6Outcome="), State->Mode);
        FParse::Value(FCommandLine::Get(), TEXT("Zone6Output="), State->Output);
        State->Motion = World->GetGameInstance()->GetSubsystem<UOBMediaPipeOSCSubsystem>();
        const FString Output = FPaths::ProjectDir() / State->Output;
        IFileManager::Get().MakeDirectory(*Output, true);
        TWeakObjectPtr<UWorld> WeakWorld = World;
        World->GetTimerManager().SetTimer(State->Timer, FTimerDelegate::CreateLambda([State, WeakWorld, Output]() {
            auto* W = WeakWorld.Get(); if (!W) return;
            auto* PC = Cast<AOBPlayerController>(W->GetFirstPlayerController());
            auto* P = PC ? Cast<AOBPlayerCharacter>(PC->GetPawn()) : nullptr; if (!P) return;
            auto* Flow = P->GetStageFlowComponent(); auto* Rail = P->GetPlayerSplineComponent(); auto* Action = P->GetActionComponent();
            const auto KeepAlive = State;
            auto Check = [&](bool Good, const TCHAR* Label) {
                State->OK &= Good;
                State->Log += FString::Printf(TEXT("%s %s\n"), Good ? TEXT("PASS") : TEXT("FAIL"), Label);
            };
            auto ObserveEndingOpened = [State, WeakWorld, Output]() {
                if (State->bSawMediaOpen) return;
                auto* OpenWorld = WeakWorld.Get();
                auto* OpenPC = OpenWorld ? Cast<AOBPlayerController>(OpenWorld->GetFirstPlayerController()) : nullptr;
                auto* OpenPlayer = OpenPC ? Cast<AOBPlayerCharacter>(OpenPC->GetPawn()) : nullptr;
                auto* OpenFlow = OpenPlayer ? OpenPlayer->GetStageFlowComponent() : nullptr;
                if (!OpenFlow || !OpenFlow->Cinematic || !OpenFlow->bEndingVideoOpened) return;
                auto* Media = OpenFlow->Cinematic->GetMediaPlayer();
                const bool bSuccess = State->Mode.Equals(TEXT("Success"), ESearchCase::IgnoreCase);
                State->bSawMediaOpen = true; State->OpenClock = Media ? Media->GetTime().GetTotalSeconds() : -1.;
                State->bCaptionAtOpen = CaptionMatches(OpenFlow->GetOverlay(), OpenFlow->FindScript(bSuccess ? 2022 : 2023))
                    && State->OpenClock >= 0. && State->OpenClock < .5;
                if (auto* Overlay=OpenFlow->GetOverlay())
                {
                    State->bSameSubtitle &= State->SharedSubtitle.Get()==Overlay->GetSubtitleWidget();
                    State->MovieCaptionRequest=Overlay->SubtitleRequestNumber;
                }
                TInlineComponentArray<UMediaSoundComponent*> Sounds(OpenPC);
                State->bMovieGainValid = Sounds.ContainsByPredicate([](UMediaSoundComponent* Sound) {
                    const auto* Audio = Sound->GetAudioComponent();
                    return Audio && FMath::IsNearlyEqual(Audio->VolumeMultiplier, 3.f);
                });
                FScreenshotRequest::RequestScreenshot(Output / (State->Mode + TEXT("-ending-caption.png")), true, false);
            };
            auto Finish = [&]() {
                State->Log += FString::Printf(TEXT("case=%s max_hold_drift_cm=%.4f held_83_camera_fov=%.4f media_open_seconds=%.4f media_max_seconds=%.4f media_duration_seconds=%.4f combatants=%d hostages=%d moved_rigid_fragments=%d max_fragment_travel_cm=%.4f\noutcome=%s\n"),
                    *State->Mode, State->MaxHoldDrift, State->HeldCameraFOV, State->OpenClock, State->MaxMediaClock, State->MediaDuration,
                    State->FinalCombatants, State->Hostages, State->MovedRigidFragments, State->MaxFragmentTravel,
                    State->OK ? TEXT("PASS") : TEXT("FAIL"));
                FFileHelper::SaveStringToFile(State->Log, *(Output / (State->Mode + TEXT("-zone6-pie.txt"))));
                UE_LOG(LogTemp, Display, TEXT("ZONE6_PIE\n%s"), *State->Log);
                W->GetTimerManager().ClearTimer(State->Timer); GEditor->RequestEndPlayMap();
                const bool OK = State->OK;
                FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([OK](float) {
                    if (GEditor && GEditor->PlayWorld) return true;
                    FPlatformMisc::RequestExitWithStatus(false, OK ? 0 : 1); return false;
                }), .5f);
            };
            const double Now = W->GetTimeSeconds();
            if (State->Step >= 3 && State->Step <= 6 && PC->PlayerCameraManager)
            {
                const auto* Camera = PC->PlayerCameraManager.Get();
                if (State->FadeStartedAt < 0. && Camera->bEnableFading && Camera->FadeAmount > .001f) State->FadeStartedAt = Now;
                if (State->FadeStartedAt >= 0. && State->FadeFinishedAt < 0. && !Camera->bEnableFading) State->FadeFinishedAt = Now;
                const bool FullBlack = Camera->bEnableFading && Camera->FadeAmount >= .999f;
                if (FullBlack && State->FullBlackStartedAt < 0.) { State->FullBlackStartedAt = Now; State->BlackHoldPosition = P->GetActorLocation(); }
                if (!FullBlack && State->FullBlackStartedAt >= 0. && State->FullBlackFinishedAt < 0.) State->FullBlackFinishedAt = Now;
                if (FullBlack && Flow->GetPhase() == EOBStagePhase::Zone6Breach)
                    State->bBlackHoldStill &= P->GetActorLocation().Equals(State->BlackHoldPosition, .1f);
                if (IsValid(Flow->BreachExplosionAudio) && Flow->BreachExplosionAudio->IsPlaying())
                {
                    const TWeakObjectPtr<UAudioComponent> Explosion(Flow->BreachExplosionAudio.Get());
                    if (!State->ExplosionInstances.Contains(Explosion))
                        State->bExplosionOnlyWhileBlack &= FullBlack && Flow->GetPhase() == EOBStagePhase::Zone6Breach;
                    State->ExplosionInstances.Add(Explosion);
                }
            }
            if (Now - State->Started > 120.) { Check(false, TEXT("Zone6 scenario deadline")); Finish(); return; }
            // Observe the real simulation while normal stage travel continues. This does not
            // delay release, move a fragment, or change the collection's velocity/physics.
            if (!State->bFlyingCaptured && State->RootBrokenAt >= 0. && Now - State->RootBrokenAt >= .2)
            {
                auto* Door = State->Door.Get(); auto* Debris = Door ? Door->GetFracturedDoor() : nullptr;
                if (Debris)
                {
                    const auto& Current = Debris->GetComponentSpaceTransforms3f();
                    const FTransform CurrentWorld = Debris->GetComponentTransform();
                    for (int32 Index : State->RigidFragmentIndices)
                        if (State->InitialFragmentTransforms.IsValidIndex(Index) && Current.IsValidIndex(Index))
                        {
                            const FVector Before = State->InitialCollectionWorld.TransformPosition(FVector(State->InitialFragmentTransforms[Index].GetLocation()));
                            const FVector After = CurrentWorld.TransformPosition(FVector(Current[Index].GetLocation()));
                            const float Distance = float(FVector::Distance(Before, After));
                            State->MaxFragmentTravel = FMath::Max(State->MaxFragmentTravel, Distance);
                            if (Distance > 5.f) ++State->MovedRigidFragments;
                        }
                }
                Check(State->RigidFragmentIndices.Num() > 1 && State->MovedRigidFragments > 1 && State->MaxFragmentTravel > 5.f,
                    TEXT("Multiple actual rigid fragments fly more than five centimeters after the Chaos break"));
                FScreenshotRequest::RequestScreenshot(Output / (State->Mode + TEXT("-Zone6-door-flying.png")), true, false);
                State->bFlyingCaptured = true;
            }
            if (State->Step == 0)
            {
                auto* Overlay = Flow->GetOverlay();
                auto* Subtitle = Overlay ? Overlay->GetWidgetFromName(TEXT("Subtitle")) : nullptr;
                State->SharedSubtitle=Subtitle;
                State->Log += FString::Printf(TEXT("stage_overlay_class=%s stage_subtitle_class=%s\n"),
                    *GetPathNameSafe(Overlay ? Overlay->GetClass() : nullptr),
                    *GetPathNameSafe(Subtitle ? Subtitle->GetClass() : nullptr));
                Check(Flow->IsStageActive() && Flow->bUseZone6Breach, TEXT("Saved Stage_01 enables the new Zone6 breach flow"));
                Check(State->Motion.IsValid(), TEXT("The real game instance owns the OSC subsystem"));
                Check(Rail->GetRailPath()->GetSpline()->GetNumberOfSplinePoints() > 86, TEXT("The saved rail includes all three final stops"));
                AOBBreachDoor* Door = nullptr;
                for (TActorIterator<AOBBreachDoor> It(W); It; ++It) { Door = *It; break; }
                State->Door = Door;
                Check(Door && Door->GetIntactDoorMesh()->GetStaticMesh() && Door->FracturedDoorAsset,
                    TEXT("The saved stage door has an intact leaf and a baked Chaos collection"));
                if (!State->OK) { Finish(); return; }
                Flow->RestoreSlowMotion(); Flow->ClearPresentation(); Flow->RestoreOpeningMovement();
                Flow->AutomaticScriptIds.Reset(); Flow->bZone6DialogueStarted = Flow->bZone6DialogueFinished = Flow->bBreachCompleted = false;
                Flow->Phase = EOBStagePhase::Travel; Flow->CurrentZone = 5;
                Action->ResetEncounterForQA(); P->SetCanBeDamaged(false);
                for (TActorIterator<AOBEncounterTrigger> It(W); It; ++It) It->ResetForQA();
                Rail->SetStageMovementBlocked(false);
                Rail->CurrentPointIndex = 83; Rail->NextSplineCuePoint = 84; Rail->TargetPointIndex = INDEX_NONE;
                Rail->CurrentDistance = Rail->TargetDistance = Rail->GetRailPath()->GetSpline()->GetDistanceAlongSplineAtSplinePoint(83);
                Rail->ApplySplineTransform(Rail->CurrentDistance);
                SendBreach(State->Motion.Get(), true); State->LastMotion = Now;
                Rail->BeginStopAlignment(83); Rail->BeginAction();
                State->HeldPosition = P->GetActorLocation(); State->HeldDistance = Rail->GetDistanceAlongRoute();
                Check(Flow->GetPhase() == EOBStagePhase::Zone6Dialogue && Flow->ActiveScriptId == 2017,
                    TEXT("Real stop 83 begins the hostage line and blocks progression"));
                const auto* Stop = Rail->GetRailPath()->FindStopSettings(83);
                Check(Stop && Stop->bOverrideFacingYaw && FMath::IsNearlyEqual(Stop->FacingYaw, 90.f)
                    && Stop->bOverrideCameraView && FMath::IsNearlyEqual(Stop->CameraFOV, 80.f)
                    && FMath::Abs(FMath::FindDeltaAngleDegrees(P->GetActorRotation().Yaw, 90.f)) < 1.f,
                    TEXT("83 uses the saved 90-degree facing and 80-degree camera view"));
                FScreenshotRequest::RequestScreenshot(Output / (State->Mode + TEXT("-Zone6-83-dialogue.png")), true, false);
                State->Step = 1; State->StepStarted = Now; return;
            }
            // Keep the pre-arrival recognition held through the real voice durations and approach.
            if (State->Step <= 3 && Now - State->LastMotion >= .2)
            { SendBreach(State->Motion.Get(), true); State->LastMotion = Now; }
            if (State->Step == 1)
            {
                if (Flow->ActiveScriptId == 2017 || Flow->ActiveScriptId == 2018)
                {
                    if (State->DialogueIds.IsEmpty() || State->DialogueIds.Last() != Flow->ActiveScriptId) State->DialogueIds.Add(Flow->ActiveScriptId);
                    const bool Playing = Flow->NarrationVoice && Flow->NarrationVoice->IsPlaying();
                    if (Playing) State->bNarrationGainValid &= FMath::IsNearlyEqual(Flow->NarrationVoice->VolumeMultiplier, 3.f);
                    if (Flow->ActiveScriptId == 2017) State->bSawHostageAudio |= Playing;
                    else State->bSawTerroristAudio |= Playing;
                }
                if (Flow->GetPhase() == EOBStagePhase::Zone6Dialogue)
                {
                    State->MaxHoldDrift = FMath::Max(State->MaxHoldDrift, float(FVector::Distance(P->GetActorLocation(), State->HeldPosition)));
                    State->bHoldValid &= Flow->IsInputBlocked() && Flow->GetRemainingSeconds() == 0.f && !Flow->CanActivateEncounter(86)
                        && Action->GetEncounterEnemies().IsEmpty() && FMath::Abs(Rail->GetDistanceAlongRoute() - State->HeldDistance) < .1f;
                    if (!Flow->ActiveScriptId && Flow->NarrationQueue.IsEmpty() && Flow->Zone6GapRemaining > 0.f)
                    {
                        // Sample before release; the next travel camera is allowed to have a different FOV.
                        State->HeldCameraFOV = P->GetPlayerCamera()->FieldOfView; State->bSettledCameraObserved = true;
                        if (!State->bGapObserved) { State->bGapObserved = true; State->GapStarted = Now; }
                    }
                    return;
                }
                Check(State->DialogueIds == TArray<int32>({2017, 2018}) && State->bSawHostageAudio && State->bSawTerroristAudio,
                    TEXT("83 plays both actual AudioComponents once, in hostage/terrorist order"));
                Check(State->bNarrationGainValid, TEXT("Live Stage_01 dialogue AudioComponents use the requested 3.0x volume"));
                Check(State->bHoldValid && State->MaxHoldDrift < 1.f, TEXT("83 remains stopped with no encounter or hostage clock during both lines"));
                Check(State->bSettledCameraObserved && FMath::IsNearlyEqual(State->HeldCameraFOV, 80.f, .5f),
                    TEXT("The actual held 83 camera reaches its authored 80-degree field of view before release"));
                Check(State->bGapObserved && Now - State->GapStarted >= Flow->Zone6DialogueGapSeconds - .11,
                    TEXT("A separate pause follows the end of the second line"));
                Check(Rail->TargetPointIndex == 85, TEXT("The normal rail release targets 85 next"));
                State->Step = 2; State->StepStarted = Now; return;
            }
            if (State->Step == 2)
            {
                if (!Flow->IsWaitingForBreach()) return;
                Check(Rail->GetCurrentPointIndex() == 85 && Flow->GetRemainingSeconds() == 0.f && !Flow->bBreachRequested,
                    TEXT("85 waits on the held OSC state without a timer or automatic breach"));
                Check(State->Motion->IsBreachRecognized() && !Flow->bBreachSawNeutral,
                    TEXT("The real OSC parser preserves held true at gate entry"));
                Check(Flow->ActiveScriptId == 2016, TEXT("The breach instruction begins at 85"));
                FScreenshotRequest::RequestScreenshot(Output / (State->Mode + TEXT("-Zone6-85-await-breach.png")), true, false);
                State->HeldPosition = P->GetActorLocation(); State->Step = 3; State->StepStarted = Now; return;
            }
            if (State->Step == 3)
            {
                if (State->GuideOnly && Now-State->StepStarted>.2)
                {
                    auto* Overlay=Flow->GetOverlay();
                    auto* Markers=Overlay ? Overlay->GetTrainingMarkers() : nullptr;
                    auto* Text=Markers ? Cast<UTextBlock>(Markers->GetWidgetFromName(TEXT("BreachText"))) : nullptr;
                    if (!State->GuideInjected)
                    {
                        Check(Markers && Markers->IsVisible() && (Overlay->VisibleElements&int32(EOBOverlayElement::Markers)),
                            TEXT("Zone6 displays the authored breach guide without a training director"));
                        Check(Text && Text->IsVisible() && !Text->GetText().IsEmpty() && !Text->GetText().ToString().Contains(TEXT("카메라")),
                            TEXT("Missing camera feedback still shows action guidance with no connection notice"));
                        Check(Overlay && Overlay->GetSubtitleWidget() && Overlay->GetSubtitleWidget()->IsVisible(),
                            TEXT("Arrival subtitles and the breach guide remain visible together"));
                        State->GuideInjected=true;
                    }
                    FOSCMessage Guide; UOSCManager::SetOSCMessageAddress(Guide,FOSCAddress(TEXT("/mediapipe/breach/guide")));
                    const bool BothHands=Now-State->StepStarted>.85;
                    for (float Value:TArray<float>{1,1280,720,0,1,1,.6475f,.3875f,.7825f,.5625f,.2175f,.3875f,.3525f,.5625f,.7f,.45f,.9f,.28f,.45f,.9f,1,BothHands?1.f:0.f,.5f,0})
                        UOSCManager::AddFloat(Guide,Value);
                    State->Motion->HandleOSCMessage(Guide,TEXT("127.0.0.1"),8000);
                    if (!State->GuideChecked && Now-State->StepStarted>.65)
                    {
                        auto* Left=Markers ? Markers->GetWidgetFromName(TEXT("LeftHandSuccess")) : nullptr;
                        auto* Right=Markers ? Markers->GetWidgetFromName(TEXT("RightHandTarget")) : nullptr;
                        auto* Progress=Markers ? Cast<UProgressBar>(Markers->GetWidgetFromName(TEXT("BreachProgress"))) : nullptr;
                        auto* Layer=Markers ? Markers->GetWidgetFromName(TEXT("BreachLayer")) : nullptr;
                        Check(Layer && Layer->IsVisible() && Layer->GetCachedGeometry().GetLocalSize().X>0,
                            TEXT("The breach guide has a rendered layout during the arrival caption"));
                        Check(Left && !Left->IsVisible() && Right && !Right->IsVisible() && !Progress,
                            TEXT("The central C4 target waits while the tracked hand is outside it"));
                        FScreenshotRequest::RequestScreenshot(Output/TEXT("Zone6-guide-live.png"),true,false);
                        State->GuideChecked=true;
                    }
                    if (!State->GuideHoldChecked && Now-State->StepStarted>1.15)
                    {
                        auto* Left=Markers ? Markers->GetWidgetFromName(TEXT("LeftHandSuccess")) : nullptr;
                        auto* Right=Markers ? Markers->GetWidgetFromName(TEXT("RightHandSuccess")) : nullptr;
                        auto* Progress=Markers ? Cast<UProgressBar>(Markers->GetWidgetFromName(TEXT("BreachProgress"))) : nullptr;
                        Check(Left && !Left->IsVisible() && Right && !Right->IsVisible() && !Progress,
                            TEXT("Legacy two-hand target success cannot advance central C4 placement"));
                        FScreenshotRequest::RequestScreenshot(Output/TEXT("Zone6-guide-hold.png"),true,false);
                        State->GuideHoldChecked=true;
                    }
                }
                if (Now - State->StepStarted < (State->GuideOnly?1.5:1.))
                {
                    State->bHoldValid &= Flow->IsWaitingForBreach() && !Flow->bBreachRequested && Flow->GetRemainingSeconds() == 0.f
                        && FVector::Distance(P->GetActorLocation(), State->HeldPosition) < 1.f && !Flow->CanActivateEncounter(86);
                    return;
                }
                Check(State->bHoldValid && !State->Door->IsBreached(), TEXT("A held gesture and elapsed time cannot skip the locked door"));
                SendBreach(State->Motion.Get(), false);
                SendBreach(State->Motion.Get(), true);
                Check(!Flow->bBreachRequested,TEXT("Legacy two-hand OSC recognition cannot plant Stage_01 C4"));
                Check(Flow->AttachBreachChargeForQA(),TEXT("Outcome fixture plants C4 through the explicit QA placement helper"));
                Check(Flow->bBreachRequested && !State->Door->IsBreachInProgress() && !State->Door->IsBreached(),
                    TEXT("Planting starts fade-out while the actual door remains intact"));
                State->Step = 4; State->StepStarted = Now; return;
            }
            if (State->Step == 4)
            {
                auto* Door = State->Door.Get();
                if (!Door) { Check(false, TEXT("The authored breach door remains valid")); Finish(); return; }
                if (Flow->bBreachPhysicsStarted && State->InitialFragmentTransforms.IsEmpty())
                {
                    auto* Debris=Door->GetFracturedDoor();
                    State->InitialFragmentTransforms=Debris->GetComponentSpaceTransforms3f();State->InitialCollectionWorld=Debris->GetComponentTransform();
                    const auto Collection=Door->FracturedDoorAsset->GetGeometryCollection();
                    if (Collection) for (int32 I=0;I<Collection->SimulationType.Num();++I)
                        if (Collection->IsRigid(I) && State->InitialFragmentTransforms.IsValidIndex(I)) State->RigidFragmentIndices.Add(I);
                    Check(State->RigidFragmentIndices.Num()>1,TEXT("The fracture starts with measurable rigid-piece transforms under black"));
                    Check(PC->PlayerCameraManager && PC->PlayerCameraManager->FadeAmount>=.999f,TEXT("Actual Chaos destruction starts only under full black"));
                }
                if (!Door->IsBreached())
                {
                    State->bHoldValid &= Flow->IsWaitingForBreach() && Flow->GetRemainingSeconds() == 0.f && !Flow->CanActivateEncounter(86);
                    if (Now - State->StepStarted > 10.) { Check(false, TEXT("Chaos root destruction completes")); Finish(); }
                    return;
                }
                Check(State->bHoldValid && Door->GetFracturedDoor()->IsRootBroken(), TEXT("Progression waits for the actual synchronized Chaos root break"));
                Check(Door->GetIntactDoorMesh()->GetCollisionEnabled() == ECollisionEnabled::NoCollision && !Door->GetIntactDoorMesh()->IsVisible(),
                    TEXT("The destroyed intact leaf no longer blocks or draws"));
                Check(Door->GetFracturedDoor()->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Ignore
                    && Door->GetFracturedDoor()->GetCollisionResponseToChannel(ECC_Camera) == ECR_Ignore,
                    TEXT("Real debris leaves player movement and the camera clear"));
                Check(!Door->TryBreach(), TEXT("Repeated recognition cannot destroy the same door twice"));
                State->RootBrokenAt = Now;
                FScreenshotRequest::RequestScreenshot(Output / (State->Mode + TEXT("-Zone6-door-broken.png")), true, false);
                if (State->GuideOnly) { State->Step=50;State->StepStarted=Now;return; }
                State->Step = 5; State->StepStarted = Now; return;
            }
            if (State->Step==50 && Now-State->StepStarted>.3)
            {
                auto* Overlay=Flow->GetOverlay();
                Check(Overlay && Overlay->GetTrainingMarkers() && !Overlay->GetTrainingMarkers()->IsVisible() && !(Overlay->VisibleElements&int32(EOBOverlayElement::Markers)),
                    TEXT("The breach guide disappears after the actual door is destroyed"));
                Finish();return;
            }
            if (State->Step == 5)
            {
                if (Flow->GetPhase() != EOBStagePhase::Combat || Rail->GetCurrentPointIndex() != 86) return;
                Check(Flow->bBreachCompleted && Flow->CanActivateEncounter(86), TEXT("The rail reaches 86 only after the completed door gate"));
                State->CombatSeconds = Flow->GetRemainingSeconds();
                Check(State->CombatSeconds > 9.f && State->CombatSeconds <= 10.f, TEXT("The ten-second rescue clock starts after the independent introduction"));
                for (TActorIterator<AOBEncounterSpawner> It(W); It; ++It)
                    if (It->GetPreparedPointIndex() == 86)
                        if (auto* Enemy = It->GetSpawnedEnemy())
                        {
                            if (It->bParticipatesInCombat)
                            {
                                State->Combatants.Add(Enemy); ++State->FinalCombatants;
                                Check(FMath::IsNearlyEqual(Enemy->CustomTimeDilation, 1.f)
                                    && (!Enemy->GetController() || FMath::IsNearlyEqual(Enemy->GetController()->CustomTimeDilation, 1.f)),
                                    TEXT("The three-second introduction restores normal actor rates before input"));
                            }
                            else if (auto* Hostage = Cast<AOBHostageCharacter>(Enemy))
                            { State->HostageActors.Add(Hostage); ++State->Hostages; }
                        }
                Check(State->FinalCombatants > 0 && State->Hostages == 2, TEXT("The saved final encounter prepares combatants and both dedicated hostages"));
                Check(FMath::IsNearlyEqual(W->GetWorldSettings()->GetEffectiveTimeDilation(), 1.f)
                    && FMath::IsNearlyEqual(P->CustomTimeDilation, 1.f), TEXT("The player, movie clock and world keep normal time"));
                State->Step = 6; State->StepStarted = Now; return;
            }
            if (State->Step == 6)
            {
                if (Now - State->StepStarted < .65) return;
                if (!State->bHostagesCaptured)
                {
                    FScreenshotRequest::RequestScreenshot(Output/(State->Mode+TEXT("-hostages-before-shot.png")),true,false);
                    State->bHostagesCaptured=true; return;
                }
                if (FParse::Param(FCommandLine::Get(),TEXT("Zone6HostageGroundingOnly")))
                {
                    FCollisionQueryParams Query(SCENE_QUERY_STAT(OBHostageGroundingProbe),false);
                    for (TActorIterator<ACharacter> It(W);It;++It) Query.AddIgnoredActor(*It);
                    for (TActorIterator<AOBEncounterSpawner> It(W);It;++It)
                        if (It->GetPreparedPointIndex()==86 && !It->bParticipatesInCombat)
                            State->Log+=FString::Printf(TEXT("hostage_spawner=%s position=%s\n"),*It->GetName(),*It->GetActorLocation().ToString());
                    for (const auto& Weak:State->HostageActors)
                    {
                        auto* Hostage=Weak.Get();if (!Hostage) continue;
                        auto* Mesh=Hostage->GetMesh();TArray<FFinalSkinVertex> Vertices;
                        Mesh->GetCPUSkinnedVertices(Vertices,0);
                        FVector Lowest(0,0,TNumericLimits<double>::Max());
                        for (const auto& Vertex:Vertices)
                        {
                            const FVector Position=Mesh->GetComponentTransform().TransformPosition(FVector(Vertex.Position));
                            if (Position.Z<Lowest.Z) Lowest=Position;
                        }
                        FHitResult Floor;const FVector Center=Hostage->GetActorLocation();
                        const bool Hit=W->LineTraceSingleByChannel(Floor,Center+FVector(0,0,20),Center-FVector(0,0,500),ECC_Pawn,Query);
                        State->Log+=FString::Printf(TEXT("hostage=%s actor=%s capsule_half=%.3f mesh_relative=%s lowest_vertex=%s vertices=%d floor=%s floor_z=%.3f visible_gap_cm=%.3f capsule_gap_cm=%.3f\n"),
                            *Hostage->GetName(),*Center.ToString(),Hostage->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),*Mesh->GetRelativeLocation().ToString(),
                            *Lowest.ToString(),Vertices.Num(),*GetNameSafe(Floor.GetComponent()),Floor.ImpactPoint.Z,Lowest.Z-Floor.ImpactPoint.Z,
                            Center.Z-Hostage->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()-Floor.ImpactPoint.Z);
                        if (FParse::Param(FCommandLine::Get(),TEXT("Zone6CheckGrounded")))
                            Check(Hit && Vertices.Num()>0 && FMath::Abs(Lowest.Z-Floor.ImpactPoint.Z)<3.f,TEXT("Both visible kneeling hostages rest within three centimeters of the actual floor"));
                    }
                    Finish();return;
                }
                const double FadeSeconds = State->FadeFinishedAt - State->FadeStartedAt;
                const double FullBlackSeconds = State->FullBlackFinishedAt - State->FullBlackStartedAt;
                State->Log += FString::Printf(TEXT("breach_fade_seconds=%.3f full_black_seconds=%.3f explosion_instances=%d explosion_started_while_black=%d\n"),
                    FadeSeconds, FullBlackSeconds, State->ExplosionInstances.Num(), State->bExplosionOnlyWhileBlack);
                Check(State->FadeStartedAt >= 0. && State->FadeFinishedAt >= 0. && FadeSeconds >= 2.6 && FadeSeconds <= 3.1,
                    TEXT("Fade-out and fade-in are separate from the two-second full-black hold"));
                Check(State->FullBlackStartedAt >= 0. && State->FullBlackFinishedAt >= 0. && FullBlackSeconds >= 1.9 && FullBlackSeconds <= 2.2
                    && State->bBlackHoldStill, TEXT("The actual screen stays completely black for two seconds with movement stopped"));
                Check(State->ExplosionInstances.Num() == 1 && State->bExplosionOnlyWhileBlack,
                    TEXT("One real explosion AudioComponent starts while the screen is completely black"));
                Check(Flow->GetRemainingSeconds() < State->CombatSeconds - .4f, TEXT("The live hostage countdown advances during final combat"));
                for (const auto& Hostage : State->HostageActors)
                    if (Hostage.IsValid())
                    {
                        TInlineComponentArray<UBoxComponent*> Boxes(Hostage.Get());
                        for (const auto* Box : Boxes)
                        {
                            Check(Box->GetScaledBoxExtent().GetMax() < 50.f, TEXT("Live imported hostage hit boxes remain human-sized"));
                            Check(FVector::Distance(Box->GetComponentLocation(), Hostage->GetMesh()->GetSocketLocation(Box->GetAttachSocketName())) < 35.f,
                                TEXT("Live imported hostage offsets remain on the animated limb"));
                        }
                    }
                PC->SetCrouchMode(P->bIsCrouched);
                auto ShootHead = [&](AOBEnemyCharacter* Target) {
                    if (!IsValid(Target)) return;
                    FHitResult Resolved;
                    const auto Handle = PC->OnShotResolved.AddLambda([&Resolved](const FHitResult& Hit) { Resolved = Hit; });
                    const FVector Start = P->GetPlayerCamera()->GetComponentLocation();
                    const FVector Aim = Target->GetMesh()->GetSocketLocation(TEXT("head"));
                    PC->FireAlongRay(Start, Aim + (Aim - Start).GetSafeNormal() * 50.f);
                    PC->OnShotResolved.Remove(Handle);
                    State->Log += FString::Printf(TEXT("shot_target=%s actual_actor=%s component=%s origin=%s aim=%s\n"),
                        *GetNameSafe(Target), *GetNameSafe(Resolved.GetActor()), *GetNameSafe(Resolved.GetComponent()), *Start.ToCompactString(), *Aim.ToCompactString());
                    Check(Resolved.GetActor() == Target, TEXT("Production camera shot resolves the visible intended character"));
                };
                if (State->Mode.Equals(TEXT("Success"), ESearchCase::IgnoreCase))
                {
                    const auto Enemies = Action->GetEncounterEnemies();
                    for (AOBEnemyCharacter* Enemy : Enemies)
                    {
                        ShootHead(Enemy);
                        Check(Flow->DidSucceed() || Flow->GetPhase() == EOBStagePhase::Combat,
                            TEXT("An enemy shot never selects the hostage failure ending"));
                    }
                }
                else if (State->Mode.Equals(TEXT("Timeout"), ESearchCase::IgnoreCase)) Flow->RemainingSeconds = .01f;
                else
                {
                    Check(!State->HostageActors.IsEmpty() && State->HostageActors[0].IsValid(), TEXT("An actual final hostage is available for the failure path"));
                    AOBHostageCharacter* Target = nullptr;
                    const bool bWorker = State->Mode.Equals(TEXT("FailWorker"), ESearchCase::IgnoreCase);
                    // Imported mesh filenames can change; the saved Blueprint defines which hostage this is.
                    auto* WorkerClass=LoadClass<AOBHostageCharacter>(nullptr,TEXT("/Game/Blueprints/Hostage/BP_HostageWorker.BP_HostageWorker_C"));
                    Check(WorkerClass!=nullptr,TEXT("The saved worker-hostage class is available for fixture selection"));
                    for (const auto& Hostage : State->HostageActors)
                        if (Hostage.IsValid())
                        {
                            const auto* Mesh = Hostage->GetMesh()->GetSkeletalMeshAsset();
                            Check(Mesh != nullptr, TEXT("The hostage shot fixture requires an assigned visible mesh"));
                            if (Mesh && WorkerClass && Hostage->IsA(WorkerClass)==bWorker) Target = Hostage.Get();
                        }
                    Check(Target != nullptr, TEXT("The requested visible hostage is available for the shot fixture"));
                    const int32 ShotsBefore=Flow->StageRecord.ShotsFired,HitsBefore=Flow->StageRecord.ValidHits;
                    ShootHead(Target);
                    Check(Flow->GetPhase()==EOBStagePhase::Ending && !Flow->DidSucceed(),TEXT("Direct hostage damage immediately selects failure before the deadline"));
                    Check(Flow->StageRecord.ShotsFired==ShotsBefore+1 && Flow->StageRecord.ValidHits==HitsBefore,TEXT("The actual hostage shot counts as a shot without a valid enemy hit"));
                }
                if (Flow->Cinematic)
                {
                    Flow->Cinematic->OnVideoOpened.AddLambda(ObserveEndingOpened); State->bBoundMediaOpen = true;
                    if (Flow->bEndingVideoOpened) ObserveEndingOpened();
                }
                State->Step = 7; State->StepStarted = Now; return;
            }
            if (State->Step == 7)
            {
                const bool ExpectedSuccess = State->Mode.Equals(TEXT("Success"), ESearchCase::IgnoreCase);
                if (Flow->Cinematic && !State->bBoundMediaOpen)
                {
                    Flow->Cinematic->OnVideoOpened.AddLambda(ObserveEndingOpened); State->bBoundMediaOpen = true;
                    if (Flow->bEndingVideoOpened) ObserveEndingOpened();
                }
                if (Flow->GetPhase()==EOBStagePhase::Ending && Flow->bWaitingForOutcomeSubtitle)
                    State->bSawOutcomeFadeOut=true;
                if (Flow->GetPhase() == EOBStagePhase::Ending && Flow->Cinematic && Flow->bEndingVideoOpened && !Flow->bWaitingForOutcomeSubtitle)
                {
                    auto* Media = Flow->Cinematic->GetMediaPlayer();
                    const double Clock = Media ? Media->GetTime().GetTotalSeconds() : -1.;
                    const bool Matches = CaptionMatches(Flow->GetOverlay(), Flow->FindScript(ExpectedSuccess ? 2022 : 2023));
                    if (!State->bSawMediaOpen) ObserveEndingOpened();
                    State->MaxMediaClock = FMath::Max(State->MaxMediaClock, Clock);
                    if (Media) State->MediaDuration = FMath::Max(State->MediaDuration, Media->GetDuration().GetTotalSeconds());
                    State->bCaptionStayedVisible &= Matches && !Flow->NarrationVoice && !Flow->ActiveScriptId;
                    if (auto* Overlay=Flow->GetOverlay())
                        State->bNoRestartedFade &= State->MovieCaptionRequest==Overlay->SubtitleRequestNumber;
                    if (Clock > 1.5 && Matches) State->bLateCaption = true;
                    if (!State->bMoviePictureCaptured && Clock > 2. && Matches)
                    {
                        const auto MovieSlot=UGameViewportSubsystem::Get()->GetWidgetSlot(Flow->Cinematic);
                        const auto HudSlot=UGameViewportSubsystem::Get()->GetWidgetSlot(Flow->GetOverlay());
                        Check(MovieSlot.ZOrder!=INDEX_NONE && MovieSlot.ZOrder<HudSlot.ZOrder
                            && Flow->GetOverlay()->GetSubtitleWidget()->GetRenderOpacity()>.95f
                            && Flow->GetOverlay()->GetSubtitleWidget()->GetCachedGeometry().GetLocalSize().Y>0,
                            TEXT("The authored caption is opaque, laid out, and mounted above the movie"));
                        // Replace the callback-time shot after the movie has produced a visible frame.
                        FScreenshotRequest::RequestScreenshot(Output / (State->Mode + TEXT("-ending-caption.png")), true, false);
                        State->bMoviePictureCaptured = true;
                    }
                }
                if (Flow->GetPhase() != EOBStagePhase::Complete) return;
                if (State->bResultCaptured) { Finish(); return; }
                if (auto* Overlay=Flow->GetOverlay())
                {
                    auto* Result=Overlay->GetTrainingResultWidget();
                    auto* Title=Result ? Cast<UTextBlock>(Result->GetWidgetFromName(TEXT("StageTitleText"))) : nullptr;
                    Check(Title && Title->GetText().ToString()==TEXT("훈련 종료"),TEXT("Every rescue outcome displays neutral training completion"));
                    Check(Result && Result->GetWidgetFromName(TEXT("StageResultPanel"))->IsVisible(),TEXT("Stage completion displays the portrait report"));
                    Check(Result && Result->GetWidgetFromName(TEXT("ResultBorder"))->GetVisibility()==ESlateVisibility::Collapsed,TEXT("Stage report has no PASS or FAIL stamp"));
                    Check(Overlay->GetWidgetFromName(TEXT("TrainingBackgroundBlur"))->IsVisible(),TEXT("Result report keeps the background blurred"));
                }
                Check(Flow->DidSucceed() == ExpectedSuccess, TEXT("Normal gameplay chooses the requested clear or failure outcome"));
                Check(State->bMovieGainValid, TEXT("The actual ending media audio uses the requested 3.0x volume"));
                Check(State->bSameSubtitle && State->bNoRestartedFade, TEXT("Movies use the gameplay Subtitle instance without restarting its authored animation every frame"));
                Check(State->bSawOutcomeFadeOut && Flow->GetOverlay() && !Flow->GetOverlay()->IsCinematicSubtitleMode(),
                    TEXT("Results wait for the authored subtitle fade-out and restore the normal overlay"));
                Check(State->bSawMediaOpen && State->bCaptionAtOpen, TEXT("The outcome caption appears on the actual movie from its first media-clock sample"));
                Check(State->bMoviePictureCaptured, TEXT("A decoded movie frame and caption were captured after the opening black frame"));
                Check(State->bCaptionStayedVisible && State->bLateCaption && State->MaxMediaClock > 1.5,
                    TEXT("The caption follows the movie timeline beyond the old 1.2-second text duration without a second voice"));
                Check(FMath::Abs(State->MediaDuration - (ExpectedSuccess ? 8.375 : 6.)) < .15
                    && State->MaxMediaClock >= State->MediaDuration - .3,
                    TEXT("The unaccelerated movie reaches its full authored media duration with the caption present"));
                Check(!Flow->ActiveScriptId && Flow->NarrationQueue.IsEmpty(), TEXT("Movie completion shows results without a delayed duplicate outcome caption"));
                Check(State->bFlyingCaptured, TEXT("Flying debris was observed during normal travel without holding stage progression"));
                bool Restored = true;
                for (const auto& Enemy : State->Combatants) if (Enemy.IsValid()) Restored &= FMath::IsNearlyEqual(Enemy->CustomTimeDilation, 1.f);
                Check(Restored, TEXT("Ending cleanup restores the enemies' original local time dilation"));
                FScreenshotRequest::RequestScreenshot(Output/(State->Mode+TEXT("-result.png")),true,false);
                State->bResultCaptured=true;
            }
        }), .05f, true);
    }
};
static FAutoConsoleCommand GOBStageZone6PIE(TEXT("OB.Stage.Zone6PIE"),
    TEXT("Saved Zone6 dialogue, fresh OSC breach, actual Chaos destruction and ending media/caption PIE check. Exits this editor."),
    FConsoleCommandDelegate::CreateStatic(&FOBStageZone6MapSmoke::Start));
#endif

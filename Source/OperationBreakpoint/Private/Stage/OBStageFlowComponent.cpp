#include "Stage/OBStageFlowComponent.h"
#include "Character/OBHostageCharacter.h"
#include "Stage/OBStageCinematicWidget.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Character/OBActionComponent.h"
#include "Character/OBEnemyCharacter.h"
#include "Character/OBAllyCharacter.h"
#include "Character/OBAllyNavigationComponent.h"
#include "Character/OBAllySplineComponent.h"
#include "AI/OBAIController.h"
#include "World/OBEncounterTrigger.h"
#include "World/OBEncounterSpawner.h"
#include "World/OBBulletActor.h"
#include "World/OBBreachDoor.h"
#include "World/OBBreachCharge.h"
#include "World/OBRailPath.h"
#include "World/OBSquadRendezvous.h"
#include "Components/SplineComponent.h"
#include "MediaPipe/OBMediaPipeOSCSubsystem.h"
#include "Engine/GameInstance.h"
#include "Widget/OBHUD.h"
#include "Widget/OBPlayerOverlayWidget.h"
#include "Widget/OBDamageScreenWidget.h"
#include "Training/OBTrainingResultWidget.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/OBPlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Engine/DamageEvents.h"
#include "MediaPlayer.h"
#include "MediaSource.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameModeBase.h"
#include "Camera/PlayerCameraManager.h"
#include "Core/OBGameInstance.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogOBStage,Log,All);

UOBStageFlowComponent::UOBStageFlowComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    ScriptTable=TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Blueprints/Stage/Data/DT_Stage_01_Script.DT_Stage_01_Script")));
    OpeningVideo=TSoftObjectPtr<UMediaSource>(FSoftObjectPath(TEXT("/Game/Movies/Stage_01_Opening.Stage_01_Opening")));
    OpeningVoice=TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Assets/FX/SFX/Stage_01/Stage_01_Opening_Briefing.Stage_01_Opening_Briefing")));
    ClearVideo=TSoftObjectPtr<UMediaSource>(FSoftObjectPath(TEXT("/Game/Movies/Stage_01_Ending_Clear.Stage_01_Ending_Clear")));
    FailVideo=TSoftObjectPtr<UMediaSource>(FSoftObjectPath(TEXT("/Game/Movies/Stage_01_Ending_Fail.Stage_01_Ending_Fail")));
    SupportShotSound=TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Assets/FX/SFX/PlaySound/Shooting.Shooting")));
    BreachExplosionSound=TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Assets/FX/SFX/PlaySound/Bomb_attachment.Bomb_attachment")));
}
void UOBStageFlowComponent::BeginPlay()
{
    Super::BeginPlay();
    Player=Cast<AOBPlayerCharacter>(GetOwner());
    bActive=bEnabled && Player && UGameplayStatics::GetCurrentLevelName(this,true)==TEXT("Stage_01");
    if (!bActive) { SetComponentTickEnabled(false);return; }
    BindResultTracking();
    if (!bPlayOpening) StartResultTracking();
    LoadedScripts=ScriptTable.LoadSynchronous();
    auto* Rail=Player->GetPlayerSplineComponent();
    Rail->OnActionPointReached.AddUObject(this,&ThisClass::HandleActionPoint);
    Rail->OnTravelStarted.AddUObject(this,&ThisClass::HandleTravel);
    for (TActorIterator<AOBEncounterTrigger> It(GetWorld());It;++It)
        It->OnEncounterTriggered.AddUniqueDynamic(this,&ThisClass::HandleEncounterTriggered);
    if (auto* GI=GetWorld()->GetGameInstance())
        if (auto* Motion=GI->GetSubsystem<UOBMediaPipeOSCSubsystem>())
        {
            MotionInput=Motion;
        }
    RemainingLives=FMath::Clamp(LivesPerZone,1,5);
    EnsureDamageScreen();
    if (const auto* GI=GetWorld()->GetGameInstance<UOBGameInstance>()) bStageShortcutDemo=GI->IsStageShortcutEnabled();
    if (bStageShortcutDemo) StageRecord.bUsedTestInputs=true;
    if (const auto* Mode=GetWorld()->GetAuthGameMode(); Mode && UGameplayStatics::ParseOption(Mode->OptionsString,TEXT("OBZone6Demo"))==TEXT("1"))
    {
        bPendingZone6Demo=true;StageRecord.bUsedTestInputs=true;
        Phase=EOBStagePhase::Opening;HoldPlayerMovement();return;
    }
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    // A fresh map restores the Chaos door, encounters and presentation for each QA run.
    if (bUseZone6Breach)
        if (const auto* Mode=GetWorld()->GetAuthGameMode())
        {
            const FString Point=UGameplayStatics::ParseOption(Mode->OptionsString,TEXT("OBQABreach"));
            if (Point==TEXT("83")) PendingQABreachPoint=Zone6DialoguePoint;
            else if (Point==TEXT("85")) PendingQABreachPoint=Zone6BreachPoint;
        }
    if (PendingQABreachPoint!=INDEX_NONE) { StageRecord.bUsedTestInputs=true;Phase=EOBStagePhase::Opening;HoldPlayerMovement();return; }
#endif
    Phase=bPlayOpening ? EOBStagePhase::Opening : EOBStagePhase::Travel;
    if (bPlayOpening)
    {
        Rail->SetStageMovementBlocked(true);
        auto* Movement=Player->GetCharacterMovement();
        OpeningMovementMode=Movement->MovementMode;OpeningCustomMovementMode=Movement->CustomMovementMode;bOpeningMovementSaved=true;
        Movement->StopMovementImmediately();Movement->DisableMovement();
    }
}
UOBPlayerOverlayWidget* UOBStageFlowComponent::GetOverlay() const
{
    auto* PC=Player ? Cast<AOBPlayerController>(Player->GetController()) : nullptr;
    auto* HUD=PC ? PC->GetOBHUD() : nullptr;
    return HUD ? HUD->GetPlayerOverlayWidget() : nullptr;
}
void UOBStageFlowComponent::EnsureDamageScreen()
{
    if (DamageScreen || !Player) return;
    auto* PC=Cast<AOBPlayerController>(Player->GetController());
    if (!PC || !PC->IsLocalController() || !GetWorld()->IsGameWorld()) return;
    DamageScreen=CreateWidget<UOBDamageScreenWidget>(PC);
    if (DamageScreen && GetWorld()->GetGameViewport()) DamageScreen->AddToPlayerScreen(-10);
}
void UOBStageFlowComponent::ClearDamageScreen()
{
    if (DamageScreen) DamageScreen->ResetEffects();
}
const FOBStageScriptRow* UOBStageFlowComponent::FindScript(int32 Id) const
{
    return LoadedScripts && LoadedScripts->GetRowStruct()==FOBStageScriptRow::StaticStruct()
        ? LoadedScripts->FindRow<FOBStageScriptRow>(FName(*FString::FromInt(Id)),TEXT("Stage script"),false) : nullptr;
}
void UOBStageFlowComponent::StartOpening()
{
    auto* PC=Player ? Cast<AOBPlayerController>(Player->GetController()) : nullptr;
    if (!PC || !PC->IsLocalController()) return;
    bVideoFinished=bVoiceFinished=bVoiceStarted=false;
    Cinematic=CreateWidget<UOBStageCinematicWidget>(PC);
    // The persistent HUD owns captions; mount the movie below that same HUD.
    Cinematic->AddToPlayerScreen(-100);
    Cinematic->OnVideoOpened.AddUObject(this,&ThisClass::HandleVideoOpened);
    Cinematic->OnVideoFinished.AddUObject(this,&ThisClass::HandleVideoFinished);
    BeginPresentationSubtitles();
    Cinematic->OpenVideo(OpeningVideo.LoadSynchronous(),true,VoiceVolumeMultiplier);
    UE_LOG(LogOBStage,Display,TEXT("Opening started | Input and rail movement blocked"));
}
void UOBStageFlowComponent::HandleVideoOpened()
{
    if (Phase==EOBStagePhase::Ending)
    {
        bEndingVideoOpened=true;
        UpdateEndingSubtitle();
        return;
    }
    if (Phase!=EOBStagePhase::Opening || bVoiceStarted) return;
    bVoiceStarted=true;bVoiceFinished=false;VoiceElapsed=0;VoiceWatchdogElapsed=VoicePlaybackCallbackAge=0;
    if (auto* Sound=OpeningVoice.LoadSynchronous())
    {
        Voice=UGameplayStatics::CreateSound2D(this,Sound,FMath::Max(0.f,VoiceVolumeMultiplier));
        if (Voice)
        {
            Voice->OnAudioFinished.AddDynamic(this,&ThisClass::HandleOpeningVoiceFinished);
            Voice->OnAudioPlaybackPercent.AddDynamic(this,&ThisClass::HandleVoicePlayback);
            Voice->Play();
        }
    }
    UpdateOpeningSubtitle();FinishOpeningIfReady();
}
void UOBStageFlowComponent::HandleVoicePlayback(const USoundWave* Wave,float Percent)
{
    if (Phase==EOBStagePhase::Opening && Wave)
    { VoiceElapsed=Wave->GetDuration()*FMath::Clamp(Percent,0.f,1.f);VoicePlaybackCallbackAge=0; }
}
void UOBStageFlowComponent::TickOpeningVoiceClock(float DeltaTime)
{
    if (!bVoiceStarted || bVoiceFinished || (Voice && Voice->GetPlayState()==EAudioComponentPlayState::Paused)) return;
    VoiceWatchdogElapsed+=DeltaTime;VoicePlaybackCallbackAge+=DeltaTime;
    // PIE may disable audio, and some audio backends do not report playback
    // percentages. Captions must still progress while the movie is playing.
    // Keep the real voice clock authoritative whenever callbacks are arriving.
    if (VoicePlaybackCallbackAge>.5f) VoiceElapsed=FMath::Max(VoiceElapsed,VoiceWatchdogElapsed);
    UpdateOpeningSubtitle();
    const auto* Sound=OpeningVoice.Get();float Duration=Sound ? Sound->GetDuration() : 0.f;
    for (int32 Id=2001;Id<=2004;++Id) if (const auto* Row=FindScript(Id)) Duration=FMath::Max(Duration,Row->OpeningEndSeconds);
    if (Duration>0.f && (((!Voice || VoicePlaybackCallbackAge>1.f) && VoiceElapsed>=Duration) || VoiceWatchdogElapsed>Duration+5.f))
        HandleOpeningVoiceFinished();
}
void UOBStageFlowComponent::HandleOpeningVoiceFinished()
{
    bVoiceFinished=true;
    ShowPresentationSubtitle(0);
    FinishOpeningIfReady();
}
void UOBStageFlowComponent::HandleVideoFinished()
{
    bVideoFinished=true;
    if (Phase==EOBStagePhase::Opening)
    {
        // A missing/unopenable video still plays the briefing on black.
        if (!bVoiceStarted) HandleVideoOpened();
        FinishOpeningIfReady();
    }
    else if (Phase==EOBStagePhase::Ending) FinishEndingVideo();
}
void UOBStageFlowComponent::FinishOpeningIfReady()
{
    if (Phase==EOBStagePhase::Opening && bVideoFinished && bVoiceFinished) FinishOpening();
}
void UOBStageFlowComponent::FinishOpening()
{
    ClearPresentation();Phase=EOBStagePhase::Travel;
    StartResultTracking();
    if (auto* Overlay=GetOverlay()) { Overlay->SetVisibility(ESlateVisibility::HitTestInvisible);Overlay->SetStageMode(); }
    BeginDeparture(false);
    UE_LOG(LogOBStage,Display,TEXT("Opening finished | Waiting for first departure narration"));
}
void UOBStageFlowComponent::UpdateOpeningSubtitle()
{
    if (!Cinematic || !bVoiceStarted || bVoiceFinished) return;
    int32 Id=0;
    for (int32 I=2001;I<=2004;++I)
        if (const auto* Row=FindScript(I); Row && VoiceElapsed>=Row->OpeningStartSeconds && VoiceElapsed<Row->OpeningEndSeconds) { Id=I;break; }
    if (OpeningSubtitleId==Id) return;
    OpeningSubtitleId=Id;
    ShowPresentationSubtitle(Id);
    UE_LOG(LogOBStage,Display,TEXT("Opening subtitle | ID=%d AudioSeconds=%.2f"),Id,VoiceElapsed);
}
void UOBStageFlowComponent::UpdateEndingSubtitle()
{
    if (!Cinematic || Phase!=EOBStagePhase::Ending || !bEndingVideoOpened || bWaitingForOutcomeSubtitle) return;
    // Ending movies already contain their voice track. Keep the outcome caption on that movie,
    // from media-open until media-end, without starting a second voice or a post-movie caption.
    ShowPresentationSubtitle(bSucceeded ? 2022 : 2023);
}
void UOBStageFlowComponent::BeginPresentationSubtitles()
{
    PresentationSubtitleId=OpeningSubtitleId=0;bWaitingForOutcomeSubtitle=false;
    if (auto* Overlay=GetOverlay())
    {
        Overlay->CancelTrainingDialogue();
        Overlay->OnSubtitleFinished.AddUniqueDynamic(this,&ThisClass::HandlePresentationSubtitleFinished);
        Overlay->SetCinematicSubtitles(true);
    }
}
void UOBStageFlowComponent::ShowPresentationSubtitle(int32 Id)
{
    if (PresentationSubtitleId==Id) return;
    PresentationSubtitleId=Id;
    if (auto* Overlay=GetOverlay())
    {
        if (const auto* Row=FindScript(Id); Row && !Row->Text.IsEmpty()) Overlay->ShowPersistentSubtitle(Row->SpeakerName,Row->Text);
        else Overlay->DismissSubtitle();
    }
}
void UOBStageFlowComponent::HandlePresentationSubtitleFinished()
{
    if (bWaitingForOutcomeSubtitle && Phase==EOBStagePhase::Ending)
    { bWaitingForOutcomeSubtitle=false;ShowCompletion(); }
}
void UOBStageFlowComponent::HandleTravel()
{
    if (!bActive || Phase==EOBStagePhase::Opening || Phase==EOBStagePhase::QA || Phase==EOBStagePhase::Complete || Phase==EOBStagePhase::Ending) return;
    ClearDamageScreen();
    // CompleteAction broadcasts travel after ReleaseZone6Travel starts the entry scene.
    // Keep that scene's slow motion and input hold throughout the breached doorway.
    if (!bZone6EntrySceneStarted) RestoreSlowMotion();
    Phase=EOBStagePhase::Travel;
    if (bStageShortcutDemo && !bStageShortcutTransferred && CurrentZone==1)
    { BeginShortcutTransition();return; }
    RefreshHUD();
}
bool UOBStageFlowComponent::DeferInitialTravelUntilNarrationDone()
{
    if (!bActive || bReleasingDeparture || CurrentZone!=0 || Phase==EOBStagePhase::QA || Phase==EOBStagePhase::Ending || Phase==EOBStagePhase::Complete) return false;
    if (Phase==EOBStagePhase::Opening || bDeparturePending) return true;
    BeginDeparture(false);
    return true;
}
bool UOBStageFlowComponent::DeferTravelUntilNarrationDone()
{
    if (!bActive || bReleasingDeparture) return false;
    if (bDeparturePending) return true;
    if (CurrentZone<1 || CurrentZone>=6 || (Phase!=EOBStagePhase::Combat && Phase!=EOBStagePhase::SupportingFire)) return false;
    BeginDeparture(true);
    return true;
}
void UOBStageFlowComponent::BeginDeparture(bool bViaAction)
{
    if (bDeparturePending || !Player || CurrentZone>=6) return;
    bDeparturePending=true;bDepartureViaAction=bViaAction;Phase=EOBStagePhase::AwaitingDeparture;
    HoldPlayerMovement();
    RestoreSlowMotion();RefreshHUD();
    // The departure order finishes before player alignment and squad release.
    PlayAutomaticScript(2005+CurrentZone*2,CurrentZone+1);
    UE_LOG(LogOBStage,Display,TEXT("Departure waiting | Zone=%d Script=%d"),CurrentZone,2005+CurrentZone*2);
    if (!ActiveScriptId && NarrationQueue.IsEmpty()) ReleaseDeparture();
}
void UOBStageFlowComponent::HoldPlayerMovement()
{
    if (!Player) return;
    Player->GetPlayerSplineComponent()->SetStageMovementBlocked(true);
    if (auto* Movement=Player->GetCharacterMovement())
    {
        if (!bOpeningMovementSaved)
        {
            OpeningMovementMode=Movement->MovementMode;OpeningCustomMovementMode=Movement->CustomMovementMode;bOpeningMovementSaved=true;
        }
        Movement->StopMovementImmediately();Movement->DisableMovement();
    }
}
void UOBStageFlowComponent::ReleaseDeparture()
{
    if (!bDeparturePending || Phase!=EOBStagePhase::AwaitingDeparture || !Player) return;
    ClearDamageScreen();
    const bool bViaAction=bDepartureViaAction;
    bDeparturePending=bDepartureViaAction=false;bReleasingDeparture=true;Phase=EOBStagePhase::Travel;
    RestoreOpeningMovement();Player->GetPlayerSplineComponent()->SetStageMovementBlocked(false);RefreshHUD();
    UE_LOG(LogOBStage,Display,TEXT("Departure narration finished | Zone=%d | Releasing rail alignment"),CurrentZone);
    if (bViaAction) Player->GetActionComponent()->CompleteAction();
    else Player->GetPlayerSplineComponent()->StartMoveToNextPoint();
    bReleasingDeparture=false;
}
int32 UOBStageFlowComponent::GetNextStageStop(int32 AfterPoint,int32 AuthoredNext) const
{
    if (!bActive || !bUseZone6Breach || Phase==EOBStagePhase::QA) return AuthoredNext;
    int32 Next=AuthoredNext;
    for (int32 Point : {Zone6DialoguePoint,Zone6BreachPoint,Zone6EncounterPoint})
        if (Point>AfterPoint && (Next==INDEX_NONE || Point<Next)) Next=Point;
    return Next;
}
bool UOBStageFlowComponent::CanActivateEncounter(int32 Point) const
{
    if (Phase==EOBStagePhase::ShortcutTransition || (bStageShortcutTransferred && Point<Zone6EncounterPoint)) return false;
    if (!bActive || !bUseZone6Breach || Phase==EOBStagePhase::QA || Point!=Zone6EncounterPoint) return true;
    const auto* Rail=Player ? Player->GetPlayerSplineComponent() : nullptr;
    return bBreachCompleted && Rail && Rail->GetCurrentPointIndex()==Zone6EncounterPoint && !Rail->IsMoving()
        && Phase!=EOBStagePhase::Ending && Phase!=EOBStagePhase::Complete;
}
void UOBStageFlowComponent::PrepareEncounterAtStop(int32 Point)
{
    if (!bActive || Point!=Zone6EncounterPoint || !CanActivateEncounter(Point)) return;
    for (TActorIterator<AOBEncounterTrigger> It(GetWorld());It;++It)
        if (It->GetPointIndex()==Point) It->ActivateForPlayer(Player);
}
void UOBStageFlowComponent::BeginShortcutTransition()
{
    Phase=EOBStagePhase::ShortcutTransition;HoldPlayerMovement();CancelNarration();
    ShortcutFadeStep=0;ShortcutFadeRemaining=.35f;
    if (auto* PC=Cast<APlayerController>(Player->GetController()); PC && PC->PlayerCameraManager)
        PC->PlayerCameraManager->StartCameraFade(0.f,1.f,ShortcutFadeRemaining,FLinearColor::Black,false,true);
    RefreshHUD();
    UE_LOG(LogOBStage,Display,TEXT("Stage shortcut fade-out | CompletedZone=1 | Ammo is preserved"));
}
bool UOBStageFlowComponent::MoveShortcutToZone5Exit()
{
    for (TActorIterator<AOBAllyCharacter> It(GetWorld());It;++It)
    { It->GetNavigationComponent()->StopNavigation();It->GetRouteComponent()->StopRoute(); }
    for (TActorIterator<AOBSquadRendezvous> It(GetWorld());It;++It)
        if (It->DepartAfterZone<5) It->bEnabled=false;
    // Suppress the skipped stop's overlap/action encounter while positioning.
    bStageShortcutTransferred=true;CurrentZone=5;RemainingSeconds=0;
    FString Error;
    if (!Player->GetPlayerSplineComponent()->StartAtStageStop(72,Error))
    { UE_LOG(LogOBStage,Error,TEXT("Stage shortcut checkpoint rejected: %s"),*Error);return false; }
    HoldPlayerMovement();
    int32 Staged=0;
    for (TActorIterator<AOBSquadRendezvous> It(GetWorld());It;++It)
        if (It->bEnabled && It->DepartAfterZone==5)
        {
            if (!It->StageForPlayer(Player)) return false;
            ++Staged;
        }
    UE_LOG(LogOBStage,Display,TEXT("Stage shortcut checkpoint | Point=72 Zone=5 SquadSections=%d | Zones2-5 remain unplayed"),Staged);
    return Staged>0;
}
void UOBStageFlowComponent::TickShortcutTransition(float DeltaTime)
{
    auto* PC=Player ? Cast<APlayerController>(Player->GetController()) : nullptr;
    auto* Camera=PC ? PC->PlayerCameraManager.Get() : nullptr;
    ShortcutFadeRemaining=FMath::Max(0.f,ShortcutFadeRemaining-FMath::Max(0.f,DeltaTime));
    if (ShortcutFadeRemaining>0.f) return;
    if (ShortcutFadeStep==0)
    {
        if (Camera) Camera->SetManualCameraFade(1.f,FLinearColor::Black,false);
        ShortcutFadeStep=1;ShortcutFadeRemaining=.3f;return;
    }
    if (ShortcutFadeStep==1)
    {
        if (!MoveShortcutToZone5Exit())
        { if (Camera) Camera->StopCameraFade();Phase=EOBStagePhase::Inactive;return; }
        ShortcutFadeStep=2;ShortcutFadeRemaining=.35f;
        if (Camera) Camera->StartCameraFade(1.f,0.f,ShortcutFadeRemaining,FLinearColor::Black,false,false);
        return;
    }
    if (Camera) Camera->StopCameraFade();
    Phase=EOBStagePhase::Travel;BeginDeparture(false);
}
void UOBStageFlowComponent::StartZone6Demo()
{
    // A fresh level owns fresh hostages, combatants, ammunition and result state.
    ClearPresentation();RestoreSlowMotion();ClearDamageScreen();RestoreOpeningMovement();
    CurrentZone=6;Phase=EOBStagePhase::Travel;bBreachCompleted=true;
    if (auto* Overlay=GetOverlay())
    { Overlay->CancelTrainingDialogue();Overlay->SetVisibility(ESlateVisibility::HitTestInvisible);Overlay->SetStageMode(); }
    FString Error;
    if (!Player->GetPlayerSplineComponent()->StartAtStageStop(Zone6EncounterPoint,Error))
    {
        HoldPlayerMovement();Phase=EOBStagePhase::Inactive;
        UE_LOG(LogOBStage,Error,TEXT("Zone6 demo checkpoint rejected: %s"),*Error);return;
    }
    // The checkpoint is already inside the breached room. Remove only its
    // intact barrier rather than replaying Chaos destruction and the blackout.
    AOBBreachDoor* Door=Zone6Door.Get();
    if (!IsValid(Door))
    {
        float Nearest=TNumericLimits<float>::Max();
        for (TActorIterator<AOBBreachDoor> It(GetWorld());It;++It)
        {
            const float Distance=FVector::DistSquared(Player->GetActorLocation(),It->GetActorLocation());
            if (Distance<Nearest) { Nearest=Distance;Door=*It; }
        }
    }
    if (IsValid(Door)) { Door->SetActorHiddenInGame(true);Door->SetActorEnableCollision(false); }
    UE_LOG(LogOBStage,Display,TEXT("Zone6 demo ready | Point=%d | Post-breach intro and normal rescue outcomes"),Zone6EncounterPoint);
}
void UOBStageFlowComponent::BeginZone6Dialogue()
{
    if (bZone6DialogueStarted || !Player) return;
    bZone6DialogueStarted=true;CurrentZone=6;Phase=EOBStagePhase::Zone6Dialogue;
    PrepareZone6Scene();
    RemainingSeconds=0;Zone6GapRemaining=0;
    HoldPlayerMovement();RestoreSlowMotion();DiscardOldNarration(6);RefreshHUD();
    PlayAutomaticScript(2017,6);PlayAutomaticScript(2018,6);
    UE_LOG(LogOBStage,Display,TEXT("Zone6 dialogue hold | Point=%d | Hostage then terrorist"),Zone6DialoguePoint);
}
void UOBStageFlowComponent::BeginZone6Breach()
{
    if (!Player || Phase==EOBStagePhase::Zone6Breach) return;
    CurrentZone=6;Phase=EOBStagePhase::Zone6Breach;RemainingSeconds=0;
    PrepareZone6Scene();
    HoldPlayerMovement();RestoreSlowMotion();RefreshHUD();
    bBreachRequested=false;bBreachSawNeutral=!MotionInput.IsValid() || !MotionInput->IsBreachRecognized();
    ActiveBreachDoor=Zone6Door.Get();
    if (!IsValid(ActiveBreachDoor))
    {
        float Nearest=TNumericLimits<float>::Max();
        for (TActorIterator<AOBBreachDoor> It(GetWorld());It;++It)
        {
            const float Distance=FVector::DistSquared(Player->GetActorLocation(),It->GetActorLocation());
            if (Distance<Nearest) { Nearest=Distance;ActiveBreachDoor=*It; }
        }
    }
    if (IsValid(ActiveBreachDoor)) ActiveBreachDoor->OnBreachCompleted.AddUniqueDynamic(this,&ThisClass::HandleBreachCompleted);
    else UE_LOG(LogOBStage,Error,TEXT("Zone6 breach has no dedicated breach door; progression remains blocked"));
    BeginC4Placement();
    PlayAutomaticScript(2016,6);
    UE_LOG(LogOBStage,Display,TEXT("Zone6 breach armed | Point=%d Door=%s Neutral=%d"),Zone6BreachPoint,*GetNameSafe(ActiveBreachDoor),bBreachSawNeutral);
}
void UOBStageFlowComponent::HandleBreachStateChanged(bool bRecognized)
{
    // The tutorial's legacy two-hand state cannot plant a Stage_01 charge.
}
void UOBStageFlowComponent::HandleBreachCompleted(AOBBreachDoor* Door)
{
    if (Phase!=EOBStagePhase::Zone6Breach || bBreachCompleted || !bBreachRequested || !bBreachBlackHoldStarted || !bBreachPhysicsStarted || Door!=ActiveBreachDoor || !Door || !Door->IsBreached()) return;
    bBreachCompleted=true;
    UE_LOG(LogOBStage,Display,TEXT("Zone6 breach physics completed | Door=%s"),*GetNameSafe(Door));
}
void UOBStageFlowComponent::ReleaseZone6Travel()
{
    if (!Player || (Phase!=EOBStagePhase::Zone6Dialogue && Phase!=EOBStagePhase::Zone6Breach)) return;
    if (Phase==EOBStagePhase::Zone6Breach) StartZone6EntryScene();
    if (ActiveBreachDoor) ActiveBreachDoor->OnBreachCompleted.RemoveAll(this);
    Phase=EOBStagePhase::Travel;RestoreOpeningMovement();
    Player->GetPlayerSplineComponent()->SetStageMovementBlocked(false);RefreshHUD();
    Player->GetActionComponent()->CompleteAction();
    // Missing/non-combat action setup must not strand an otherwise ready rail stop.
    if (Player->GetPlayerSplineComponent()->IsAtActionPoint()) Player->GetPlayerSplineComponent()->StartMoveToNextPoint();
}
void UOBStageFlowComponent::TickZone6Gate(float DeltaTime)
{
    if (Phase==EOBStagePhase::Zone6Dialogue)
    {
        if (ActiveScriptId || !NarrationQueue.IsEmpty()) return;
        if (!bZone6DialogueFinished)
        {
            bZone6DialogueFinished=true;Zone6GapRemaining=FMath::Max(0.f,Zone6DialogueGapSeconds);
            if (Zone6GapRemaining>0.f) return;
        }
        Zone6GapRemaining=FMath::Max(0.f,Zone6GapRemaining-FMath::Max(0.f,DeltaTime));
        if (Zone6GapRemaining<=0.f) ReleaseZone6Travel();
    }
    else if (Phase==EOBStagePhase::Zone6Breach)
    {
        if (!bBreachRequested) { TickC4Placement();return; }
        if (!bBreachBlackHoldStarted)
        {
            BreachFadeOutRemaining=FMath::Max(0.f,BreachFadeOutRemaining-FMath::Max(0.f,DeltaTime));
            if (BreachFadeOutRemaining>0.f) return;
            if (BreachCamera.IsValid() && BreachCamera->FadeAmount<.999f) return;
            // Render one completely black frame before issuing any Chaos destruction.
            if (BreachCamera.IsValid()) BreachCamera->SetManualCameraFade(1.f,FLinearColor::Black,false);
            bBreachBlackHoldStarted=true;BreachBlackRemaining=FMath::Max(0.f,BreachBlackHoldSeconds);
            BreachBlackFrame=GFrameCounter;
            UE_LOG(LogOBStage,Display,TEXT("Zone6 breach blackout | Hold=%.2f Sound=%s"),BreachBlackRemaining,*GetNameSafe(BreachExplosionSound.Get()));
            return;
        }
        if (!bBreachPhysicsStarted)
        {
            if (GFrameCounter<=BreachBlackFrame) return;
            if (IsValid(BreachCharge)) BreachCharge->Destroy();BreachCharge=nullptr;
            bBreachPhysicsStarted=true;
            if (!IsValid(ActiveBreachDoor) || !ActiveBreachDoor->TryBreach(GetBreachDirection()))
            {
                UE_LOG(LogOBStage,Error,TEXT("C4 breach rejected by the authored door; returning to placement"));
                if (BreachCamera.IsValid()) BreachCamera->StopCameraFade();
                bBreachRequested=bBreachPhysicsStarted=bBreachBlackHoldStarted=false;BeginC4Placement();return;
            }
            if (auto* Sound=BreachExplosionSound.LoadSynchronous()) BreachExplosionAudio=UGameplayStatics::SpawnSound2D(this,Sound);
        }
        BreachBlackRemaining=FMath::Max(0.f,BreachBlackRemaining-FMath::Max(0.f,DeltaTime));
        if (BreachBlackRemaining>0.f || !bBreachCompleted) return;
        if (!IsValid(ActiveBreachDoor) || !ActiveBreachDoor->ClearPassageForEntry()) return;
        if (BreachCamera.IsValid())
            BreachCamera->StartCameraFade(1.f,0.f,FMath::Max(.01f,BreachFadeInSeconds),FLinearColor::Black,false,false);
        ReleaseZone6Travel();
    }
}
void UOBStageFlowComponent::CancelZone6Gate()
{
    if (ActiveBreachDoor) ActiveBreachDoor->OnBreachCompleted.RemoveAll(this);
    ActiveBreachDoor=nullptr;Zone6GapRemaining=0;
    if (IsValid(BreachCharge)) BreachCharge->Destroy();BreachCharge=nullptr;
    C4Placement.Reset(FPlatformTime::Seconds());bBreachPhysicsStarted=false;BreachBlackFrame=MAX_uint64;
    BreachFadeOutRemaining=BreachBlackRemaining=0;bBreachBlackHoldStarted=false;
    if (IsValid(BreachExplosionAudio)) BreachExplosionAudio->Stop();
    BreachExplosionAudio=nullptr;
    if (BreachCamera.IsValid()) { BreachCamera->SetManualCameraFade(0.f,FLinearColor::Black,false);BreachCamera->StopCameraFade(); }
    BreachCamera.Reset();
    bBreachSawNeutral=bBreachRequested=false;
}
void UOBStageFlowComponent::HandleEncounterTriggered(int32 Point)
{
    if (!bActive || Phase==EOBStagePhase::QA || Point!=Zone6EncounterPoint) return;
    PrepareZone6Scene();
    for (TActorIterator<AOBEncounterSpawner> It(GetWorld());It;++It)
        if (It->GetPreparedPointIndex()==Point)
            if (auto* Hostage=Cast<AOBHostageCharacter>(It->GetSpawnedEnemy()))
            { Hostage->SetCanBeDamaged(true);Hostage->OnTakeAnyDamage.AddUniqueDynamic(this,&ThisClass::HandleHostageDamage); }
}
void UOBStageFlowComponent::HandleApproachToStop(int32 Point,float EstimatedSecondsRemaining)
{
    if (!bActive || Phase!=EOBStagePhase::Travel || !Player || Player->GetPlayerSplineComponent()->IsCameraTuningForQA()
        || !FMath::IsFinite(EstimatedSecondsRemaining) || EstimatedSecondsRemaining<0.f
        || EstimatedSecondsRemaining>FMath::Max(0.f,ArrivalVoiceLeadSeconds)) return;
    const int32 Points[]={7,19,44,64,72};
    int32 Zone=0;
    for (int32 I=0;I<UE_ARRAY_COUNT(Points);++I) if (Point==Points[I]) Zone=I+1;
    if (bUseZone6Breach && Point==Zone6BreachPoint) Zone=6;
    if (!Zone) return;
    DiscardOldNarration(Zone);
    // The arrival callback remains a fallback for checkpoint starts; the automatic ID prevents repeats.
    const int32 Id=2004+Zone*2;
    if (!AutomaticScriptIds.Contains(Id))
        UE_LOG(LogOBStage,Display,TEXT("Arrival approach | Zone=%d Point=%d Script=%d ETA=%.2f"),Zone,Point,Id,EstimatedSecondsRemaining);
    PlayAutomaticScript(Id,Zone);
}
void UOBStageFlowComponent::HandleActionPoint(int32 Point)
{
    if (Phase==EOBStagePhase::ShortcutTransition) return;
    if (!bActive || Phase==EOBStagePhase::QA || Player->GetPlayerSplineComponent()->IsCameraTuningForQA()) return;
    if (bUseZone6Breach && Point==Zone6DialoguePoint) { BeginZone6Dialogue();return; }
    if (bUseZone6Breach && Point==Zone6BreachPoint) { BeginZone6Breach();return; }
    if (bUseZone6Breach && Point==Zone6EncounterPoint && !bBreachCompleted)
    { UE_LOG(LogOBStage,Error,TEXT("Zone6 encounter reached without a completed breach; combat remains blocked"));BeginZone6Breach();return; }
    const int32 Points[]={7,19,44,64,72,Zone6EncounterPoint};int32 Zone=0;
    for (int32 I=0;I<6;++I) if (Point==Points[I]) Zone=I+1;
    if (!Zone) return;
    if (Zone!=6) RestoreSlowMotion();
    ClearDamageScreen();CurrentZone=Zone;Phase=EOBStagePhase::Combat;
    RemainingLives=FMath::Clamp(LivesPerZone,1,5);
    RemainingSeconds=FMath::Max(1.f,Zone==6 ? HostageTimeLimit : ZoneTimeLimit);
    StartZoneRecord(Zone,Point);
    if (Zone==6)
    {
        StartZone6EntryScene();
        Zone6SlowMotionSeconds=3.f;Phase=EOBStagePhase::Zone6Intro;
        HandleEncounterTriggered(Point);
    }
    DiscardOldNarration(Zone);
    if (Zone<6) PlayAutomaticScript(2004+Zone*2,Zone);
    RefreshHUD();
    UE_LOG(LogOBStage,Display,TEXT("Combat ready | Zone=%d Time=%.2f Lives=%d EnemyRate=%.2f WorldRate=%.2f"),Zone,RemainingSeconds,RemainingLives,Zone==6 ? EnemySlowMotionRate : 1.f,GetWorld()->GetWorldSettings()->GetEffectiveTimeDilation());
}
bool UOBStageFlowComponent::ApplyPlayerHit(int32 DamageHearts)
{
    if (!bActive || Phase!=EOBStagePhase::Combat || CurrentZone==6 || DamageHearts<=0 || RemainingLives<=0) return false;
    RemainingLives=FMath::Max(0,RemainingLives-FMath::Min(DamageHearts,RemainingLives));RefreshHUD();
    EnsureDamageScreen();
    if (DamageScreen) { DamageScreen->PlayHit();if (RemainingLives==0) DamageScreen->SetInjuredActive(true); }
    if (RemainingLives==0) StartSupport();
    return true;
}
void UOBStageFlowComponent::StartSupport()
{
    if (Phase!=EOBStagePhase::Combat || CurrentZone<1 || CurrentZone>5) return;
    if (auto* Z=ActiveZoneRecord()) Z->SupportReason=RemainingLives<=0 ? TEXT("hearts_depleted") : TEXT("time_expired");
    FinalizeZoneRecord();
    Phase=EOBStagePhase::SupportingFire;SupportElapsed=0;
    // Urgent squad warnings supersede routine travel chatter rather than talking over it.
    CancelNarration();
    if (!SupportScriptIds.IsEmpty()) { LastSupportScript=SupportScriptIds[FMath::RandRange(0,SupportScriptIds.Num()-1)];QueueScript(LastSupportScript,CurrentZone); }
    // Stop incoming shots/AI while the squad clears the remaining combatants. Hostages are untouched.
    for (AOBEnemyCharacter* Enemy : Player->GetActionComponent()->GetEncounterEnemies())
        if (IsValid(Enemy))
        {
            if (auto* AI=Cast<AOBAIController>(Enemy->GetController())) AI->StopStateTree();
            if (Enemy->GetCombatState()!=EOBEnemyCombatState::Dead) Enemy->SetCombatState(EOBEnemyCombatState::Idle);
        }
    UE_LOG(LogOBStage,Display,TEXT("Squad support | Zone=%d Script=%d RemainingEnemies=%d"),CurrentZone,LastSupportScript,Player->GetActionComponent()->GetEncounterEnemies().Num());
    if (Player->GetActionComponent()->GetEncounterEnemies().IsEmpty()) Player->GetActionComponent()->CompleteAction();
}
void UOBStageFlowComponent::FireSupportShot()
{
    AOBEnemyCharacter* Target=nullptr;
    for (AOBEnemyCharacter* Enemy : Player->GetActionComponent()->GetEncounterEnemies())
        if (IsValid(Enemy) && Enemy->IsEncounterCombatant() && Enemy->GetCombatState()!=EOBEnemyCombatState::Dead) { Target=Enemy;break; }
    if (!Target) return;
    AOBAllyCharacter* Ally=nullptr;
    for (TActorIterator<AOBAllyCharacter> It(GetWorld());It;++It) { Ally=*It;break; }
    if (Ally) Ally->FireSupportAt(Target);
    if (auto* Sound=SupportShotSound.LoadSynchronous()) UGameplayStatics::PlaySound2D(this,Sound);
    FDamageEvent Damage;
    Target->TakeDamage(Target->GetMaxHealth()+1.f,Damage,nullptr,Ally ? static_cast<AActor*>(Ally) : GetOwner());
}
bool UOBStageFlowComponent::HandleEncounterCleared()
{
    if (!bActive || Phase==EOBStagePhase::QA) return false;
    if (Phase==EOBStagePhase::Zone6Dialogue || Phase==EOBStagePhase::Zone6Breach || Phase==EOBStagePhase::Zone6Intro) return true;
    if (CurrentZone==6 && Phase==EOBStagePhase::Combat) { FinishStage(RemainingSeconds>0);return true; }
    if (CurrentZone>=1 && CurrentZone<=5 && Phase==EOBStagePhase::Combat) FinalizeZoneRecord();
    return Phase==EOBStagePhase::Ending || Phase==EOBStagePhase::Complete;
}
void UOBStageFlowComponent::HandleHostageDamage(AActor* Victim,float Damage,const UDamageType* Type,AController* Instigator,AActor* Causer)
{
    if (Phase==EOBStagePhase::Combat && CurrentZone==6 && IsValid(Victim) && Victim->IsA<AOBHostageCharacter>()
        && Damage>0 && Player && Instigator==Player->GetController())
    {
        if (auto* Z=ActiveZoneRecord()) Z->FailureReason=TEXT("hostage_hit");
        FinishStage(false);
    }
}
void UOBStageFlowComponent::FinishStage(bool bSuccess)
{
    if (Phase==EOBStagePhase::Ending || Phase==EOBStagePhase::Complete || Phase==EOBStagePhase::QA) return;
    ClearDamageScreen();
    if (!bSuccess) if (auto* Z=ActiveZoneRecord(); Z && Z->FailureReason.IsEmpty()) Z->FailureReason=TEXT("time_expired");
    FinalizeZoneRecord(bSuccess);
    StageRecord.bHasStageScore=true;
    StageRecord.StageScore=FOBStageScoring::Average(StageRecord.StageZones);
    StageRecord.StageRank=FOBStageScoring::Rank(StageRecord.StageScore);
    StageRecord.TotalSeconds=ResultStartSeconds>=0 ? FMath::Max(0.,GetWorld()->GetRealTimeSeconds()-ResultStartSeconds) : 0;
    StageRecord.CompletedAtUtc=FDateTime::UtcNow();
    StageRecord.bFinished=true; StageRecord.bScenarioCompleted=bSuccess;
    bSucceeded=bSuccess;Phase=EOBStagePhase::Ending;PresentationElapsed=0;
    Player->GetPlayerSplineComponent()->SetStageMovementBlocked(true);
    RestoreSlowMotion();ClearPresentation();
    for (TActorIterator<AOBEncounterSpawner> It(GetWorld());It;++It)
        if (auto* Enemy=It->GetSpawnedEnemy())
        {
            if (auto* AI=Cast<AOBAIController>(Enemy->GetController())) AI->StopStateTree();
            if (Enemy->GetCombatState()!=EOBEnemyCombatState::Dead) Enemy->SetCombatState(EOBEnemyCombatState::Idle);
        }
    BeginPresentationSubtitles();
    auto* PC=Cast<AOBPlayerController>(Player->GetController());
    if (!PC || !PC->IsLocalController()) { FinishEndingVideo();return; }
    Cinematic=CreateWidget<UOBStageCinematicWidget>(PC);Cinematic->AddToPlayerScreen(-100);
    Cinematic->OnVideoOpened.AddUObject(this,&ThisClass::HandleVideoOpened);
    Cinematic->OnVideoFinished.AddUObject(this,&ThisClass::HandleVideoFinished);
    Cinematic->OpenVideo((bSuccess ? ClearVideo : FailVideo).LoadSynchronous(),true,VoiceVolumeMultiplier);
    UE_LOG(LogOBStage,Display,TEXT("Ending started | Success=%d"),bSuccess);
}
void UOBStageFlowComponent::FinishEndingVideo()
{
    if (Phase!=EOBStagePhase::Ending || bEndingNarration) return;
    if (bEndingVideoOpened)
    {
        if (bWaitingForOutcomeSubtitle) return;
        if (auto* Overlay=GetOverlay(); Overlay && Overlay->IsSubtitleActive())
        {
            bWaitingForOutcomeSubtitle=true;
            Overlay->DismissSubtitle();
            return;
        }
        // A successfully opened movie already displayed this caption alongside its embedded voice.
        ShowCompletion();
        return;
    }
    // Missing media still gets the short text-only outcome before the result screen.
    ClearPresentation();bEndingNarration=true;
    if (auto* Overlay=GetOverlay()) { Overlay->SetVisibility(ESlateVisibility::HitTestInvisible);Overlay->SetStageMode(); }
    QueueScript(bSucceeded ? 2022 : 2023,CurrentZone);
    if (!ActiveScriptId) ShowCompletion();
}
void UOBStageFlowComponent::ShowCompletion()
{
    if (Phase!=EOBStagePhase::Ending) return;
    ClearPresentation();Phase=EOBStagePhase::Complete;
    const bool bSaved=SaveStageRecord();
    if (auto* Overlay=GetOverlay())
    {
        Overlay->SetVisibility(ESlateVisibility::HitTestInvisible);Overlay->ShowTrainingResult(StageRecord);
        if (auto* Widget=Overlay->GetTrainingResultWidget()) Widget->SetStageCompletion(bSucceeded,bSaved);
    }
    UE_LOG(LogOBStage,Display,TEXT("Stage training ended | Success=%d"),bSucceeded);
}
void UOBStageFlowComponent::BindResultTracking()
{
    auto* PC=Player ? Cast<AOBPlayerController>(Player->GetController()) : nullptr;
    if (ShotController.Get()==PC) return;
    if (ShotController.IsValid()) ShotController->OnShotResolved.RemoveAll(this);
    ShotController=PC;
    if (PC) PC->OnShotResolved.AddUObject(this,&ThisClass::RecordPlayerShot);
}
void UOBStageFlowComponent::StartResultTracking()
{
    if (ResultStartSeconds>=0 || !GetWorld()) return;
    ResultStartSeconds=GetWorld()->GetRealTimeSeconds();
    StageRecord.SessionId=FGuid::NewGuid(); StageRecord.StartedAtUtc=FDateTime::UtcNow();
    StageRecord.bPrototypeFlow=true;
    StageRecord.StageZones.SetNum(6);
    for (int32 I=0;I<6;++I) StageRecord.StageZones[I].Zone=I+1;
    if (const auto* GI=Cast<UOBGameInstance>(GetWorld()->GetGameInstance()))
    { StageRecord.LearnerName=GI->GetLearnerName(); StageRecord.LearnerId=GI->GetLearnerId(); }
}
void UOBStageFlowComponent::RecordPlayerShot(const FHitResult& Hit)
{
    if (!bActive || IsInputBlocked() || Phase==EOBStagePhase::QA || ResultStartSeconds<0) return;
    ++StageRecord.ShotsFired;
    // The shot event precedes damage, so a killing shot counts and shooting a corpse does not.
    const auto* Enemy=Cast<AOBEnemyCharacter>(Hit.GetActor());
    const bool bValid=Enemy && Enemy->CanBeDamaged() && Enemy->GetCurrentHealth()>0 && Enemy->GetCombatState()!=EOBEnemyCombatState::Dead;
    const bool bEnemyHit=bValid && Enemy->IsEncounterCombatant();
    if (bEnemyHit)
        ++StageRecord.ValidHits;
    if (auto* Z=ActiveZoneRecord(); Z && !Z->bFinalized)
    {
        ++Z->ShotsFired;
        if (bEnemyHit) ++Z->EnemyHits;
        else if (bValid && Enemy->IsA<AOBHostageCharacter>()) ++Z->HostageHits;
    }
}
FOBStageZoneResult* UOBStageFlowComponent::ActiveZoneRecord()
{
    return StageRecord.StageZones.IsValidIndex(CurrentZone-1) ? &StageRecord.StageZones[CurrentZone-1] : nullptr;
}
void UOBStageFlowComponent::StartZoneRecord(int32 Zone,int32 Point)
{
    if (ResultStartSeconds<0) StartResultTracking();
    auto* Z=ActiveZoneRecord();
    if (!Z || Z->bStarted) return;
    Z->Zone=Zone;Z->bStarted=true;ZoneElapsedSeconds=0;
    EnemyDamageAssessment.Reset();CreditedKills.Reset();
    for (TActorIterator<AOBEncounterTrigger> It(GetWorld());It;++It)
        if (It->GetPointIndex()==Point) Z->PlannedEnemies+=It->GetPlannedCombatantCount();
    // Isolated runtime fixtures can use the already registered roster.
    if (!Z->PlannedEnemies && Player) Z->PlannedEnemies=Player->GetActionComponent()->GetEncounterEnemies().Num();
}
void UOBStageFlowComponent::RecordEnemyDamage(AOBEnemyCharacter* Enemy,FName HitZone,float ActualDamage,bool bKilled,AController* Instigator)
{
    auto* Z=ActiveZoneRecord();
    if (!bActive || Phase!=EOBStagePhase::Combat || !Z || !Z->bStarted || Z->bFinalized || !Player || Instigator!=Player->GetController()
        || !IsValid(Enemy) || !Enemy->IsEncounterCombatant() || ActualDamage<=0 || !FMath::IsFinite(ActualDamage) || CreditedKills.Contains(Enemy)) return;
    if (HitZone==TEXT("HitZone.Head")) ++Z->HeadHits;
    else if (HitZone==TEXT("HitZone.Body")) ++Z->BodyHits;
    else if (HitZone==TEXT("HitZone.Arm") || HitZone==TEXT("HitZone.Leg")) ++Z->LimbHits;
    double& Assessment=EnemyDamageAssessment.FindOrAdd(Enemy);
    Assessment+=ActualDamage*FOBStageScoring::HitWeight(HitZone)/FMath::Max(1.f,Enemy->GetMaxHealth());
    if (bKilled)
    {
        CreditedKills.Add(Enemy);++Z->PlayerKills;
        Z->KillAssessmentSum+=FMath::Clamp(Assessment,0.,1.);
    }
}
void UOBStageFlowComponent::FinalizeZoneRecord(bool bRescued)
{
    auto* Z=ActiveZoneRecord();
    if (!Z || !Z->bStarted || Z->bFinalized) return;
    Z->ElapsedSeconds=ZoneElapsedSeconds;
    Z->RemainingSeconds=FMath::Max(0.f,RemainingSeconds);
    Z->RemainingHearts=CurrentZone==6 ? 0 : RemainingLives;
    Z->bRescued=bRescued;Z->bFinalized=true;
    FOBStageScoring::Evaluate(*Z);
    UE_LOG(LogOBStage,Display,TEXT("Zone score | Zone=%d Kills=%d/%d Kill=%.4f Time=%.2f Hearts=%.2f Total=%.4f Support=%s Failure=%s"),
        Z->Zone,Z->PlayerKills,Z->PlannedEnemies,Z->KillScore,Z->TimeScore,Z->HeartScore,Z->Score,*Z->SupportReason,*Z->FailureReason);
}
void UOBStageFlowComponent::AbortForNavigation()
{
    ClearDamageScreen();
    // An interrupted mission is never exported as a completed result.
    bActive=false;
    RestoreSlowMotion();ClearPresentation();RestoreOpeningMovement();
    Phase=EOBStagePhase::Inactive;
    if (ShotController.IsValid()) ShotController->OnShotResolved.RemoveAll(this);
}
bool UOBStageFlowComponent::SaveStageRecord() const
{
    if (!StageRecord.SessionId.IsValid()) return false;
    auto Json=MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("session_id"),StageRecord.SessionId.ToString(EGuidFormats::DigitsWithHyphens));
    Json->SetStringField(TEXT("stage"),TEXT("Stage_01"));
    Json->SetStringField(TEXT("status"),TEXT("completed"));
    Json->SetStringField(TEXT("started_at_utc"),StageRecord.StartedAtUtc.ToIso8601());
    Json->SetStringField(TEXT("completed_at_utc"),StageRecord.CompletedAtUtc.ToIso8601());
    Json->SetStringField(TEXT("learner_name"),StageRecord.LearnerName); Json->SetStringField(TEXT("learner_id"),StageRecord.LearnerId);
    Json->SetBoolField(TEXT("hostage_rescued"),bSucceeded);
    Json->SetNumberField(TEXT("shots_fired"),StageRecord.ShotsFired); Json->SetNumberField(TEXT("valid_hits"),StageRecord.ValidHits);
    if (StageRecord.ShotsFired>0) Json->SetNumberField(TEXT("accuracy_percent"),100.*StageRecord.ValidHits/StageRecord.ShotsFired);
    else Json->SetField(TEXT("accuracy_percent"),MakeShared<FJsonValueNull>());
    Json->SetNumberField(TEXT("elapsed_seconds"),StageRecord.TotalSeconds);
    Json->SetNumberField(TEXT("score"),StageRecord.StageScore); Json->SetStringField(TEXT("rank"),StageRecord.StageRank);
    Json->SetNumberField(TEXT("score_schema_version"),1);
    TArray<TSharedPtr<FJsonValue>> Zones;
    for (const auto& Z:StageRecord.StageZones)
    {
        auto J=MakeShared<FJsonObject>();
        J->SetNumberField(TEXT("zone"),Z.Zone);J->SetBoolField(TEXT("started"),Z.bStarted);J->SetBoolField(TEXT("finalized"),Z.bFinalized);
        J->SetNumberField(TEXT("planned_enemies"),Z.PlannedEnemies);J->SetNumberField(TEXT("player_kills"),Z.PlayerKills);
        J->SetNumberField(TEXT("kill_score"),Z.KillScore);J->SetNumberField(TEXT("time_score"),Z.TimeScore);J->SetNumberField(TEXT("heart_score"),Z.HeartScore);J->SetNumberField(TEXT("score"),Z.Score);
        J->SetNumberField(TEXT("elapsed_seconds"),Z.ElapsedSeconds);J->SetNumberField(TEXT("remaining_seconds"),Z.RemainingSeconds);
        if (Z.Zone==6) J->SetField(TEXT("remaining_hearts"),MakeShared<FJsonValueNull>());else J->SetNumberField(TEXT("remaining_hearts"),Z.RemainingHearts);
        J->SetNumberField(TEXT("shots_fired"),Z.ShotsFired);J->SetNumberField(TEXT("enemy_hits"),Z.EnemyHits);J->SetNumberField(TEXT("hostage_hits"),Z.HostageHits);
        J->SetNumberField(TEXT("misses"),Z.ShotsFired-Z.EnemyHits-Z.HostageHits);
        J->SetNumberField(TEXT("head_hits"),Z.HeadHits);J->SetNumberField(TEXT("body_hits"),Z.BodyHits);J->SetNumberField(TEXT("limb_hits"),Z.LimbHits);
        J->SetStringField(TEXT("time_grade"),Z.TimeGrade);J->SetStringField(TEXT("support_reason"),Z.SupportReason);J->SetStringField(TEXT("failure_reason"),Z.FailureReason);
        J->SetBoolField(TEXT("hostage_rescued"),Z.bRescued);Zones.Add(MakeShared<FJsonValueObject>(J));
    }
    Json->SetArrayField(TEXT("zones"),Zones);
    FString Text; if (!FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Text))) return false;
    const FString Folder=FPaths::ProjectSavedDir()/(StageRecord.bUsedTestInputs ? TEXT("Tests/StageResults/Records") : TEXT("TrainingRecords/Stage_01"));
    IFileManager::Get().MakeDirectory(*Folder,true);
    const bool bSaved=FFileHelper::SaveStringToFile(Text,*(Folder/(StageRecord.SessionId.ToString(EGuidFormats::Digits)+TEXT(".json"))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    UE_LOG(LogOBStage,Display,TEXT("Stage report | Saved=%d Shots=%d Hits=%d Seconds=%.1f"),bSaved,StageRecord.ShotsFired,StageRecord.ValidHits,StageRecord.TotalSeconds);
    return bSaved;
}
void UOBStageFlowComponent::PlayScript(int32 Id)
{
    if (!bActive || Phase==EOBStagePhase::Opening || Phase==EOBStagePhase::QA || Phase==EOBStagePhase::Complete || (Phase==EOBStagePhase::Ending && !bEndingNarration)) return;
    QueueScript(Id,CurrentZone);
}
void UOBStageFlowComponent::PlayAutomaticScript(int32 Id,int32 Zone)
{
    if (AutomaticScriptIds.Contains(Id)) return;
    AutomaticScriptIds.Add(Id);
    QueueScript(Id,Zone);
}
void UOBStageFlowComponent::QueueScript(int32 Id,int32 Zone)
{
    const auto* Row=FindScript(Id);
    if (!Row || (Row->Text.IsEmpty() && Row->Sound.IsNull()) || ActiveScriptId==Id || NarrationQueue.ContainsByPredicate([Id](const FQueuedScript& Entry) { return Entry.Id==Id; })) return;
    NarrationQueue.Add({Id,Zone});
    if (!ActiveScriptId) StartNextNarration();
}
void UOBStageFlowComponent::DiscardOldNarration(int32 Zone)
{
    // Finish the spoken sentence. Drop only unstarted lines belonging to a passed zone.
    NarrationQueue.RemoveAll([Zone](const FQueuedScript& Entry) { return Entry.Zone>0 && Entry.Zone<Zone; });
    if (ActiveScriptId && !bNarrationStarted && ActiveScriptZone>0 && ActiveScriptZone<Zone)
    {
        ActiveScriptId=0;NarrationDelay=0;
        StartNextNarration();
    }
}
void UOBStageFlowComponent::StartNextNarration()
{
    if (ActiveScriptId || NarrationQueue.IsEmpty()) return;
    const auto Entry=NarrationQueue[0];NarrationQueue.RemoveAt(0);
    const auto* Row=FindScript(Entry.Id);
    if (!Row) { StartNextNarration();return; }
    ActiveScriptId=Entry.Id;ActiveScriptZone=Entry.Zone;
    NarrationDelay=FMath::IsFinite(Row->Delay_Time) ? FMath::Max(0.f,Row->Delay_Time) : 0.f;
    NarrationElapsed=NarrationDuration=0;bNarrationStarted=false;
    if (NarrationDelay<=0) StartNarration();
}
void UOBStageFlowComponent::StartNarration()
{
    const auto* Row=FindScript(ActiveScriptId);
    if (!Row) { HandleNarrationFinished();return; }
    // Outcome movie audio owns this voice. A missing movie falls back to its caption only.
    auto* Sound=bEndingNarration ? nullptr : Row->Sound.LoadSynchronous();
    const float SoundDuration=Sound ? Sound->GetDuration() : 0.f;
    const float TextDuration=FMath::IsFinite(Row->Display_Time) ? FMath::Max(.1f,Row->Display_Time) : 1.2f;
    NarrationDuration=FMath::IsFinite(SoundDuration) && SoundDuration>0.f && SoundDuration<3600.f ? SoundDuration : TextDuration;
    bNarrationStarted=true;NarrationElapsed=0;
    if (auto* Overlay=GetOverlay()) Overlay->ShowPersistentSubtitle(Row->SpeakerName,Row->Text);
    UE_LOG(LogOBStage,Display,TEXT("Script | ID=%d Voice=%s Duration=%.2f"),ActiveScriptId,*GetNameSafe(Sound),NarrationDuration);
    if (Sound)
    {
        NarrationVoice=UGameplayStatics::CreateSound2D(this,Sound,FMath::Max(0.f,VoiceVolumeMultiplier));
        if (NarrationVoice)
        {
            NarrationVoice->OnAudioFinished.AddDynamic(this,&ThisClass::HandleNarrationFinished);
            NarrationVoice->Play();
        }
    }
}
void UOBStageFlowComponent::TickNarration(float DeltaTime)
{
    if (!ActiveScriptId) return;
    if (!bNarrationStarted)
    {
        NarrationDelay=FMath::Max(0.f,NarrationDelay-DeltaTime);
        if (NarrationDelay<=0.f) StartNarration();
        return;
    }
    NarrationElapsed+=DeltaTime;
    // Audio callbacks drive normal playback. Missing audio devices/callbacks cannot stall the queue.
    if (NarrationElapsed>=NarrationDuration+(NarrationVoice ? 2.f : 0.f)) HandleNarrationFinished();
}
void UOBStageFlowComponent::HandleNarrationFinished()
{
    if (!ActiveScriptId) return;
    const int32 FinishedId=ActiveScriptId;
    if (NarrationVoice) { NarrationVoice->OnAudioFinished.RemoveAll(this);NarrationVoice->Stop();NarrationVoice=nullptr; }
    if (auto* Overlay=GetOverlay()) Overlay->DismissSubtitle();
    ActiveScriptId=0;ActiveScriptZone=0;bNarrationStarted=false;NarrationDelay=NarrationElapsed=NarrationDuration=0;
    UE_LOG(LogOBStage,Display,TEXT("Script finished | ID=%d"),FinishedId);
    StartNextNarration();
    if (bDeparturePending && !ActiveScriptId && NarrationQueue.IsEmpty()) ReleaseDeparture();
    if (bEndingNarration && !ActiveScriptId && NarrationQueue.IsEmpty()) ShowCompletion();
}
void UOBStageFlowComponent::CancelNarration()
{
    NarrationQueue.Reset();ActiveScriptId=0;ActiveScriptZone=0;bNarrationStarted=false;
    NarrationDelay=NarrationElapsed=NarrationDuration=0;
    if (NarrationVoice) { NarrationVoice->OnAudioFinished.RemoveAll(this);NarrationVoice->Stop();NarrationVoice=nullptr; }
    if (auto* Overlay=GetOverlay()) Overlay->DismissSubtitle();
}
void UOBStageFlowComponent::RefreshHUD()
{
    if (auto* Overlay=GetOverlay())
    {
        Overlay->SetStageTime(Phase==EOBStagePhase::Combat ? RemainingSeconds : 0,false);
        Overlay->SetZoneProgress(CurrentZone,6);
        if (const auto* PC=Player ? Cast<AOBPlayerController>(Player->GetController()) : nullptr)
            Overlay->SetAmmo(PC->GetCurrentAmmo(),PC->GetMagazineCapacity());
        Overlay->SetHealth(RemainingLives,FMath::Clamp(LivesPerZone,1,5));
        Overlay->SetPanelEnabled(EOBOverlayElement::Life,CurrentZone!=6 && (Phase==EOBStagePhase::Combat || Phase==EOBStagePhase::SupportingFire));
        Overlay->SetPanelEnabled(EOBOverlayElement::Timer,Phase==EOBStagePhase::Combat || Phase==EOBStagePhase::SupportingFire);
    }
}
void UOBStageFlowComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* Tick)
{
    Super::TickComponent(DeltaTime,TickType,Tick);if (!bActive) return;
    if (!DamageScreen) EnsureDamageScreen();
    BindResultTracking();
    if (Phase==EOBStagePhase::ShortcutTransition) { TickShortcutTransition(DeltaTime);return; }
    if (bPendingZone6Demo)
    {
        if (!Player->GetPlayerSplineComponent()->GetRailPath() || !GetOverlay()) return;
        bPendingZone6Demo=false;StartZone6Demo();return;
    }
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    if (PendingQABreachPoint!=INDEX_NONE)
    {
        // All components and the HUD have completed BeginPlay before teleporting/broadcasting a stop.
        const int32 Point=PendingQABreachPoint;PendingQABreachPoint=INDEX_NONE;
        ClearPresentation();RestoreOpeningMovement();CurrentZone=6;Phase=EOBStagePhase::Travel;
        if (auto* Overlay=GetOverlay())
        { Overlay->CancelTrainingDialogue();Overlay->SetVisibility(ESlateVisibility::HitTestInvisible);Overlay->SetStageMode(); }
        FString Error;
        if (!Player->GetPlayerSplineComponent()->JumpToStagePointForQA(Point,Error))
        {
            Phase=EOBStagePhase::QA;HoldPlayerMovement();
            UE_LOG(LogOBStage,Error,TEXT("QA Breach rejected: %s | QA Reset to restart normally"),*Error);
        }
        else UE_LOG(LogOBStage,Display,TEXT("QA Breach ready | Point=%d | Normal stage progression enabled"),Point);
        return;
    }
#endif
    TickNarration(DeltaTime);
    if (Phase==EOBStagePhase::AwaitingDeparture && !ActiveScriptId && NarrationQueue.IsEmpty()) ReleaseDeparture();
    TickZone6Gate(DeltaTime);
    if (Phase==EOBStagePhase::Opening)
    {
        if (!Cinematic) { StartOpening();return; }
        OpeningElapsed+=DeltaTime;
        if (!bVoiceStarted && OpeningElapsed>15.f)
        { UE_LOG(LogOBStage,Warning,TEXT("Opening video open timed out; briefing will play on black"));bVideoFinished=true;HandleVideoOpened(); }
        if (bVoiceStarted && !bVoiceFinished)
        {
            TickOpeningVoiceClock(DeltaTime);
        }
    }
    else if (Phase==EOBStagePhase::Zone6Intro)
    {
        Zone6SlowMotionSeconds=FMath::Max(0.f,Zone6SlowMotionSeconds-DeltaTime);
        if (Zone6SlowMotionSeconds<=0)
        {
            RestoreSlowMotion();Phase=EOBStagePhase::Combat;RefreshHUD();
            UE_LOG(LogOBStage,Display,TEXT("Zone6 input enabled | IntroSeconds=3 RescueSeconds=%.2f"),RemainingSeconds);
        }
    }
    else if (Phase==EOBStagePhase::Combat)
    {
        ZoneElapsedSeconds+=FMath::Min(FMath::Max(0.f,DeltaTime),RemainingSeconds);
        RemainingSeconds=FMath::Max(0.f,RemainingSeconds-DeltaTime);RefreshHUD();
        if (RemainingSeconds<=0.f)
        {
            if (CurrentZone==6) FinishStage(false);
            else if (auto* Z=ActiveZoneRecord(); Player && Z && Z->PlannedEnemies>0 && Z->PlayerKills==Z->PlannedEnemies && !Player->GetActionComponent()->HasEncounterEnemies())
                Player->GetActionComponent()->CompleteAction();
            else StartSupport();
        }
    }
    else if (Phase==EOBStagePhase::SupportingFire)
    {
        SupportElapsed+=DeltaTime;
        if (SupportElapsed>=FMath::Max(.05f,SupportShotInterval)) { SupportElapsed=0;FireSupportShot(); }
    }
    else if (Phase==EOBStagePhase::Ending && Cinematic)
    {
        PresentationElapsed+=DeltaTime;
        UpdateEndingSubtitle();
        auto* Media=Cinematic->GetMediaPlayer();
        if (PresentationElapsed>15.f && (!Media || (!Media->IsPlaying() && !Media->IsPreparing())))
        { UE_LOG(LogOBStage,Warning,TEXT("Ending media unavailable; showing outcome caption fallback"));FinishEndingVideo(); }
    }
}
void UOBStageFlowComponent::RestoreSlowMotion()
{
    for (const auto& Item : OriginalDilations) if (Item.Key.IsValid()) Item.Key->CustomTimeDilation=Item.Value;
    OriginalDilations.Reset();
    bZone6EntrySceneStarted=false;
    for (TActorIterator<AOBBulletActor> It(GetWorld());It;++It)
        if (auto* Owner=Cast<AOBEnemyCharacter>(It->GetOwner())) It->CustomTimeDilation=Owner->CustomTimeDilation;
}
void UOBStageFlowComponent::ClearPresentation()
{
    bWaitingForOutcomeSubtitle=false;PresentationSubtitleId=OpeningSubtitleId=0;
    CancelNarration();bEndingNarration=false;
    CancelZone6Gate();bEndingVideoOpened=false;
    bDeparturePending=bDepartureViaAction=bReleasingDeparture=false;
    if (Voice) { Voice->OnAudioFinished.RemoveAll(this);Voice->OnAudioPlaybackPercent.RemoveAll(this);Voice->Stop();Voice=nullptr; }
    if (Cinematic) { Cinematic->OnVideoOpened.Clear();Cinematic->OnVideoFinished.Clear();Cinematic->CloseVideo();Cinematic->RemoveFromParent();Cinematic=nullptr; }
    if (auto* Overlay=GetOverlay())
    {
        Overlay->OnSubtitleFinished.RemoveDynamic(this,&ThisClass::HandlePresentationSubtitleFinished);
        Overlay->SetCinematicSubtitles(false);
    }
}
void UOBStageFlowComponent::RestoreOpeningMovement()
{
    if (!bOpeningMovementSaved || !Player) return;
    bOpeningMovementSaved=false;
    Player->GetCharacterMovement()->SetMovementMode(static_cast<EMovementMode>(OpeningMovementMode),OpeningCustomMovementMode);
}
void UOBStageFlowComponent::SuspendForQA()
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    if (!bActive) return;
    ClearDamageScreen();
    RestoreSlowMotion();ClearPresentation();RestoreOpeningMovement();Phase=EOBStagePhase::QA;
    Player->GetPlayerSplineComponent()->SetStageMovementBlocked(false);
    if (auto* Overlay=GetOverlay()) { Overlay->CancelTrainingDialogue();Overlay->SetVisibility(ESlateVisibility::HitTestInvisible); }
#endif
}
void UOBStageFlowComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if (DamageScreen) DamageScreen->RemoveFromParent();
    DamageScreen=nullptr;
    if (ShotController.IsValid()) ShotController->OnShotResolved.RemoveAll(this);
    RestoreSlowMotion();ClearPresentation();
    RestoreOpeningMovement();
    if (Player)
    {
        Player->GetPlayerSplineComponent()->OnActionPointReached.RemoveAll(this);
        Player->GetPlayerSplineComponent()->OnTravelStarted.RemoveAll(this);
    }
    if (MotionInput.IsValid()) MotionInput->OnBreachStateChanged.RemoveAll(this);
    MotionInput.Reset();
    Super::EndPlay(Reason);
}

#include "Core/OBPlayerController.h"
#include "Core/OBWeaponFeedback.h"
#include "Stage/OBStageFlowComponent.h"
#include "Core/OBQACheatManager.h"

#include "Core/OBGameInstance.h"
#include "DrawDebugHelpers.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Application/SlateApplication.h"
#include "InputAction.h"
#include "Character/OBEnemyCharacter.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "Character/OBActionComponent.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Widget/OBHUD.h"
#include "Widget/OBPlayerOverlayWidget.h"
#include "InputKeyEventArgs.h"
#include "Sound/SoundBase.h"
#include "Input/OBOpenFireInputComponent.h"
#include "Input/OBOpenFireSubsystem.h"
#include "Input/OBDeviceSettingsSubsystem.h"
#include "Engine/GameInstance.h"
#include "Camera/PlayerCameraManager.h"
#include "Character/OBHostageCharacter.h"
#include "Components/PostProcessComponent.h"
#include "Materials/MaterialInterface.h"

DEFINE_LOG_CATEGORY_STATIC(LogOBShooting, Log, All);

AOBPlayerController::AOBPlayerController()
{
    CreateDefaultSubobject<UOBOpenFireInputComponent>(TEXT("OpenFireInput"));
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    CheatClass=UOBQACheatManager::StaticClass();
#endif
    bShowMouseCursor = true;
    DefaultMouseCursor = EMouseCursor::Default;
    AimOutlineMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Assets/Materials/M_PP_AimOutline.M_PP_AimOutline")));
    FireSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Assets/FX/SFX/PlaySound/Shooting.Shooting")));
    EmptyFireSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Assets/FX/SFX/PlaySound/Shooting_EmptyMagazine.Shooting_EmptyMagazine")));
    ReloadSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Assets/FX/SFX/PlaySound/Reload.Reload")));
}
bool AOBPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
    if (HandleOperatorShortcut(Params.Key, Params.Event)) return true;
    if (Params.Key==EKeys::F5)
    {
        if (Params.Event==IE_Pressed) ReturnToMainMenu();
        return true;
    }
    if (auto* QA=Cast<UOBQACheatManager>(CheatManager); QA && QA->HandleCameraTuningKey(Params.Key,Params.Event==IE_Pressed || Params.Event==IE_Repeat)) return true;
    if (const auto* QA=Cast<UOBQACheatManager>(CheatManager); QA && QA->IsUIPreviewActive()
        && (Params.Key==EKeys::LeftMouseButton || Params.Key==EKeys::SpaceBar)) return true;
    if (Params.Key == EKeys::RightMouseButton)
    {
        // GP0 emits a right click. In IR mode its bridge event owns reloading,
        // so the simultaneous native HID event must not complete it twice.
        if (Params.Event == IE_Pressed && !UOBOpenFireSubsystem::IsRequested()) CompleteReload();
        return true;
    }
    // The same gun also generates a native mouse click. The bridge owns combat
    // input in this mode, preventing a HID click + UDP press from firing twice.
    if (UOBOpenFireSubsystem::IsRequested() && Params.Key == EKeys::LeftMouseButton) return true;
    const auto* HUD = GetOBHUD();
    auto* Overlay = HUD ? HUD->GetPlayerOverlayWidget() : nullptr;
    const bool bResult = Overlay && Overlay->IsTrainingResultVisible();
    const bool bInteraction = bResult || (Overlay && Overlay->IsTrainingPauseVisible());
    if (Params.Key == EKeys::LeftMouseButton || (bInteraction && Params.Key == EKeys::SpaceBar))
    {
        if (Params.Event == IE_Pressed || Params.Event == IE_Released)
        {
            bTriggerHeld = Params.Event == IE_Pressed;
            if (Overlay && Params.Key==EKeys::LeftMouseButton && Overlay->HandleInitialSkipTrigger(bTriggerHeld)) return true;
            if (bInteraction) Overlay->HandleTrainingTrigger(bTriggerHeld);
        }
    }
    // Keep Enhanced Input's key state current; the visible result blocks shooting separately.
    return Super::InputKey(Params);
}

void AOBPlayerController::ReturnToMainMenu() { NavigateToMap(TEXT("/Game/Maps/TitleMap")); }
void AOBPlayerController::RestartCurrentStage()
{
    NavigateToMap(FName(*UGameplayStatics::GetCurrentLevelName(this,true)));
}
void AOBPlayerController::RestartZone6Demo()
{
    NavigateToMap(TEXT("/Game/Maps/Stage_01"),TEXT("OBZone6Demo=1"));
}
bool AOBPlayerController::HandleOperatorShortcut(const FKey& Key, EInputEvent Event)
{
    if (Key != EKeys::F9 && Key != EKeys::F10 && Key != EKeys::F11) return false;
    if (Event != IE_Pressed) return true;
    if (Key == EKeys::F10) RestartZone6Demo();
    else if (auto* Instance = GetGameInstance<UOBGameInstance>())
    {
        if (Key == EKeys::F9)
        {
            Instance->ToggleCrosshair();
            if (FSlateApplication::IsInitialized()) FSlateApplication::Get().QueryCursor();
        }
        else if (UGameplayStatics::GetCurrentLevelName(this, true) == TEXT("TitleMap")) Instance->ToggleStageShortcut();
    }
    return true;
}
void AOBPlayerController::NavigateToMap(FName Map,const FString& Options)
{
    if (bNavigationPending || Map.IsNone() || !GetWorld()) return;
    bNavigationPending=true;
    SetOutlinedEnemy(nullptr);
    if (auto* GI=GetGameInstance())
        if (auto* Settings=GI->GetSubsystem<UOBDeviceSettingsSubsystem>(); Settings && Settings->IsOpen()) Settings->Close();
    if (auto* P=Cast<AOBPlayerCharacter>(GetPawn())) P->GetStageFlowComponent()->AbortForNavigation();
    SetPause(false);bTriggerHeld=false;FlushPressedKeys();
    ResetIgnoreMoveInput();ResetIgnoreLookInput();
    if (PlayerCameraManager) PlayerCameraManager->StopCameraFade();
    FInputModeGameOnly Mode;Mode.SetConsumeCaptureMouseDown(false);SetInputMode(Mode);
    UE_LOG(LogOBShooting,Display,TEXT("Navigation requested | From=%s To=%s"),*UGameplayStatics::GetCurrentLevelName(this,true),*Map.ToString());
    UGameplayStatics::OpenLevel(this,Map,true,Options);
}

void AOBPlayerController::BeginPlay()
{
    Super::BeginPlay();
    ShotTraceChannel = OBCombatCollision::PlayerShotTarget;
    // Apply after saved Blueprint defaults. Tutorial exercises explicitly set their own ammunition.
    SetTrainingAmmo(StandardMagazineCapacity, StandardMagazineCapacity);

    if (!IsLocalController()) return;
    AimOutlineMaterial.LoadSynchronous();
    LoadedWeaponSounds.Reset();
    for (USoundBase* Sound:{FireSound.LoadSynchronous(),EmptyFireSound.LoadSynchronous(),ReloadSound.LoadSynchronous()})
    {
        LoadedWeaponSounds.Emplace(Sound);
        OBWeaponFeedback::Prepare(Sound);
    }

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    // Apply after Blueprint defaults as well.
    CheatClass=UOBQACheatManager::StaticClass();
    if (CheatManager && !CheatManager->IsA<UOBQACheatManager>()) CheatManager=nullptr;
    EnableCheats();
#endif

    UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
        ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer());

    if (InputSubsystem && CommonMappingContext)
    {
        InputSubsystem->AddMappingContext(CommonMappingContext, 0);
    }

    FInputModeGameAndUI InputMode;
    InputMode.SetHideCursorDuringCapture(false);
    InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::LockAlways);
    const AOBHUD* HUD = GetOBHUD();
    if (!HUD || !HUD->IsMenuVisible()) SetInputMode(InputMode);

}

void AOBPlayerController::PlayerTick(float DeltaTime)
{
    Super::PlayerTick(DeltaTime);
    FVector Start,End;
    if (!IsLocalController() || !GetWorld() || GetWorld()->IsPaused() || GetGunInputMode()!=EOBGunInputMode::Shooting || !BuildCursorTraceRay(Start,End))
    {
        SetOutlinedEnemy(nullptr);return;
    }
    UpdateAimOutlineAlongRay(Start,End);
}

void AOBPlayerController::SetOutlinedEnemy(AOBEnemyCharacter* Enemy)
{
    if (!IsValid(Enemy) || (!Enemy->IsEncounterCombatant() && !Enemy->IsA<AOBHostageCharacter>())
        || !Enemy->CanBeDamaged() || Enemy->GetCurrentHealth()<=0 || Enemy->GetCombatState()==EOBEnemyCombatState::Dead) Enemy=nullptr;
    if (OutlinedEnemy.Get()==Enemy) return;
    if (auto* Previous=OutlinedEnemy.Get()) Previous->SetAimOutline(false);
    OutlinedEnemy=Enemy;
    if (Enemy && !AimOutlinePostProcess && GetPawn())
        if (auto* Material=AimOutlineMaterial.Get())
        {
            // Controllers are hidden actors: their scene components do not render.
            // The visible possessed character owns this local presentation component.
            AimOutlinePostProcess=NewObject<UPostProcessComponent>(GetPawn(),TEXT("AimOutlinePostProcess"));
            AimOutlinePostProcess->bUnbound=true;
            AimOutlinePostProcess->Priority=100.f;
            AimOutlinePostProcess->Settings.AddBlendable(Material,1.f);
            AimOutlinePostProcess->RegisterComponent();
        }
    if (AimOutlinePostProcess) AimOutlinePostProcess->BlendWeight=Enemy ? 1.f : 0.f;
    if (Enemy) Enemy->SetAimOutline(true);
}

void AOBPlayerController::UpdateAimOutlineAlongRay(const FVector& Start,const FVector& End)
{
    FHitResult Hit;
    TraceShotTarget(Start,End,Hit);
    SetOutlinedEnemy(Cast<AOBEnemyCharacter>(Hit.GetActor()));
}

bool AOBPlayerController::TraceShotTarget(const FVector& Start,const FVector& End,FHitResult& Hit) const
{
    FCollisionQueryParams Query(SCENE_QUERY_STAT(OBGunCursorTrace),bTraceComplex,GetPawn());
    FCollisionResponseParams Responses(ECR_Ignore);
    Responses.CollisionResponse.SetResponse(OBCombatCollision::PlayerShotTarget,ECR_Block);
    return GetWorld() && GetWorld()->LineTraceSingleByChannel(Hit,Start,End,OBCombatCollision::PlayerShotTarget,Query,Responses);
}

void AOBPlayerController::OnUnPossess()
{
    SetOutlinedEnemy(nullptr);
    if (AimOutlinePostProcess) { AimOutlinePostProcess->DestroyComponent();AimOutlinePostProcess=nullptr; }
    Super::OnUnPossess();
}

void AOBPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
    SetOutlinedEnemy(nullptr);
    if (AimOutlinePostProcess) { AimOutlinePostProcess->DestroyComponent();AimOutlinePostProcess=nullptr; }
    Super::EndPlay(Reason);
}

void AOBPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();

    UEnhancedInputComponent* EnhancedInputComponent = CastChecked<UEnhancedInputComponent>(InputComponent);
    if (FireAction)
    {
        EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Started, this, &ThisClass::FireAtCursor);
    }
    if (CalibrationCompletedAction)
        if (!UOBOpenFireSubsystem::IsRequested()) EnhancedInputComponent->BindAction(CalibrationCompletedAction, ETriggerEvent::Started, this, &ThisClass::NotifyAimCalibrationCompleted);
}

void AOBPlayerController::ShowResultWidget()
{
    if (!IsLocalController()) return;
    if (AOBHUD* HUD = GetOBHUD())
    {
        HUD->ShowResultWidget(ResultWidgetClass);
    }
    else
    {
        UE_LOG(LogOBShooting, Warning, TEXT("ShowResultWidget requires OBHUD as the GameMode HUD Class."));
    }
}

AOBHUD* AOBPlayerController::GetOBHUD() const
{
    return Cast<AOBHUD>(GetHUD());
}

bool AOBPlayerController::BuildCursorTraceRay(FVector& OutTraceStart, FVector& OutTraceEnd) const
{
    FVector WorldDirection = FVector::ZeroVector;

    if (UOBOpenFireSubsystem::IsRequested())
    {
        const auto* Receiver = GetGameInstance() ? GetGameInstance()->GetSubsystem<UOBOpenFireSubsystem>() : nullptr;
        FVector2D Aim;
        if (!Receiver || !Receiver->GetAimPosition(Aim)) return false;
        int32 Width = 0, Height = 0;
        GetViewportSize(Width, Height);
        if (Width <= 0 || Height <= 0 || !DeprojectScreenPositionToWorld(Aim.X*(Width-1), Aim.Y*(Height-1), OutTraceStart, WorldDirection)) return false;
    }
    else if (!DeprojectMousePositionToWorld(OutTraceStart, WorldDirection)) return false;

    OutTraceEnd = OutTraceStart + WorldDirection.GetSafeNormal() * TraceDistance;
    return true;
}

bool AOBPlayerController::IsActionInputAllowed() const
{
    if (const auto* Settings=GetGameInstance() ? GetGameInstance()->GetSubsystem<UOBDeviceSettingsSubsystem>() : nullptr; Settings && Settings->IsOpen()) return false;
    if (const auto* QA=Cast<UOBQACheatManager>(CheatManager); QA && QA->IsUIPreviewActive()) return false;
    if (const AOBHUD* HUD = GetOBHUD(); HUD && HUD->IsMenuVisible()) return false;
    const AOBPlayerCharacter* PlayerCharacter = Cast<AOBPlayerCharacter>(GetPawn());
    if (PlayerCharacter && PlayerCharacter->GetStageFlowComponent()->IsInputBlocked()) return false;
    if (PlayerCharacter && PlayerCharacter->GetPlayerSplineComponent()->IsCameraTuningForQA()) return false;
    return PlayerCharacter && PlayerCharacter->GetActionComponent()
        && PlayerCharacter->GetActionComponent()->GetActionState() == EOBActionState::Action;
}

bool AOBPlayerController::IsShootingPostureAllowed() const
{
    const auto* PlayerCharacter=Cast<AOBPlayerCharacter>(GetPawn());
    return PlayerCharacter && ((bTrainingControls && !bTrainingRequireShootingPosture) || PlayerCharacter->bIsCrouched==bIsCrouchMode);
}

EOBGunInputMode AOBPlayerController::GetGunInputMode() const
{
    if (const auto* Settings=GetGameInstance() ? GetGameInstance()->GetSubsystem<UOBDeviceSettingsSubsystem>() : nullptr; Settings && Settings->IsOpen()) return EOBGunInputMode::Disabled;
    if (bNavigationPending) return EOBGunInputMode::Disabled;
    if (const auto* QA=Cast<UOBQACheatManager>(CheatManager); QA && QA->IsUIPreviewActive()) return EOBGunInputMode::Disabled;
    const auto* HUD=GetOBHUD();
    const auto* Overlay=HUD ? HUD->GetPlayerOverlayWidget() : nullptr;
    if ((HUD && HUD->IsMenuVisible()) || (Overlay && Overlay->IsInitialTrainingSkipVisible()))
        return EOBGunInputMode::Interaction;
    const auto* PlayerCharacter=Cast<AOBPlayerCharacter>(GetPawn());
    if (!PlayerCharacter || PlayerCharacter->GetStageFlowComponent()->IsInputBlocked()) return EOBGunInputMode::Disabled;
    const auto* Spline=PlayerCharacter->GetPlayerSplineComponent();
    if (Spline->IsCameraTuningForQA() || Spline->IsRoutePreview()) return EOBGunInputMode::Disabled;
    if (Spline->IsMoving()) return EOBGunInputMode::Travel;
    if (PlayerCharacter->GetActionComponent()->GetActionState()!=EOBActionState::Action) return EOBGunInputMode::Disabled;
    // Guides and the weapon-button check advance via the existing training
    // signal. Reload/calibration exercises retain their existing permissions.
    if (bTrainingControls && !bTrainingCanShoot && !bTrainingCanReload) return EOBGunInputMode::Interaction;
    return EOBGunInputMode::Shooting;
}

bool AOBPlayerController::CanReloadInCurrentMode() const
{
    switch (GetGunInputMode())
    {
    case EOBGunInputMode::Travel:
    {
        const auto* PlayerCharacter=Cast<AOBPlayerCharacter>(GetPawn());
        return bAllowReloadWhileMoving || (PlayerCharacter && PlayerCharacter->GetStageFlowComponent()->IsStageActive());
    }
    case EOBGunInputMode::Shooting: return !bTrainingControls || bTrainingCanReload;
    default: return false;
    }
}

void AOBPlayerController::SetTrainingControls(bool bEnabled, bool bCanShoot, bool bCanReload, bool bRequireShootingPosture)
{
    bTrainingControls = bEnabled;
    bTrainingCanShoot = bCanShoot;
    bTrainingCanReload = bCanReload;
    bTrainingRequireShootingPosture = bRequireShootingPosture;
    if (!bTrainingControls) SetTrainingAmmo(CurrentAmmo, StandardMagazineCapacity);
}

void AOBPlayerController::ResetQAInputState(bool bResetPosture)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    bTriggerHeld=false;
    FlushPressedKeys();
    if (auto* HUD=GetOBHUD(); HUD && HUD->GetPlayerOverlayWidget()) HUD->GetPlayerOverlayWidget()->HandleInitialSkipTrigger(false);
    if (bResetPosture) SetCrouchMode(false);
#endif
}

void AOBPlayerController::SetTrainingAmmo(int32 Ammo, int32 Capacity, int32 ReloadAmount)
{
    const auto* PlayerCharacter = Cast<AOBPlayerCharacter>(GetPawn());
    const bool bStage = PlayerCharacter && PlayerCharacter->GetStageFlowComponent()->IsStageActive();
    const bool bTrainingMagazine = bTrainingControls && !bStage;
    MagazineCapacity = bTrainingMagazine ? FMath::Clamp(Capacity, 1, StandardMagazineCapacity) : StandardMagazineCapacity;
    ReloadAmmo = bTrainingMagazine && ReloadAmount!=INDEX_NONE ? FMath::Clamp(ReloadAmount, 1, MagazineCapacity) : MagazineCapacity;
    CurrentAmmo = FMath::Clamp(Ammo, 0, MagazineCapacity);
    if (AOBHUD* HUD = GetOBHUD())
        if (UOBPlayerOverlayWidget* Overlay = HUD->GetPlayerOverlayWidget())
            Overlay->SetAmmo(CurrentAmmo, MagazineCapacity);
}

void AOBPlayerController::CompleteReload()
{
    if (!CanReloadInCurrentMode()) return;
    const bool bRefilled = CurrentAmmo != ReloadAmmo;
    SetTrainingAmmo(ReloadAmmo, MagazineCapacity, ReloadAmmo);
    if (bRefilled)
        if (auto* Sound = ReloadSound.LoadSynchronous())
        {
            const float StartSeconds=OBWeaponFeedback::ReloadStartTime(Sound);
            UGameplayStatics::PlaySound2D(this, Sound,1.f,1.f,StartSeconds);
            UE_LOG(LogOBShooting, Display, TEXT("Weapon feedback=Reload sound=%s start_seconds=%.3f"), *Sound->GetName(),StartSeconds);
        }
    OnReloadCompleted.Broadcast();
}

void AOBPlayerController::NotifyAimCalibrationCompleted()
{
    if (bTrainingControls && IsActionInputAllowed()) OnAimCalibrationCompleted.Broadcast();
}

void AOBPlayerController::FireAtCursor()
{
    if (UOBOpenFireSubsystem::IsRequested() && !bApplyingOpenFireTrigger) return;
    if (const auto* QA=Cast<UOBQACheatManager>(CheatManager); QA && QA->IsUIPreviewActive()) return;
    if (const auto* HUD=GetOBHUD(); HUD && HUD->GetPlayerOverlayWidget() && HUD->GetPlayerOverlayWidget()->IsInitialTrainingSkipVisible()) return;
    if (const AOBHUD* HUD = GetOBHUD(); HUD && HUD->IsMenuVisible()) return;

    const EOBGunInputMode Mode=GetGunInputMode();
    if (Mode==EOBGunInputMode::Interaction)
    {
        if (bTrainingControls && IsActionInputAllowed()) OnTriggerPressed.Broadcast();
        return;
    }
    if (Mode!=EOBGunInputMode::Shooting) return;
    if (UOBOpenFireSubsystem::IsRequested())
    {
        const auto* Receiver=GetGameInstance() ? GetGameInstance()->GetSubsystem<UOBOpenFireSubsystem>() : nullptr;
        FVector2D Aim;
        if (!Receiver || !Receiver->GetAimPosition(Aim)) return;
    }

    const AOBPlayerCharacter* PlayerCharacter = Cast<AOBPlayerCharacter>(GetPawn());
    if (!IsValid(PlayerCharacter)) return;

    const UOBActionComponent* ActionComponent = PlayerCharacter->GetActionComponent();
    if (ActionComponent && ActionComponent->GetActionState() == EOBActionState::Prepare) return;
    OnTriggerPressed.Broadcast();
    if (!IsShootingPostureAllowed()) return;

    FVector TraceStart = FVector::ZeroVector;
    if (!CheckWeaponFireAllowed()) return;
    FVector TraceEnd = FVector::ZeroVector;

    if (!BuildCursorTraceRay(TraceStart, TraceEnd)) return;
    FireAlongRay(TraceStart, TraceEnd);
}

void AOBPlayerController::HandleTriggerState(bool bHeld)
{
    bTriggerHeld = bHeld;
    const auto* HUD = GetOBHUD();
    auto* Overlay = HUD ? HUD->GetPlayerOverlayWidget() : nullptr;
    if (!Overlay) return;
    if (Overlay->HandleInitialSkipTrigger(bHeld)) return;
    if (Overlay->IsTrainingResultVisible() || Overlay->IsTrainingPauseVisible()) Overlay->HandleTrainingTrigger(bHeld);
}

void AOBPlayerController::ApplyOpenFireInput(bool bHeld, bool bPressed, bool bReload, bool bCalibration)
{
    FOBOpenFireEvents Events;
    Events.bTriggerHeld=Events.bInteractionHeld=bHeld;
    Events.bTriggerPressed=Events.bInteractionPressed=bPressed;
    Events.bReloadPressed=bReload; Events.bCalibrationPressed=bCalibration;
    ApplyOpenFireInput(Events);
}

void AOBPlayerController::CancelGunTrigger()
{
    bTriggerHeld=false;
    if (auto* HUD=GetOBHUD(); HUD && HUD->GetPlayerOverlayWidget())
    {
        HUD->GetPlayerOverlayWidget()->CancelResultTrigger();
        HUD->GetPlayerOverlayWidget()->HandleInitialSkipTrigger(false);
    }
}

void AOBPlayerController::CancelOpenFireInput()
{
    // Connection/focus loss cancels a UI hold; it is not a released short tap.
    bGunTriggerNeedsRelease=true;
    CancelGunTrigger();
}

void AOBPlayerController::ApplyOpenFireInput(const FOBOpenFireEvents& Events)
{
    if (!UOBOpenFireSubsystem::IsRequested()) return;
    const EOBGunInputMode Mode=GetGunInputMode();
    const bool bModeChanged=bGunModeInitialized && Mode!=LastGunInputMode;
    if (!bGunModeInitialized || bModeChanged)
    {
        bGunTriggerNeedsRelease=Events.bInteractionHeld || bTriggerHeld;
        CancelGunTrigger();
        bGunModeInitialized=true; LastGunInputMode=Mode;
    }
    if (Mode==EOBGunInputMode::Disabled) { CancelGunTrigger(); return; }
    bool bPressed=false;
    if (bGunTriggerNeedsRelease)
    {
        CancelGunTrigger();
        if (!Events.bInteractionHeld)
        {
            bGunTriggerNeedsRelease=false;
            HandleTriggerState(false);
        }
    }
    else if (bModeChanged && !Events.bInteractionHeld) HandleTriggerState(false);
    else if (!bModeChanged)
    {
        if (Mode==EOBGunInputMode::Interaction)
        {
            // A counter-only quick tap still provides the existing UI's down/up.
            if (Events.bInteractionPressed && !Events.bInteractionHeld) HandleTriggerState(true);
            HandleTriggerState(Events.bInteractionHeld);
            bPressed=Events.bInteractionPressed;
        }
        else
        {
            bTriggerHeld=Events.bInteractionHeld;
            bPressed=Mode==EOBGunInputMode::Shooting && Events.bTriggerPressed;
        }
    }
    if (bPressed && Mode==GetGunInputMode())
    {
        TGuardValue<bool> Guard(bApplyingOpenFireTrigger, true);
        FireAtCursor();
    }
    if (Events.bReloadPressed && Mode==GetGunInputMode()) CompleteReload();
    if (Events.bCalibrationPressed) NotifyAimCalibrationCompleted();
}

void AOBPlayerController::FireAlongRay(const FVector& TraceStart, const FVector& TraceEnd)
{
    if (GetGunInputMode()!=EOBGunInputMode::Shooting) return;
    if (!IsActionInputAllowed()) return;
    const AOBPlayerCharacter* PlayerCharacter = Cast<AOBPlayerCharacter>(GetPawn());
    if (!PlayerCharacter || !IsShootingPostureAllowed()) return;
    if (!CheckWeaponFireAllowed()) return;
    PlayWeaponFireSound(false);
    SetTrainingAmmo(CurrentAmmo - 1, MagazineCapacity, ReloadAmmo);

    // Saved Blueprint channel overrides must not restore Visibility/world blocking.
    ShotTraceChannel = OBCombatCollision::PlayerShotTarget;
    FHitResult HitResult;
    const bool bHit = TraceShotTarget(TraceStart,TraceEnd,HitResult);

    if (bDrawDebugTrace)
    {
        const FVector DebugEnd = bHit ? HitResult.ImpactPoint : TraceEnd;

        DrawDebugLine(
            GetWorld(),
            TraceStart,
            DebugEnd,
            bHit ? FColor::Red : FColor::Green,
            false,
            DebugDrawTime,
            0,
            1.5f
        );

        if (bHit)
        {
            DrawDebugSphere(
                GetWorld(),
                HitResult.ImpactPoint,
                12.0f,
                12,
                FColor::Yellow,
                false,
                DebugDrawTime,
                0,
                1.5f
            );
        }
    }

    OnShotResolved.Broadcast(HitResult);
    if (!bHit) return;

    
    if (AOBEnemyCharacter* Enemy = Cast<AOBEnemyCharacter>(HitResult.GetActor()))
	{
	    UGameplayStatics::ApplyPointDamage(
	        Enemy,
            PlayerCharacter->GetStageFlowComponent()->IsStageActive() ? 100.f : ShotDamage,
	        (TraceEnd - TraceStart).GetSafeNormal(),
	        HitResult,
	        this,
	        GetPawn(),
	        UDamageType::StaticClass()
	    );
	}
}

bool AOBPlayerController::CheckWeaponFireAllowed()
{
    // Empty feedback also works while waiting for reload; guide/menu trigger presses stay silent.
    if (bTrainingControls && !bTrainingCanShoot && !(bTrainingCanReload && CurrentAmmo <= 0)) return false;
    if (CurrentAmmo <= 0)
    {
        PlayWeaponFireSound(true);
        return false;
    }
    return true;
}

void AOBPlayerController::PlayWeaponFireSound(bool bEmpty)
{
    if (auto* Sound = (bEmpty ? EmptyFireSound : FireSound).LoadSynchronous())
    {
        UGameplayStatics::PlaySound2D(this, Sound);
        UE_LOG(LogOBShooting, Display, TEXT("Weapon feedback=%s sound=%s"),
            bEmpty ? TEXT("Empty") : TEXT("Fire"), *Sound->GetName());
    }
}

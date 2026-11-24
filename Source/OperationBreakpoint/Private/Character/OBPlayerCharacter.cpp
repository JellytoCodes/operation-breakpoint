#include "Character/OBPlayerCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Core/OBCombatCollision.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MediaPipe/OBMediaPipeInputComponent.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Character/OBActionComponent.h"
#include "Stage/OBStageFlowComponent.h"
#include "World/OBRPGDamageType.h"
#include "World/OBBulletActor.h"
#include "Engine/DamageEvents.h"
#include "Character/OBModularSoldierComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "UnrealClient.h"

#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "TimerManager.h"

namespace OBCamera
{
    constexpr float StandingHeight = 64.0f;
    constexpr float CrouchCameraDrop = 36.0f;
}

namespace OBHitScreen
{
    constexpr float UpdateInterval = 0.01f;
}

DEFINE_LOG_CATEGORY_STATIC(LogOBCameraCollision, Log, All);

AOBPlayerCharacter::AOBPlayerCharacter()
{
    WeaponMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WeaponMesh"));
    // The base pawn has no mesh; the grip component attaches to hand_r after the BP assets are assigned.
    WeaponMesh->SetupAttachment(GetMesh());
    WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    WeaponMesh->SetGenerateOverlapEvents(false);
    WeaponMesh->SetCanEverAffectNavigation(false);

    PlayerCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("PlayerCamera"));
    PlayerCamera->SetupAttachment(GetCapsuleComponent());
    PlayerCamera->SetRelativeLocation(FVector(0.0f, 0.0f, OBCamera::StandingHeight));
    PlayerCamera->SetRelativeRotation(FRotator::ZeroRotator);
    PlayerCamera->bUsePawnControlRotation = false;

    StageFlowComponent = CreateDefaultSubobject<UOBStageFlowComponent>(TEXT("StageFlowComponent"));
    MediaPipeInputComponent = CreateDefaultSubobject<UOBMediaPipeInputComponent>(TEXT("MediaPipeInputComponent"));
    PlayerSplineComponent = CreateDefaultSubobject<UOBPlayerSplineComponent>(TEXT("PlayerSplineComponent"));
    ActionComponent = CreateDefaultSubobject<UOBActionComponent>(TEXT("ActionComponent"));

    GetCharacterMovement()->GetNavAgentPropertiesRef().bCanCrouch = true;

    EnemyBulletHitBox = CreateDefaultSubobject<UCapsuleComponent>(TEXT("EnemyBulletHitBox"));
    EnemyBulletHitBox->SetupAttachment(GetCapsuleComponent());
    ConfigureEnemyBulletHitBox();
}

void AOBPlayerCharacter::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
    Super::CalcCamera(DeltaTime, OutResult);

    if (!PlayerCamera || !PlayerCamera->IsActive())
    {
        CurrentEdgeLookYaw = 0.0f;
        bCameraCollisionInitialized = bCollisionCameraNearBody = bHasSafeCameraLocation = false;
        return;
    }

    const bool bCombatActive = ActionComponent && ActionComponent->GetActionState() == EOBActionState::Action
        && !(PlayerSplineComponent && (PlayerSplineComponent->IsTrainingCameraFocusActive() || PlayerSplineComponent->IsCameraTuningForQA()));
    const APlayerController* PC = Cast<APlayerController>(GetController());
    const ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
    FViewport* Viewport = LocalPlayer && LocalPlayer->ViewportClient ? LocalPlayer->ViewportClient->Viewport : nullptr;
    float CursorX = 0.0f;
    float CursorY = 0.0f;
    const FIntPoint Size = Viewport ? Viewport->GetSizeXY() : FIntPoint::ZeroValue;
    const bool bHasCursor = PC && PC->IsLocalController() && Viewport
        && Viewport->HasFocus() && Viewport->IsForegroundWindow()
        && Size.X > 0 && Size.Y > 0 && PC->GetMousePosition(CursorX, CursorY)
        && FMath::IsFinite(CursorX) && FMath::IsFinite(CursorY)
        && CursorX >= 0.0f && CursorX <= Size.X && CursorY >= 0.0f && CursorY <= Size.Y;

    ApplyCombatCamera(DeltaTime, bCombatActive, bHasCursor,
        bHasCursor ? CursorX / Size.X : 0.5f, OutResult);
    ApplyCameraCollision(DeltaTime, OutResult);
    GetSoldierAppearanceComponent()->UpdateFirstPersonVisibility();
}

void AOBPlayerCharacter::ApplyCameraCollision(float DeltaTime, FMinimalViewInfo& View)
{
    bCollisionCameraNearBody = false;
    if (!bEnableCameraCollision || !GetWorld() || View.ProjectionMode != ECameraProjectionMode::Perspective)
    {
        bCameraCollisionInitialized = bHasSafeCameraLocation = false;
        return;
    }

    const FVector Desired = View.Location;
    const FVector Anchor = GetPawnViewLocation();
    const FVector Arm = Desired - Anchor;
    const float Length = Arm.Size();
    const bool bTeleported = (bCameraCollisionInitialized || bHasSafeCameraLocation)
        && FVector::DistSquared(PreviousCameraAnchor, Anchor) > FMath::Square(FMath::Max(500.f, Length * 2.f));
    if (bTeleported) bCameraCollisionInitialized = bHasSafeCameraLocation = false;
    PreviousCameraAnchor = Anchor;

    const float Near = FMath::Max(0.f, View.GetFinalPerspectiveNearClipPlane());
    const float HalfWidth = Near * FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(View.FOV, 5.f, 170.f) * .5f));
    const float HalfHeight = HalfWidth / FMath::Max(.1f, View.AspectRatio);
    const float Radius = FMath::Max(FMath::Max(1.f, CameraCollisionProbeRadius),
        FMath::Sqrt(Near * Near + HalfWidth * HalfWidth + HalfHeight * HalfHeight));
    const float Padding = FMath::Max(0.f, CameraCollisionPadding);
    const FCollisionShape Shape = FCollisionShape::MakeSphere(Radius);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(OBCameraCollision), false, this);
    Query.bFindInitialOverlaps = true;
    // Ignore the whole projectile actor for both the boom sweep and every safety overlap.
    // This also covers RPG subclasses and saved Blueprint/custom-channel collision overrides.
    for (TActorIterator<AOBBulletActor> It(GetWorld()); It; ++It) Query.AddIgnoredActor(*It);
    FCollisionResponseParams Response(ECR_Block);
    // Squad members and enemy capsules should not repeatedly zoom the travel camera.
    Response.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);
    auto IsBlocked = [&](const FVector& Position) {
        return GetWorld()->OverlapBlockingTestByChannel(Position, FQuat::Identity, CameraCollisionChannel, Shape, Query, Response);
    };
    FHitResult Hit;
    const bool bHit = GetWorld()->SweepSingleByChannel(Hit, Anchor, Desired, FQuat::Identity,
        CameraCollisionChannel, Shape, Query, Response);

    if (bHit && Hit.bStartPenetrating)
    {
        // A rail can put the pawn/anchor in geometry. Never pull an otherwise safe camera into it.
        if (IsBlocked(Desired))
        {
            const FVector Escape = Anchor + Hit.Normal * (Hit.PenetrationDepth + Padding);
            if (!Hit.Normal.IsNearlyZero() && !IsBlocked(Escape)) View.Location = Escape;
            else if (bHasSafeCameraLocation && !IsBlocked(LastSafeCameraLocation)) View.Location = LastSafeCameraLocation;
        }
        bCameraCollisionInitialized = false;
    }
    else
    {
        const float Allowed = bHit ? FMath::Clamp(Hit.Distance - Padding, 0.f, Length) : Length;
        if (!bCameraCollisionInitialized || Allowed < CollisionCameraDistance) CollisionCameraDistance = Allowed;
        else
        {
            const float Alpha = 1.f - FMath::Exp(-FMath::Max(.1f, CameraCollisionReturnSpeed) * FMath::Max(0.f, DeltaTime));
            CollisionCameraDistance = FMath::Lerp(CollisionCameraDistance, Allowed, Alpha);
            if (FMath::IsNearlyEqual(CollisionCameraDistance, Allowed, .01f)) CollisionCameraDistance = Allowed;
        }
        bCameraCollisionInitialized = true;
        if (Length > SMALL_NUMBER) View.Location = Anchor + Arm * (CollisionCameraDistance / Length);
    }

    const bool bUnsafe = IsBlocked(View.Location);
    if (!bUnsafe)
    {
        LastSafeCameraLocation = View.Location;
        bHasSafeCameraLocation = true;
    }
    else if (!bCameraCollisionUnresolved)
        UE_LOG(LogOBCameraCollision, Warning, TEXT("No clear camera position; move the rail or camera anchor out of geometry | Player=%s Location=%s"),
            *GetName(), *View.Location.ToCompactString());
    bCameraCollisionUnresolved = bUnsafe;
    bCollisionCameraNearBody = !View.Location.Equals(Desired, .1f)
        && GetActorTransform().InverseTransformPosition(View.Location).X > -120.f;
    // The resolved view is cached by PlayerCameraManager for both rendering and aiming.
    // Keep the camera component's authored transform, posture, rotation and FOV intact.
}

FVector AOBPlayerCharacter::GetVelocity() const
{
    if (PlayerSplineComponent && (PlayerSplineComponent->IsMoving() || PlayerSplineComponent->IsRoutePreview()))
        return PlayerSplineComponent->GetRailVelocity();
    return Super::GetVelocity();
}

void AOBPlayerCharacter::ApplyCombatCamera(float DeltaTime, bool bCombatActive, bool bHasCursor,
    float NormalizedCursorX, FMinimalViewInfo& View)
{
    if (!bCombatActive || !bEnableEdgeLook)
    {
        CurrentEdgeLookYaw = 0.0f;
        return;
    }

    float TargetYaw = 0.0f;
    const float Region = FMath::Clamp(EdgeLookRegionFraction, 0.01f, 0.49f);
    const float MaxYaw = FMath::Clamp(EdgeLookMaxYaw, 0.0f, 15.0f);
    if (bHasCursor && FMath::IsFinite(NormalizedCursorX))
    {
        const float X = FMath::Clamp(NormalizedCursorX, 0.0f, 1.0f);
        const float EdgeAmount = X < Region ? (X - Region) / Region
            : X > 1.0f - Region ? (X - (1.0f - Region)) / Region : 0.0f;
        // Smoothstep eases into the edge zone, avoiding a sudden onset during precise aiming.
        const float Amount = FMath::Abs(EdgeAmount);
        TargetYaw = FMath::Sign(EdgeAmount) * Amount * Amount * (3.0f - 2.0f * Amount) * MaxYaw;
    }

    const float Alpha = 1.0f - FMath::Exp(-FMath::Max(EdgeLookInterpSpeed, 0.1f) * FMath::Max(DeltaTime, 0.0f));
    CurrentEdgeLookYaw = FMath::Clamp(FMath::Lerp(CurrentEdgeLookYaw, TargetYaw, Alpha), -MaxYaw, MaxYaw);
    if (FMath::IsNearlyEqual(CurrentEdgeLookYaw, TargetYaw, 0.001f)) CurrentEdgeLookYaw = TargetYaw;

    // PlayerCameraManager caches this same view for rendering and cursor deprojection.
    // Keep actor yaw, camera-component transform and crouch height untouched.
    View.Rotation.Yaw = FRotator::NormalizeAxis(View.Rotation.Yaw + CurrentEdgeLookYaw);
}

void AOBPlayerCharacter::ConfigureEnemyBulletHitBox()
{
    const UCapsuleComponent* Body = GetCapsuleComponent();
    EnemyBulletHitBox->SetRelativeTransform(FTransform::Identity);
    EnemyBulletHitBox->SetCapsuleSize(Body->GetUnscaledCapsuleRadius(), Body->GetUnscaledCapsuleHalfHeight());
    EnemyBulletHitBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    EnemyBulletHitBox->SetCollisionObjectType(OBCombatCollision::PlayerBulletTarget);
    EnemyBulletHitBox->SetCollisionResponseToAllChannels(ECR_Ignore);
    EnemyBulletHitBox->SetCollisionResponseToChannel(OBCombatCollision::EnemyBullet, ECR_Block);
    EnemyBulletHitBox->SetGenerateOverlapEvents(false);
    EnemyBulletHitBox->SetCanEverAffectNavigation(false);
}

void AOBPlayerCharacter::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    ConfigureEnemyBulletHitBox();
}

void AOBPlayerCharacter::BeginPlay()
{
    Super::BeginPlay();
    ConfigureEnemyBulletHitBox();

    CurrentHealth = MaxHealth;
    if (!ScreenDamageMaterial) return;

    ScreenDamageMID = UMaterialInstanceDynamic::Create(ScreenDamageMaterial, this);
    PlayerCamera->AddOrUpdateBlendable(ScreenDamageMID, 1.0f);
    ScreenDamageMID->SetScalarParameterValue(TEXT("DamageAmount"), 0.0f);
}

void AOBPlayerCharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
    Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
    ConfigureEnemyBulletHitBox();

    const float CapsuleOffset = GetCharacterMovement()->bCrouchMaintainsBaseLocation ? HalfHeightAdjust : 0.0f;
    const float CrouchedCameraHeight = OBCamera::StandingHeight + CapsuleOffset - OBCamera::CrouchCameraDrop;

    PlayerCamera->SetRelativeLocation(FVector(60.0f, 0.0f, CrouchedCameraHeight));
}

void AOBPlayerCharacter::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
    Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
    ConfigureEnemyBulletHitBox();

    PlayerCamera->SetRelativeLocation(FVector(60.0f, 0.0f, OBCamera::StandingHeight));
}

float AOBPlayerCharacter::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
    const bool bRPGHit = DamageEvent.DamageTypeClass && DamageEvent.DamageTypeClass->IsChildOf(UOBRPGDamageType::StaticClass());
    if ((bIsCrouched && !bRPGHit) || !CanBeDamaged()) return 0.0f;
    if (StageFlowComponent && StageFlowComponent->IsStageActive() && StageFlowComponent->GetPhase()!=EOBStagePhase::QA)
    {
        if (!FMath::IsFinite(DamageAmount) || DamageAmount<=0 || !StageFlowComponent->ApplyPlayerHit(bRPGHit ? 2 : 1)) return 0.f;
        // StageFlow owns the glass/injury UI. Do not flash the legacy camera
        // post-process over the same accepted hit.
        return DamageAmount;
    }

    const float AppliedDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
    if (AppliedDamage <= 0.0f || CurrentHealth <= 0.0f) return 0.0f;

    CurrentHealth = FMath::Clamp(CurrentHealth - AppliedDamage, 0.0f, MaxHealth);
    PlayHitScreenEffect();

    return AppliedDamage;
}

void AOBPlayerCharacter::PlayHitScreenEffect()
{
    if (!ScreenDamageMID) return;

    GetWorldTimerManager().ClearTimer(HitScreenEffectTimer);

    HitScreenEffectElapsed = 0.0f;
    ScreenDamageMID->SetScalarParameterValue(TEXT("DamageAmount"), 0.0f);

    GetWorldTimerManager().SetTimer(HitScreenEffectTimer, this, &ThisClass::UpdateHitScreenEffect, OBHitScreen::UpdateInterval, true);
}

void AOBPlayerCharacter::UpdateHitScreenEffect()
{
    if (!ScreenDamageMID)
    {
        GetWorldTimerManager().ClearTimer(HitScreenEffectTimer);
        return;
    }

    HitScreenEffectElapsed += OBHitScreen::UpdateInterval;

    const float SafeDuration = FMath::Max(HitScreenEffectDuration, OBHitScreen::UpdateInterval);
    const float NormalizedTime = FMath::Clamp(HitScreenEffectElapsed / SafeDuration, 0.0f, 1.0f);
    const float EffectAmount = FMath::Sin(NormalizedTime * PI);

    ScreenDamageMID->SetScalarParameterValue(TEXT("DamageAmount"), EffectAmount);

    if (NormalizedTime < 1.0f) return;

    ScreenDamageMID->SetScalarParameterValue(TEXT("DamageAmount"), 0.0f);
    GetWorldTimerManager().ClearTimer(HitScreenEffectTimer);
}

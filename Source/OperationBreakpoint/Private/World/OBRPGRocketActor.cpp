#include "World/OBRPGRocketActor.h"
#include "World/OBRPGDamageType.h"
#include "Components/SphereComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraFunctionLibrary.h"
#include "Character/OBPlayerCharacter.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"

AOBRPGRocketActor::AOBRPGRocketActor()
{
    Damage = 2.f;
    DamageTypeClass = UOBRPGDamageType::StaticClass();
    PlayerImpactSound=TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Assets/FX/SFX/Stage_01/Engagement/RPG-7shot_end.RPG-7shot_end")));
    ImpactAttenuation=CreateDefaultSubobject<USoundAttenuation>(TEXT("ImpactAttenuation"));
    ImpactAttenuation->Attenuation.bAttenuate=true;
    ImpactAttenuation->Attenuation.bSpatialize=true;
    ImpactAttenuation->Attenuation.DistanceAlgorithm=EAttenuationDistanceModel::Logarithmic;
    ImpactAttenuation->Attenuation.AttenuationShapeExtents=FVector(150.f,0,0);
    ImpactAttenuation->Attenuation.FalloffDistance=3000.f;
    ProjectileMovement->InitialSpeed = 900.f;
    ProjectileMovement->MaxSpeed = 900.f;
    InitialLifeSpan = 15.f;
    CollisionComponent->InitSphereRadius(6.f);
    RocketTrail = CreateDefaultSubobject<UNiagaraComponent>(TEXT("RocketTrail"));
    RocketTrail->SetupAttachment(CollisionComponent);
    RocketTrail->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    RocketTrail->SetAutoActivate(false);
    // Native CDO construction precedes the editor's typed-element registry. Niagara's component
    // renderers may construct CameraShakeSourceComponent templates, so loading effects here is too early.
    FallbackTrailEffect = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(
        TEXT("/Game/Assets/FX/VFX/NiagaraExamples/FX_Weapons/Trails/NS_RocketTrail.NS_RocketTrail")));
    FallbackImpactEffect = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(
        TEXT("/Game/Assets/FX/VFX/NiagaraExamples/FX_Weapons/Impacts/NS_Impact_Metal.NS_Impact_Metal")));

    // Editable placeholder with an unambiguous flight axis; do not reuse the unverified bomb model.
    static ConstructorHelpers::FObjectFinder<UStaticMesh> ProjectileMesh(TEXT("/Engine/BasicShapes/Cone.Cone"));
    BulletMesh->SetStaticMesh(ProjectileMesh.Object);
    BulletMesh->SetRelativeRotation(FRotator(90, 0, 0));
    BulletMesh->SetRelativeScale3D(FVector(.1f, .1f, .35f));
}

void AOBRPGRocketActor::LockOnPlayer(AOBPlayerCharacter* Player)
{
    if (!IsValid(Player)) return;
    // Follow the collision capsule through crouch changes; cover never cancels a successful RPG roll.
    ProjectileMovement->HomingTargetComponent=Player->GetCapsuleComponent();
    ProjectileMovement->HomingAccelerationMagnitude=12000.f;
    ProjectileMovement->bIsHomingProjectile=true;
    ProjectileMovement->bForceSubStepping=true;
}

void AOBRPGRocketActor::BeginPlay()
{
    Super::BeginPlay();
    if (GetNetMode() == NM_DedicatedServer) return;
    if (!RocketTrail->GetAsset()) RocketTrail->SetAsset(FallbackTrailEffect.LoadSynchronous());
    if (RocketTrail->GetAsset()) RocketTrail->Activate(true);
}

void AOBRPGRocketActor::OnBulletImpact(const FHitResult& Hit)
{
    if (bImpactShown) return;
    bImpactShown = true;
    if (GetNetMode() == NM_DedicatedServer) return;
    if (Cast<AOBPlayerCharacter>(Hit.GetActor()))
        if (auto* Sound=PlayerImpactSound.LoadSynchronous())
            UGameplayStatics::SpawnSoundAtLocation(this,Sound,Hit.ImpactPoint,FRotator::ZeroRotator,.2f,1.f,0.f,ImpactAttenuation);
    if (!ImpactEffect) ImpactEffect = FallbackImpactEffect.LoadSynchronous();
    if (ImpactEffect)
        UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, ImpactEffect, Hit.ImpactPoint, Hit.ImpactNormal.Rotation());
    // Cosmetic only: the base projectile applies point damage once; no additional radial damage.
}

#include "World/OBBulletActor.h"

#include "Character/OBPlayerCharacter.h"
#include "Core/OBCombatCollision.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"

AOBBulletActor::AOBBulletActor()
{
    PrimaryActorTick.bCanEverTick = false;

    CollisionComponent = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionComponent"));

    SetRootComponent(CollisionComponent);

    CollisionComponent->InitSphereRadius(4.0f);
    ConfigureCollision();

    CollisionComponent->OnComponentHit.AddDynamic(this, &ThisClass::HandleBulletHit);

    BulletMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BulletMesh"));

    BulletMesh->SetupAttachment(CollisionComponent);
    BulletMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));

    ProjectileMovement->UpdatedComponent = CollisionComponent;

    ProjectileMovement->InitialSpeed = 1000.f;
    ProjectileMovement->MaxSpeed = 1000.f;
    ProjectileMovement->ProjectileGravityScale = 0.f;
    ProjectileMovement->bRotationFollowsVelocity = true;
    ProjectileMovement->bShouldBounce = false;

    InitialLifeSpan = 3.0f;
}

void AOBBulletActor::BeginPlay()
{
    Super::BeginPlay();

    // Reapply after saved Blueprint defaults, including the existing BP_Bullet_7mm.
    ConfigureCollision();
    BulletMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    if (AActor* OwnerActor = GetOwner())
    {
        CollisionComponent->IgnoreActorWhenMoving(OwnerActor,true);
    }
}

void AOBBulletActor::ConfigureCollision()
{
    CollisionComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    CollisionComponent->SetCollisionObjectType(OBCombatCollision::EnemyBullet);
    CollisionComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
    CollisionComponent->SetCollisionResponseToChannel(OBCombatCollision::PlayerBulletTarget, bPlayerHitAllowed ? ECR_Block : ECR_Ignore);
    CollisionComponent->SetGenerateOverlapEvents(false);
    CollisionComponent->SetNotifyRigidBodyCollision(true);
}

void AOBBulletActor::SetPlayerHitAllowed(bool bAllowed)
{
    bPlayerHitAllowed=bAllowed;
    ConfigureCollision();
}

void AOBBulletActor::HandleBulletHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit)
{
    if (!bPlayerHitAllowed) return;
    if (!IsValid(OtherActor) || !OtherActor->IsA<AOBPlayerCharacter>() || OtherActor == GetOwner()) return;
    if (bImpactHandled) return;
    bImpactHandled = true;
    if (IsValid(OtherActor) && OtherActor != this && OtherActor != GetOwner())
    {
        const FVector ShotDirection = ProjectileMovement->Velocity.GetSafeNormal();

        UGameplayStatics::ApplyPointDamage(
            OtherActor,
            Damage,
            ShotDirection,
            Hit,
            GetInstigatorController(),
            this,
            DamageTypeClass
        );
    }

    OnBulletImpact(Hit);
    Destroy();
}

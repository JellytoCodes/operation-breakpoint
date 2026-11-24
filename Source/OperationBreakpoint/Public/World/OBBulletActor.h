#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OBBulletActor.generated.h"

class UDamageType;
class UPrimitiveComponent;
class UProjectileMovementComponent;
class USphereComponent;
class UStaticMeshComponent;

UCLASS()
class OPERATIONBREAKPOINT_API AOBBulletActor : public AActor
{
    GENERATED_BODY()

public:
    AOBBulletActor();
    void SetPlayerHitAllowed(bool bAllowed);

protected:
    virtual void OnBulletImpact(const FHitResult& Hit) {}

    virtual void BeginPlay() override;

private:
    friend class FOBRPGCombatTest;
    void ConfigureCollision();
    bool bImpactHandled = false;
    bool bPlayerHitAllowed = true;
    UFUNCTION()
    void HandleBulletHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit);

protected:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OB|Bullet", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<USphereComponent> CollisionComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OB|Bullet", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UStaticMeshComponent> BulletMesh;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OB|Bullet", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

    UPROPERTY(EditDefaultsOnly, Category = "OB|Bullet")
    float Damage = 1.0f;

    UPROPERTY(EditDefaultsOnly, Category = "OB|Bullet")
    TSubclassOf<UDamageType> DamageTypeClass;
};

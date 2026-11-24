#pragma once

#include "CoreMinimal.h"
#include "World/OBBulletActor.h"
#include "OBRPGRocketActor.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;
class USoundBase;
class USoundAttenuation;
class AOBPlayerCharacter;

UCLASS(Blueprintable)
class OPERATIONBREAKPOINT_API AOBRPGRocketActor : public AOBBulletActor
{
    GENERATED_BODY()
public:
    AOBRPGRocketActor();
    void LockOnPlayer(AOBPlayerCharacter* Player);
protected:
    virtual void BeginPlay() override;
    virtual void OnBulletImpact(const FHitResult& Hit) override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="OB|Rocket")
    TObjectPtr<UNiagaraComponent> RocketTrail;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="OB|Rocket")
    TObjectPtr<UNiagaraSystem> ImpactEffect;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="OB|Rocket")
    TSoftObjectPtr<UNiagaraSystem> FallbackTrailEffect;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="OB|Rocket")
    TSoftObjectPtr<UNiagaraSystem> FallbackImpactEffect;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="OB|Rocket|Audio")
    TSoftObjectPtr<USoundBase> PlayerImpactSound;
    UPROPERTY(VisibleAnywhere, Category="OB|Rocket|Audio")
    TObjectPtr<USoundAttenuation> ImpactAttenuation;
private:
    friend class FOBEnemyEngagementAudioTest;
    bool bImpactShown = false;
};

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AI/OBEnemyCombatPattern.h"
#include "OBEnemyPatternPreview.generated.h"

class AOBEnemyCharacter;
class UCameraComponent;
class UStateTree;

USTRUCT(BlueprintType)
struct FOBEnemyPatternPreviewEntry
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere) EOBEnemyCombatPattern Pattern=EOBEnemyCombatPattern::Stand;
    UPROPERTY(EditAnywhere) TObjectPtr<AOBEnemyCharacter> Enemy;
    UPROPERTY(EditAnywhere) TObjectPtr<UStateTree> StateTree;
};

/** A level-local animation preview. No stage or production Blueprint defaults are changed. */
UCLASS()
class OPERATIONBREAKPOINT_API AOBEnemyPatternPreview : public AActor
{
    GENERATED_BODY()
public:
    AOBEnemyPatternPreview();
    UPROPERTY(EditAnywhere,Category="Preview") bool bAutoStart=true;
    UPROPERTY(EditInstanceOnly,Category="Preview") TArray<FOBEnemyPatternPreviewEntry> Entries;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Preview") TObjectPtr<UCameraComponent> PreviewCamera;
    UPROPERTY(EditAnywhere,Category="Preview") FVector PlayerTargetOffset=FVector(1300,0,100);
    bool HasStartedPreview() const { return bStarted; }
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    void StartPreview();
    FTimerHandle StartTimer;
    bool bStarted=false;
};

#include "Animation/OBAnimNotify_EnemyDeath.h"

#include "Character/OBEnemyCharacter.h"
#include "Components/SkeletalMeshComponent.h"

void UOBAnimNotify_EnemyDeath::Notify(USkeletalMeshComponent* MeshComp,
    UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
    Super::Notify(MeshComp, Animation, EventReference);

    if (!IsValid(MeshComp)) return;

    AOBEnemyCharacter* Enemy = Cast<AOBEnemyCharacter>(MeshComp->GetOwner());
    if (!IsValid(Enemy)) return;

    switch (NotifyType)
    {
    case EOBEnemyDeathNotifyType::DropWeapon:
        Enemy->DropWeapon();
        break;

    case EOBEnemyDeathNotifyType::BeginDeathPhysics:
        Enemy->BeginDeathPhysics();
        break;
    }
}
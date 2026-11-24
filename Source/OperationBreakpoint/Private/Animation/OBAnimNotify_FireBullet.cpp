#include "Animation/OBAnimNotify_FireBullet.h"

#include "Character/OBEnemyCharacter.h"
#include "Components/SkeletalMeshComponent.h"

void UOBAnimNotify_FireBullet::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
    Super::Notify(MeshComp, Animation, EventReference);

    if (!IsValid(MeshComp)) return;

    AOBEnemyCharacter* Enemy = Cast<AOBEnemyCharacter>(MeshComp->GetOwner());

    if (!IsValid(Enemy)) return;

    Enemy->FireBullet();
}
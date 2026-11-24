#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Character/OBCharacter.h"
#include "Character/OBEnemyCharacter.h"
#include "Character/OBAllyCharacter.h"
#include "Character/OBWeaponGripComponent.h"
#include "Animation/OBWeaponGripAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBWeaponGripTest,"OperationBreakpoint.Soldier.WeaponGrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOBWeaponGripTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    for (const auto* Path : {TEXT("/Game/Blueprints/Player/BP_PlayerCharacter.BP_PlayerCharacter_C"),
                            TEXT("/Game/Blueprints/Ally/BP_AllyCharacter.BP_AllyCharacter_C"),
                            TEXT("/Game/Blueprints/Enemy/BP_EnemyCharacter.BP_EnemyCharacter_C"),
                            TEXT("/Game/Blueprints/Enemy/BP_RPGTrooper.BP_RPGTrooper_C")})
    {
        auto* Class=LoadClass<AOBCharacter>(nullptr,Path);
        if (!TestNotNull(TEXT("Original Blueprint loads"),Class)) return false;
        auto* Character=World->SpawnActorDeferred<AOBCharacter>(Class,FTransform(FRotator(0,53,0),FVector(100,200,300)),
            nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Character->AutoPossessAI=EAutoPossessAI::Disabled;
        Character->FinishSpawning(Character->GetActorTransform());
        auto* Grip=Character->GetWeaponGripComponent();
        auto* Weapon=Grip->GetWeapon();
        auto* Body=Character->GetMesh();
        if (!TestNotNull(TEXT("Native rifle component exists"),Weapon)
            || !TestNotNull(TEXT("Native grip component exists"),Grip)) return false;
        TestNotNull(TEXT("Saved rifle mesh is assigned"),Weapon->GetSkeletalMeshAsset());
        if (Character->IsA<AOBAllyCharacter>())
            TestEqual(TEXT("Allies use AR4 instead of KA47"),GetNameSafe(Weapon->GetSkeletalMeshAsset()),FString(TEXT("SK_AR4")));
        TestTrue(TEXT("Rifle is attached to the animation right hand"),Weapon->GetAttachParent()==Body
            && Weapon->GetAttachSocketName()==TEXT("hand_r"));
        TestTrue(TEXT("Grip enabled on original Blueprint"),Grip->ShouldApplyGrip());
        TestTrue(TEXT("AnimBP retains native grip evaluation"),Body->GetAnimInstance()
            && Body->GetAnimInstance()->IsA<UOBWeaponGripAnimInstance>());
        auto Evaluate=[&]() {
            for (int32 I=0; I<30; ++I) { Body->TickAnimation(1.f/60.f,false); Body->RefreshBoneTransforms(); }
        };
        Evaluate();
        const FTransform Target=Grip->GetSupportHand()*Weapon->GetComponentTransform();
        const FTransform Hand=Body->GetSocketTransform(TEXT("hand_l"));
        const float Error=FVector::Distance(Hand.GetLocation(),Target.GetLocation());
        AddInfo(FString::Printf(TEXT("%s left wrist error %.3f cm"),*Character->GetClass()->GetName(),Error));
        TestTrue(TEXT("Support wrist reaches rifle grip within 0.5 cm"),Error<.5f);
        TestTrue(TEXT("Support palm follows rifle rotation"),Hand.GetRotation().AngularDistance(Target.GetRotation())<.02f);
        TestEqual(TEXT("The active weapon has its own finger settings"),Grip->GetFingerOffsets().Num(),Grip->IsUsingRPGGrip() ? 38 : 30);
        if (Grip->IsUsingRPGGrip())
        {
            TestTrue(TEXT("RPG grip selects the visible launcher, not the hidden rifle"),Weapon!=Character->GetWeaponMesh());
            TestTrue(TEXT("RPG right wrist uses the saved launcher attachment"),Weapon->GetRelativeTransform().Equals(Grip->RPGWeaponInRightHand,.001f));
        }
        Grip->SetGripReleased(true); Evaluate();
        TestFalse(TEXT("Explicit release disables IK and finger grip"),Grip->ShouldApplyGrip());
        TestTrue(TEXT("Released hand returns to its authored animation"),
            FVector::Distance(Body->GetSocketLocation(TEXT("hand_l")),Target.GetLocation())>1.f);
        Grip->SetGripReleased(false);
        Weapon->SetHiddenInGame(true);
        TestFalse(TEXT("Hidden gun cannot constrain the hands"),Grip->ShouldApplyGrip());
        Weapon->SetHiddenInGame(false);
        if (auto* Enemy=Cast<AOBEnemyCharacter>(Character))
        {
            Enemy->SetCombatState(EOBEnemyCombatState::Reloading);
            TestFalse(TEXT("Enemy reload releases grip"),Grip->ShouldApplyGrip());
            Enemy->SetCombatState(EOBEnemyCombatState::Idle);
            TestTrue(TEXT("Enemy returns to grip after reload"),Grip->ShouldApplyGrip());
            Weapon->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
            TestFalse(TEXT("Dropped rifle releases grip"),Grip->ShouldApplyGrip());
        }
        Character->Destroy();
    }
    return !HasAnyErrors();
}
#endif

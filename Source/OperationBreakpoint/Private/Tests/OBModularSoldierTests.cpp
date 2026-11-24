#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Character/OBCharacter.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBEnemyCharacter.h"
#include "Character/OBModularSoldierComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBSoldierOriginalBlueprintTest,"OperationBreakpoint.Soldier.OriginalBlueprintAssembly",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOBSoldierOriginalBlueprintTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    for (const auto* Path : {TEXT("/Game/Blueprints/Player/BP_PlayerCharacter.BP_PlayerCharacter_C"),
                            TEXT("/Game/Blueprints/Enemy/BP_EnemyCharacter.BP_EnemyCharacter_C")})
    {
        UClass* Class=LoadClass<AOBCharacter>(nullptr,Path);
        if (!TestNotNull(TEXT("Original character Blueprint loads"),Class)) return false;
        const auto AuthoredMeshes=Class->GetDefaultObject<AOBCharacter>()->GetSoldierAppearanceComponent()->OutfitMeshes;
        auto* Character=World->SpawnActorDeferred<AOBCharacter>(Class,FTransform::Identity,nullptr,nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Character->AutoPossessAI=EAutoPossessAI::Disabled;
        Character->FinishSpawning(FTransform::Identity);
        auto Check=[&]() {
            auto* Appearance=Character->GetSoldierAppearanceComponent();
            if (!TestNotNull(TEXT("Appearance is a native character property"),Appearance)) return;
            TInlineComponentArray<UOBModularSoldierComponent*> Components; Character->GetComponents(Components);
            TestEqual(TEXT("Character has exactly one appearance component"),Components.Num(),1);
            TestTrue(TEXT("Appearance component is inherited from C++"),Appearance->CreationMethod==EComponentCreationMethod::Native);
            TestNotNull(TEXT("Body mesh configuration survives Blueprint serialization"),Appearance->BodyMesh.Get());
            TestNotNull(TEXT("Animation configuration survives Blueprint serialization"),Appearance->AnimationClass.Get());
            TestEqual(TEXT("The Blueprint's authored outfit count is preserved outside outfit maps"),Appearance->OutfitMeshes.Num(),AuthoredMeshes.Num());
            for (int32 Index=0;Index<FMath::Min(Appearance->OutfitMeshes.Num(),AuthoredMeshes.Num());++Index)
                TestEqual(TEXT("Every authored mesh is preserved"),Appearance->OutfitMeshes[Index].Get(),AuthoredMeshes[Index].Get());
            auto* Leader=Character->GetMesh();
            TestTrue(TEXT("Character uses the configured Soldier body"),Appearance->BodyMesh && Leader->GetSkeletalMeshAsset()==Appearance->BodyMesh);
            TestEqual(TEXT("Character uses the configured Soldier animation"),Leader->GetAnimClass(),Appearance->AnimationClass.Get());
            TestTrue(TEXT("Weapon attachment socket exists"),Leader->DoesSocketExist(TEXT("WeaponSocket")));
            TestNotNull(TEXT("Ragdoll physics asset exists"),Leader->GetPhysicsAsset());
            TestEqual(TEXT("Every authored visible part is assembled"),Appearance->GetVisualParts().Num(),AuthoredMeshes.Num());
            if(auto* Player=Cast<AOBPlayerCharacter>(Character))
            {
                auto* Camera=Player->GetPlayerCamera();const auto Original=Camera->GetRelativeTransform();
                Camera->SetRelativeLocation(FVector(60,0,64));Appearance->UpdateFirstPersonVisibility();
                TestTrue(TEXT("Saved player BP hides the weapon in first person"),Player->GetWeaponMesh()->bOwnerNoSee);
                TestTrue(TEXT("Owner hiding preserves the world weapon and IK"),Player->GetWeaponMesh()->IsVisible() && !Player->GetWeaponMesh()->bHiddenInGame);
                Camera->SetRelativeLocation(FVector(-300,0,140));Appearance->UpdateFirstPersonVisibility();
                TestFalse(TEXT("Returning to third person restores the weapon"),Player->GetWeaponMesh()->bOwnerNoSee);
                Camera->SetRelativeTransform(Original);Appearance->UpdateFirstPersonVisibility();
            }
            else TestFalse(TEXT("Enemy weapon remains visible"),Character->GetWeaponMesh()->bOwnerNoSee);
            for (USkeletalMeshComponent* Part : Appearance->GetVisualParts())
            {
                TestTrue(TEXT("Outfit part remains registered"),IsValid(Part) && Part->IsRegistered());
                if (!IsValid(Part)) continue;
                TestTrue(TEXT("Outfit follows the body pose"),Part->LeaderPoseComponent.Get()==Leader);
                TestTrue(TEXT("Outfit cannot intercept hit-box traces"),Part->GetCollisionEnabled()==ECollisionEnabled::NoCollision);
            }
        };
        Check();
        Character->RerunConstructionScripts();
        Check();
        Character->Destroy();
    }
    return !HasAnyErrors();
}
#endif

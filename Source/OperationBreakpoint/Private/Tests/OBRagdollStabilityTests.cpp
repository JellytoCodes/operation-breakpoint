#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/DamageEvents.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Character/OBEnemyCharacter.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBRagdollStabilityTest, "OperationBreakpoint.Combat.RagdollStabilitySettings",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOBRagdollStabilityTest::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    FURL URL; URL.AddOption(TEXT("game=/Script/Engine.GameModeBase")); World->SetGameMode(URL);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Class = LoadClass<AOBEnemyCharacter>(nullptr, TEXT("/Game/Blueprints/Enemy/BP_EnemyCharacter.BP_EnemyCharacter_C"));
    if (!TestNotNull(TEXT("Saved enemy class loads"), Class)) return false;
    auto Spawn = [&](const FVector& Position) {
        const FTransform Transform(Position);
        auto* Enemy = World->SpawnActorDeferred<AOBEnemyCharacter>(Class, Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Enemy->AutoPossessAI = EAutoPossessAI::Disabled;
        Enemy->FinishSpawning(Transform); Enemy->DispatchBeginPlay();
        return Enemy;
    };
    auto* Enemy = Spawn(FVector(0, 0, 250));
    auto* Mesh = Enemy->GetMesh();
    auto* Root = Mesh->GetBodyInstance(Enemy->RagdollRootBoneName);
    if (!TestNotNull(TEXT("The actual enemy has a pelvis physics body"), Root)) return false;
    auto* Asset = Mesh->GetPhysicsAsset();
    if (!TestNotNull(TEXT("The actual shared physics asset loads"), Asset)) return false;
    TArray<FConstraintProfileProperties> OriginalProfiles;
    for (const auto& Template : Asset->ConstraintSetup)
        OriginalProfiles.Add(Template ? Template->DefaultInstance.ProfileInstance : FConstraintProfileProperties());
    const int32 OriginalDisabledPairs = Asset->CollisionDisableTable.Num();
    const auto* OriginalMaterial = Root->GetSimplePhysicalMaterial();
    auto* Template = OriginalMaterial ? DuplicateObject<UPhysicalMaterial>(OriginalMaterial, Enemy)
        : NewObject<UPhysicalMaterial>(Enemy, NAME_None, RF_Transient);
    Template->Friction = .9f; Template->StaticFriction = .8f; Template->Restitution = .3f;
    Template->Density = 2.7f;
    Template->SleepLinearVelocityThreshold = 7.f;
    Template->SleepAngularVelocityThreshold = .2f;
    Template->SleepCounterThreshold = 11;
    Mesh->SetPhysMaterialOverride(Template);
    Root->SetPhysMaterialOverride(Template);
    Enemy->bLockRagdollHorizontalMovement = true; // An old serialized Blueprint value must not restore the world anchor.
    Root->bLockXTranslation = Root->bLockYTranslation = true;
    Root->SetDOFLock(EDOFMode::SixDOF);
    FDamageEvent Damage;
    Enemy->TakeDamage(Enemy->GetMaxHealth() + 1.f, Damage, nullptr, nullptr);
    Enemy->BeginDeathPhysics(); // Do not wait for an animation notify in this settings-only test world.
    TestTrue(TEXT("Legacy lock setting cannot keep a world DOF constraint"), Root->DOFMode == EDOFMode::None && Root->DOFConstraint == nullptr);
    TestTrue(TEXT("All pelvis world-axis lock flags are cleared"),
        !Root->bLockTranslation && !Root->bLockRotation && !Root->bLockXTranslation && !Root->bLockYTranslation
        && !Root->bLockZTranslation && !Root->bLockXRotation && !Root->bLockYRotation && !Root->bLockZRotation);
    TestTrue(TEXT("The corpse retains world-static and world-dynamic collision"),
        Mesh->GetCollisionResponseToChannel(ECC_WorldStatic) == ECR_Block && Mesh->GetCollisionResponseToChannel(ECC_WorldDynamic) == ECR_Block);
    TestTrue(TEXT("The corpse keeps pawn and visibility exclusion"),
        Mesh->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Ignore && Mesh->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Ignore);
    TestTrue(TEXT("The live movement capsule remains disabled after death"), Enemy->GetCapsuleComponent()->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
    int32 ConfiguredBodies = 0;
    Mesh->ForEachBodyBelow(Enemy->RagdollRootBoneName, true, false, [&](FBodyInstance* Body) {
        if (!Body) return;
        ++ConfiguredBodies;
        TestTrue(TEXT("Bodies remain simulated rather than frozen"), Body->IsInstanceSimulatingPhysics());
        TestEqual(TEXT("Position solver override is sixteen"), Body->GetPositionSolverIterationCount(), 16);
        TestEqual(TEXT("Velocity solver override is eight"), Body->GetVelocitySolverIterationCount(), 8);
        TestEqual(TEXT("Projection solver override is one"), Body->GetProjectionSolverIterationCount(), 1);
        TestTrue(TEXT("Angular speed has an eight-radian safety cap"), FMath::IsNearlyEqual(Body->GetMaxAngularVelocityInRadians(), 8.f, .001f));
        TestEqual(TEXT("Initial-overlap separation limit is preserved"), Body->GetMaxDepenetrationVelocity(), 25.f);
        TestEqual(TEXT("Original damping is preserved"), Body->LinearDamping, 6.f);
        TestEqual(TEXT("Original angular damping is preserved"), Body->AngularDamping, 10.f);
    });
    TestTrue(TEXT("The body settings were applied to an actual articulated corpse"), ConfiguredBodies > 1);
    for (const auto* Constraint : Mesh->Constraints)
        if (Constraint)
        {
            const auto& Profile = Constraint->ProfileInstance;
            TestTrue(TEXT("Runtime joints do not project/teleport or use shock propagation at wall contacts"),
                !Profile.bEnableProjection && !Profile.bEnableShockPropagation);
            TestTrue(TEXT("Runtime joints use hard angular limits"), !Profile.ConeLimit.bSoftConstraint && !Profile.TwistLimit.bSoftConstraint);
            const int32 SourceIndex = Asset->FindConstraintIndex(Constraint->JointName);
            const auto* Source = Asset->ConstraintSetup.IsValidIndex(SourceIndex) ? Asset->ConstraintSetup[SourceIndex].Get() : nullptr;
            if (TestNotNull(TEXT("The runtime joint has an authored constraint"), Source))
            {
                const auto& Authored = Source->DefaultInstance.ProfileInstance;
                TestEqual(TEXT("Swing one angular range is preserved"), Profile.ConeLimit.Swing1LimitDegrees, Authored.ConeLimit.Swing1LimitDegrees);
                TestEqual(TEXT("Swing two angular range is preserved"), Profile.ConeLimit.Swing2LimitDegrees, Authored.ConeLimit.Swing2LimitDegrees);
                TestEqual(TEXT("Twist angular range is preserved"), Profile.TwistLimit.TwistLimitDegrees, Authored.TwistLimit.TwistLimitDegrees);
                TestEqual(TEXT("Authored swing spring tuning is preserved"), Profile.ConeLimit.Stiffness, Authored.ConeLimit.Stiffness);
                TestEqual(TEXT("Authored twist damping tuning is preserved"), Profile.TwistLimit.Damping, Authored.TwistLimit.Damping);
            }
        }
    auto* Material = Enemy->RagdollContactMaterial.Get();
    if (!TestNotNull(TEXT("The corpse owns a dedicated contact material"), Material)) return false;
    TestTrue(TEXT("The contact material is transient and local to this corpse"), Material->HasAnyFlags(RF_Transient) && Material->GetOuter() == Enemy && Material != Template);
    TestEqual(TEXT("Dynamic friction is four tenths"), Material->Friction, .4f);
    TestEqual(TEXT("Static friction is four tenths"), Material->StaticFriction, .4f);
    TestEqual(TEXT("Restitution is zero"), Material->Restitution, 0.f);
    TestTrue(TEXT("Pairwise friction uses the minimum combine mode"), Material->bOverrideFrictionCombineMode && Material->FrictionCombineMode == EFrictionCombineMode::Min);
    TestTrue(TEXT("The corpse requests minimum restitution combine"), Material->bOverrideRestitutionCombineMode && Material->RestitutionCombineMode == EFrictionCombineMode::Min);
    TestEqual(TEXT("Cloning preserves density"), Material->Density, Template->Density);
    TestEqual(TEXT("Cloning preserves linear sleep threshold"), Material->SleepLinearVelocityThreshold, Template->SleepLinearVelocityThreshold);
    TestEqual(TEXT("Cloning preserves angular sleep threshold"), Material->SleepAngularVelocityThreshold, Template->SleepAngularVelocityThreshold);
    TestEqual(TEXT("Cloning preserves the natural sleep counter"), Material->SleepCounterThreshold, Template->SleepCounterThreshold);
    TestEqual(TEXT("The original material's friction is untouched"), Template->Friction, .9f);
    TestEqual(TEXT("The original material's restitution is untouched"), Template->Restitution, .3f);
    TestEqual(TEXT("No source self-collision pair was changed"), Asset->CollisionDisableTable.Num(), OriginalDisabledPairs);
    for (int32 I = 0; I < Asset->ConstraintSetup.Num(); ++I)
        if (const auto* Source = Asset->ConstraintSetup[I].Get())
        {
            const auto& Profile = Source->DefaultInstance.ProfileInstance;
            TestEqual(TEXT("The source constraint's projection flag is unchanged"), bool(Profile.bEnableProjection), bool(OriginalProfiles[I].bEnableProjection));
            TestEqual(TEXT("The source constraint's soft swing flag is unchanged"), bool(Profile.ConeLimit.bSoftConstraint), bool(OriginalProfiles[I].ConeLimit.bSoftConstraint));
            TestEqual(TEXT("The source constraint's soft twist flag is unchanged"), bool(Profile.TwistLimit.bSoftConstraint), bool(OriginalProfiles[I].TwistLimit.bSoftConstraint));
        }
    Enemy->BeginDeathPhysics();
    TestTrue(TEXT("Repeated death setup reuses the same local material"), Enemy->RagdollContactMaterial == Material);

    auto* Invalid = Spawn(FVector(2000, 0, 250));
    Invalid->RagdollPositionSolverIterations = 0; Invalid->RagdollVelocitySolverIterations = 1000; Invalid->RagdollProjectionSolverIterations = -2;
    Invalid->RagdollMaxAngularVelocity = std::numeric_limits<float>::quiet_NaN();
    Invalid->RagdollFriction = std::numeric_limits<float>::quiet_NaN(); Invalid->RagdollStaticFriction = 100.f; Invalid->RagdollRestitution = -1.f;
    Invalid->TakeDamage(Invalid->GetMaxHealth() + 1.f, Damage, nullptr, nullptr); Invalid->BeginDeathPhysics();
    auto* SafeBody = Invalid->GetMesh()->GetBodyInstance(Invalid->RagdollRootBoneName);
    if (!TestNotNull(TEXT("The tuning clamp fixture has a physics body"), SafeBody)) return false;
    TestEqual(TEXT("Position iterations clamp above zero"), SafeBody->GetPositionSolverIterationCount(), 1);
    TestEqual(TEXT("Velocity iterations clamp to a bounded budget"), SafeBody->GetVelocitySolverIterationCount(), 64);
    TestEqual(TEXT("Projection iterations cannot wrap a negative value"), SafeBody->GetProjectionSolverIterationCount(), 0);
    TestTrue(TEXT("Nonfinite angular settings fall back to a valid cap"), FMath::IsNearlyEqual(SafeBody->GetMaxAngularVelocityInRadians(), 8.f, .001f));
    TestEqual(TEXT("Nonfinite friction uses its stable default"), Invalid->RagdollContactMaterial->Friction, .4f);
    TestEqual(TEXT("Excessive static friction is bounded"), Invalid->RagdollContactMaterial->StaticFriction, 1.f);
    TestEqual(TEXT("Negative restitution is bounded"), Invalid->RagdollContactMaterial->Restitution, 0.f);
    TestTrue(TEXT("Each corpse owns its material independently"), Invalid->RagdollContactMaterial != Material);
    return !HasAnyErrors();
}
#endif

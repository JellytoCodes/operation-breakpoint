#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Character/OBEnemyCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"

namespace OBEnemyHitBoxTest
{
    struct FTestWorld
    {
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);

        FTestWorld()
        {
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            World->SetGameInstance(NewObject<UGameInstance>(GEngine));
            FURL URL;
            URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
            World->SetGameMode(URL); // APawn rejects damage in worlds without an authoritative GameMode.
        }

        ~FTestWorld()
        {
            World->DestroyWorld(false);
            GEngine->DestroyWorldContext(World);
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBEnemyHitBoxCombatTest,
    "OperationBreakpoint.Combat.EnemyHitBoxes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOBEnemyHitBoxCombatTest::RunTest(const FString& Parameters)
{
    // Exercise the real Blueprint inheritance, attachments and engine traces, not just the multiplier helper.
    for (const TCHAR* ClassPath : {
        TEXT("/Game/Blueprints/Enemy/BP_EnemyCharacter.BP_EnemyCharacter_C")})
    {
    UClass* EnemyClass = LoadClass<AOBEnemyCharacter>(nullptr, ClassPath);
    if (!TestNotNull(TEXT("Enemy Blueprint loads"), EnemyClass)) return false;

    OBEnemyHitBoxTest::FTestWorld TestWorld;
    if (!TestNotNull(TEXT("Test world has an authoritative GameMode"), TestWorld.World->GetAuthGameMode())) return false;
    const TArray<FName> BoxNames = {
        TEXT("HeadHitBox"), TEXT("BodyHitBox"),
        TEXT("LeftUpperArmHitBox"), TEXT("LeftLowerArmHitBox"),
        TEXT("RightUpperArmHitBox"), TEXT("RightLowerArmHitBox"),
        TEXT("LeftUpperLegHitBox"), TEXT("LeftLowerLegHitBox"),
        TEXT("RightUpperLegHitBox"), TEXT("RightLowerLegHitBox")
    };

    for (int32 BoxIndex = 0; BoxIndex < BoxNames.Num(); ++BoxIndex)
    {
        const FTransform Transform(FVector(BoxIndex * 1000.0f, 0.0f, 200.0f));
        AOBEnemyCharacter* Enemy = TestWorld.World->SpawnActorDeferred<AOBEnemyCharacter>(
            EnemyClass, Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!TestNotNull(TEXT("Enemy spawns"), Enemy)) return false;
        Enemy->AutoPossessAI = EAutoPossessAI::Disabled;
        Enemy->FinishSpawning(Transform);
        const FVector AuthoredMeshScale=Enemy->GetMesh()->GetRelativeScale3D();
        const FVector AuthoredMeshOrigin=Enemy->GetMesh()->GetRelativeLocation();
        const float AuthoredCapsuleRadius=Enemy->GetCapsuleComponent()->GetScaledCapsuleRadius();
        Enemy->DispatchBeginPlay();
        TestTrue(TEXT("Enemy visuals grow twenty percent from their authored size"),Enemy->GetMesh()->GetRelativeScale3D().Equals(AuthoredMeshScale*1.2f));
        TestTrue(TEXT("Enlargement preserves the authored feet origin"),Enemy->GetMesh()->GetRelativeLocation().Equals(AuthoredMeshOrigin));
        TestEqual(TEXT("Enlargement preserves the navigation capsule radius"),Enemy->GetCapsuleComponent()->GetScaledCapsuleRadius(),AuthoredCapsuleRadius);
        Enemy->GetMesh()->RefreshBoneTransforms();
        TestTrue(TEXT("Enemy accepts damage after BeginPlay"), Enemy->CanBeDamaged());
        TestEqual(TEXT("Capsule retains pawn movement collision"),Enemy->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_Pawn),ECR_Block);
        TInlineComponentArray<USkeletalMeshComponent*> CharacterMeshes(Enemy);
        for (USkeletalMeshComponent* CharacterMesh : CharacterMeshes)
        {
            TestEqual(TEXT("Animated meshes cannot block other pawns"),CharacterMesh->GetCollisionResponseToChannel(ECC_Pawn),ECR_Ignore);
            TestFalse(TEXT("Animated meshes do not carve navigation"),CharacterMesh->CanEverAffectNavigation());
        }

        TInlineComponentArray<UBoxComponent*> Boxes;
        Enemy->GetComponents(Boxes);
        TestEqual(TEXT("Blueprint inherits exactly ten boxes"), Boxes.Num(), 10);
        UBoxComponent* TargetBox = nullptr;
        for (UBoxComponent* Box : Boxes)
        {
            TestTrue(*FString::Printf(TEXT("%s has a valid attachment"), *Box->GetName()),
                Enemy->GetMesh()->DoesSocketExist(Box->GetAttachSocketName()));
            TestTrue(TEXT("Hit boxes start query-only"), Box->GetCollisionEnabled() == ECollisionEnabled::QueryOnly);
            TestFalse(TEXT("Hit boxes do not generate overlaps"), Box->GetGenerateOverlapEvents());
            if (Box->GetFName() == BoxNames[BoxIndex]) TargetBox = Box;
        }
        if (!TestNotNull(TEXT("Target box exists"), TargetBox)) return false;
        AddInfo(FString::Printf(TEXT("Testing %s | Tags=%s"), *TargetBox->GetName(),
            *FString::JoinBy(TargetBox->ComponentTags, TEXT(","), [](FName Tag) { return Tag.ToString(); })));
        if (BoxIndex >= 2)
        {
            const FName EndBones[] = {TEXT("lowerarm_l"), TEXT("hand_l"), TEXT("lowerarm_r"), TEXT("hand_r"),
                TEXT("calf_l"), TEXT("foot_l"), TEXT("calf_r"), TEXT("foot_r")};
            const FTransform BoneTransform = Enemy->GetMesh()->GetSocketTransform(TargetBox->GetAttachSocketName());
            const FVector EndPosition = Enemy->GetMesh()->GetSocketLocation(EndBones[BoxIndex - 2]);
            const FVector Midpoint = (BoneTransform.GetLocation() + EndPosition) * 0.5f;
            const FVector LocalMidpoint = TargetBox->GetComponentTransform().InverseTransformPosition(Midpoint);
            const FVector Extent = TargetBox->GetUnscaledBoxExtent();
            TestTrue(*FString::Printf(TEXT("%s covers the correct limb segment"), *TargetBox->GetName()),
                FMath::Abs(LocalMidpoint.X) <= Extent.X && FMath::Abs(LocalMidpoint.Y) <= Extent.Y && FMath::Abs(LocalMidpoint.Z) <= Extent.Z);
        }

        // Isolate each box to test all ten independently of pose-dependent overlaps.
        for (UBoxComponent* Box : Boxes)
        {
            if (Box != TargetBox) Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }
        const FVector Center = TargetBox->GetComponentLocation();
        const FVector Start = Center - FVector(0.0f, 200.0f, 0.0f);
        const FVector End = Center + FVector(0.0f, 200.0f, 0.0f);
        FHitResult Hit;
        for (bool bComplex : {false, true})
        {
            const FCollisionQueryParams Params(SCENE_QUERY_STAT(OBHitBoxTest), bComplex);
            TestTrue(TEXT("Simple and complex shots find the box"),
                TestWorld.World->LineTraceSingleByChannel(Hit, Start, End, OBCombatCollision::PlayerShotTarget, Params));
            TestTrue(TEXT("Mesh and capsule do not intercept the shot"), Hit.GetComponent() == TargetBox);
        }

        if (BoxIndex == 0)
        {
            AActor* Wall = TestWorld.World->SpawnActor<AActor>();
            UBoxComponent* WallBox = NewObject<UBoxComponent>(Wall);
            Wall->SetRootComponent(WallBox);
            Wall->AddInstanceComponent(WallBox);
            WallBox->SetBoxExtent(FVector(50.0f, 5.0f, 50.0f));
            WallBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            WallBox->SetCollisionObjectType(ECC_WorldStatic);
            WallBox->SetCollisionResponseToAllChannels(ECR_Block);
            WallBox->SetGenerateOverlapEvents(false);
            WallBox->RegisterComponent();
            Wall->SetActorLocation(Center - FVector(0.0f, 100.0f, 0.0f));
            FHitResult WallHit;
            FCollisionResponseParams ShotResponses(ECR_Ignore);
            ShotResponses.CollisionResponse.SetResponse(OBCombatCollision::PlayerShotTarget, ECR_Block);
            TestWorld.World->LineTraceSingleByChannel(WallHit, Start, End, OBCombatCollision::PlayerShotTarget,
                FCollisionQueryParams(SCENE_QUERY_STAT(OBHitBoxWallTest), true), ShotResponses);
            TestTrue(TEXT("World cover cannot intercept a character shot"), WallHit.GetComponent() == TargetBox);
            Wall->Destroy();
        }

        FHitResult MeshHit(Enemy, Enemy->GetMesh(), Center, FVector::UpVector);
        MeshHit.BoneName = TEXT("head");
        TestEqual(TEXT("Bone names alone do not cause point damage"),
            UGameplayStatics::ApplyPointDamage(Enemy, 40.0f, FVector::ForwardVector,
                MeshHit, nullptr, nullptr, UDamageType::StaticClass()), 0.0f);
        TestEqual(TEXT("Zero damage is ignored"),
            UGameplayStatics::ApplyPointDamage(Enemy, 0.0f, FVector::ForwardVector,
                Hit, nullptr, nullptr, UDamageType::StaticClass()), 0.0f);
        TestEqual(TEXT("Initial health"), Enemy->GetCurrentHealth(), 100.0f);
        TestTrue(TEXT("Rejected and zero-damage hits produce no confirmation"), Enemy->GetLastHitFeedback().HitBoxName.IsNone());

        int32 KilledCount = 0;
        Enemy->OnKilled.AddLambda([&KilledCount](AOBEnemyCharacter*) { ++KilledCount; });
        const int32 ShotsToKill = BoxIndex == 0 ? 1 : BoxIndex == 1 ? 3 : 5;
        const float DamagePerShot = BoxIndex == 0 ? 100.0f : BoxIndex == 1 ? 40.0f : 20.0f;
        for (int32 Shot = 1; Shot <= ShotsToKill; ++Shot)
        {
            UGameplayStatics::ApplyPointDamage(Enemy, 40.0f, FVector::ForwardVector,
                Hit, nullptr, nullptr, UDamageType::StaticClass());
            TestEqual(TEXT("Damage accumulates per hit zone"), Enemy->GetCurrentHealth(),
                FMath::Max(0.0f, 100.0f - Shot * DamagePerShot));
            TestEqual(TEXT("Encounter kill callback only fires on lethal damage"),
                KilledCount, Shot == ShotsToKill ? 1 : 0);
            const FOBEnemyHitFeedback Feedback = Enemy->GetLastHitFeedback();
            TestEqual(TEXT("Feedback identifies the exact box"), Feedback.HitBoxName, TargetBox->GetFName());
            const FName ExpectedZone = BoxIndex == 0 ? TEXT("HitZone.Head") : BoxIndex == 1 ? TEXT("HitZone.Body")
                : BoxIndex < 6 ? TEXT("HitZone.Arm") : TEXT("HitZone.Leg");
            TestEqual(TEXT("Feedback identifies the damage zone"), Feedback.HitZoneTag, ExpectedZone);
            TestTrue(TEXT("Feedback uses the trace contact, not the box center"), Feedback.ImpactPoint.Equals(Hit.ImpactPoint));
            TestTrue(TEXT("Feedback preserves the impact normal"), Feedback.ImpactNormal.Equals(Hit.ImpactNormal));
            TestEqual(TEXT("Feedback reports applied damage"), Feedback.Damage, DamagePerShot);
            TestEqual(TEXT("Feedback reports health after damage"), Feedback.RemainingHealth, Enemy->GetCurrentHealth());
            TestEqual(TEXT("Lethal shots still produce hit feedback"), Feedback.bFatal, Shot == ShotsToKill);
        }
        for (UBoxComponent* Box : Boxes)
        {
            TestTrue(TEXT("Death disables every hit box"), Box->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
        }
        FHitResult DeadHit = Hit;
        DeadHit.ImpactPoint += FVector(100.0f, 0.0f, 0.0f);
        TestEqual(TEXT("Dead enemies reject further hits"),
            UGameplayStatics::ApplyPointDamage(Enemy, 40.0f, FVector::ForwardVector,
                DeadHit, nullptr, nullptr, UDamageType::StaticClass()), 0.0f);
        TestTrue(TEXT("Dead enemy hits do not produce new feedback"), Enemy->GetLastHitFeedback().ImpactPoint.Equals(Hit.ImpactPoint));
        TestEqual(TEXT("Death callback is not duplicated"), KilledCount, 1);
        Enemy->OnKilled.Clear();
        Enemy->DropWeapon();
        Enemy->BeginDeathPhysics();
        TestTrue(TEXT("Ragdoll does not intercept subsequent shots"),
            Enemy->GetMesh()->GetCollisionResponseToChannel(OBCombatCollision::PlayerShotTarget) == ECR_Ignore);
        Enemy->Destroy();
    }
    }
    return true;
}

#endif

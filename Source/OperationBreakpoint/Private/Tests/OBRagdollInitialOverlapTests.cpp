#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/DamageEvents.h"
#include "Character/OBEnemyCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "World/OBBulletActor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBRagdollInitialOverlapTest, "OperationBreakpoint.Combat.RagdollInitialOverlap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOBRagdollInitialOverlapTest::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    FURL URL; URL.AddOption(TEXT("game=/Script/Engine.GameModeBase")); World->SetGameMode(URL);
    if (!World->GetPhysicsScene()) World->CreatePhysicsScene();
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Class = LoadClass<AOBEnemyCharacter>(nullptr, TEXT("/Game/Blueprints/Enemy/BP_EnemyCharacter.BP_EnemyCharacter_C"));
    if (!TestNotNull(TEXT("The saved enemy class loads"), Class)) return false;
    auto Spawn = [&](const FVector& Position) {
        const FTransform Transform(Position);
        auto* Enemy = World->SpawnActorDeferred<AOBEnemyCharacter>(Class, Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Enemy->AutoPossessAI = EAutoPossessAI::Disabled;
        Enemy->FinishSpawning(Transform); Enemy->DispatchBeginPlay();
        Enemy->GetMesh()->SetCollisionProfileName(TEXT("Ragdoll"));
        Enemy->GetMesh()->RefreshBoneTransforms();
        return Enemy;
    };
    auto Bounds = [](AOBEnemyCharacter* Enemy) {
        FBox Result(ForceInit); auto* Mesh = Enemy->GetMesh();
        if (auto* Asset = Mesh->GetPhysicsAsset())
            for (const auto& Setup : Asset->SkeletalBodySetups)
                if (Setup && Mesh->DoesSocketExist(Setup->BoneName))
                    Result += Setup->AggGeom.CalcAABB(Mesh->GetSocketTransform(Setup->BoneName, RTS_World));
        return Result;
    };
    auto AddBox = [&](const FVector& Position, const FVector& Extent, ECollisionChannel ObjectType, AActor* ExistingOwner = nullptr) {
        auto* Owner = ExistingOwner ? ExistingOwner : World->SpawnActor<AActor>();
        auto* Box = NewObject<UBoxComponent>(Owner); Owner->AddInstanceComponent(Box);
        if (!ExistingOwner) Owner->SetRootComponent(Box);
        Box->SetBoxExtent(Extent); Box->SetCollisionObjectType(ObjectType);
        Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); Box->SetCollisionResponseToAllChannels(ECR_Block);
        Box->SetGenerateOverlapEvents(false); Box->SetWorldLocation(Position); Box->RegisterComponent();
        return Box;
    };

    auto* Enemy = Spawn(FVector(0, 0, 250));
    auto* Mesh = Enemy->GetMesh(); auto* Asset = Mesh->GetPhysicsAsset();
    if (!TestNotNull(TEXT("The test uses the saved articulated PhysicsAsset"), Asset)) return false;
    const int32 OriginalBodyCount = Asset->SkeletalBodySetups.Num();
    const FVector ActorPosition = Enemy->GetActorLocation();
    const FVector ClearMeshPosition = Mesh->GetComponentLocation();
    TestTrue(TEXT("A clear pose stays in its authored position"), Enemy->ResolveInitialRagdollPenetration().IsNearlyZero());
    TestTrue(TEXT("Clearance does not move an unobstructed mesh"), Mesh->GetComponentLocation().Equals(ClearMeshPosition));

    const FBox BeforeWall = Bounds(Enemy);
    if (!TestTrue(TEXT("The saved collision geometry has valid pose bounds"), BeforeWall.IsValid != 0)) return false;
    const float WallFace = BeforeWall.Min.X + 3.f;
    auto* Wall = AddBox(FVector(WallFace - 10.f, BeforeWall.GetCenter().Y, BeforeWall.GetCenter().Z), FVector(10, 500, 500), ECC_WorldStatic);
    const FVector WallCorrection = Enemy->ResolveInitialRagdollPenetration();
    TestTrue(TEXT("A shallow wall overlap resolves toward the capsule's side"), WallCorrection.X > 3.f && FMath::Abs(WallCorrection.Y) < .1f);
    TestTrue(TEXT("The full authored pose clears the initial wall overlap"), Bounds(Enemy).Min.X >= WallFace + .4f);
    TestTrue(TEXT("Initial correction leaves the actor and movement capsule in place"), Enemy->GetActorLocation().Equals(ActorPosition));
    TestTrue(TEXT("A cleared wall pose requires no second correction"), Enemy->ResolveInitialRagdollPenetration().IsNearlyZero());
    TestTrue(TEXT("World blocking collision stays enabled"), Mesh->GetCollisionResponseToChannel(ECC_WorldStatic) == ECR_Block);
    Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    const FBox ClearBounds = Bounds(Enemy);
    auto* PawnVolume = AddBox(ClearBounds.GetCenter(), ClearBounds.GetExtent() + FVector(10), ECC_Pawn);
    auto* OwnVolume = AddBox(ClearBounds.GetCenter(), ClearBounds.GetExtent() + FVector(10), ECC_WorldStatic, Enemy);
    auto* Projectile = World->SpawnActor<AOBBulletActor>(ClearBounds.GetCenter(), FRotator::ZeroRotator);
    TestTrue(TEXT("Owner components, Pawn collision and projectiles cannot displace the corpse pose"), Enemy->ResolveInitialRagdollPenetration().IsNearlyZero());
    PawnVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision); OwnVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    if (Projectile) Projectile->Destroy();

    auto* FloorEnemy = Spawn(FVector(3000, 0, 250));
    const FBox BeforeFloor = Bounds(FloorEnemy);
    const float PlatformTop = BeforeFloor.Min.Z + 10.f;
    auto* Platform = AddBox(FVector(BeforeFloor.GetCenter().X, BeforeFloor.GetCenter().Y, PlatformTop - 2.5f), FVector(500, 500, 2.5f), ECC_WorldStatic);
    const FVector FloorCorrection = FloorEnemy->ResolveInitialRagdollPenetration();
    TestTrue(TEXT("A limb across a thin floor resolves upward toward the capsule"), FloorCorrection.Z > 0.f);
    TestTrue(TEXT("The initial pose clears the top of a thin platform"), Bounds(FloorEnemy).Min.Z >= PlatformTop + .4f);
    TestTrue(TEXT("Whole-pose correction has a bounded distance"), FloorCorrection.Size() <= 100.01f && !FloorCorrection.ContainsNaN());
    Platform->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    auto* Blocked = Spawn(FVector(6000, 0, 250));
    auto* EnclosingVolume = AddBox(Bounds(Blocked).GetCenter(), FVector(2000), ECC_WorldStatic);
    const FVector BoundedCorrection = Blocked->ResolveInitialRagdollPenetration();
    TestTrue(TEXT("An impossible clearance remains finite within the one-time movement budget"), !BoundedCorrection.ContainsNaN() && BoundedCorrection.Size() <= 100.01f);
    EnclosingVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    FDamageEvent Damage; Enemy->TakeDamage(Enemy->GetMaxHealth() + 1.f, Damage, nullptr, nullptr); Enemy->BeginDeathPhysics();
    const FTransform StartedPose = Mesh->GetComponentTransform();
    TestTrue(TEXT("An already simulated corpse is never repositioned by pose clearance"), Enemy->ResolveInitialRagdollPenetration().IsNearlyZero());
    Enemy->BeginDeathPhysics();
    TestTrue(TEXT("Repeated death setup does not translate the mesh again"), Mesh->GetComponentTransform().Equals(StartedPose));
    TestEqual(TEXT("The shared PhysicsAsset retains every body"), Asset->SkeletalBodySetups.Num(), OriginalBodyCount);
    return !HasAnyErrors();
}
#endif

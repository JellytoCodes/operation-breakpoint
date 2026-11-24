#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBEnemyCharacter.h"
#include "Character/OBAllyCharacter.h"
#include "Character/OBHostageCharacter.h"
#include "Character/OBActionComponent.h"
#include "Core/OBPlayerController.h"
#include "Core/OBCombatCollision.h"
#include "Training/OBTrainingTarget.h"
#include "World/OBBulletActor.h"
#include "World/OBRPGRocketActor.h"
#include "World/OBEncounterSpawner.h"
#include "Stage/OBStageFlowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/Script.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBCombatCollisionTest, "OperationBreakpoint.Combat.CollisionFiltering",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOBCombatCollisionTest::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    FURL URL; URL.AddOption(TEXT("game=/Script/Engine.GameModeBase")); World->SetGameMode(URL);
    if (!World->GetPhysicsScene()) World->CreatePhysicsScene();
    FEditorScriptExecutionGuard ScriptExecution;

    for (const auto& Channel : {
        TPair<ECollisionChannel, FName>(OBCombatCollision::PlayerShotTarget, TEXT("OBPlayerShotTarget")),
        TPair<ECollisionChannel, FName>(OBCombatCollision::EnemyBullet, TEXT("OBEnemyBullet")),
        TPair<ECollisionChannel, FName>(OBCombatCollision::PlayerBulletTarget, TEXT("OBPlayerBulletTarget"))})
        TestEqual(TEXT("Combat object channel is registered in project settings"),
            UCollisionProfile::Get()->ReturnChannelNameFromContainerIndex(Channel.Key), Channel.Value);

    auto* PlayerClass = LoadClass<AOBPlayerCharacter>(nullptr, TEXT("/Game/Blueprints/Player/BP_PlayerCharacter.BP_PlayerCharacter_C"));
    auto* EnemyClass = LoadClass<AOBEnemyCharacter>(nullptr, TEXT("/Game/Blueprints/Enemy/BP_EnemyCharacter.BP_EnemyCharacter_C"));
    auto* BulletClass = LoadClass<AOBBulletActor>(nullptr, TEXT("/Game/Blueprints/World/Bullet/BP_Bullet_7mm.BP_Bullet_7mm_C"));
    if (!TestNotNull(TEXT("Player Blueprint loads"), PlayerClass) ||
        !TestNotNull(TEXT("Enemy Blueprint loads"), EnemyClass) ||
        !TestNotNull(TEXT("Saved bullet Blueprint loads"), BulletClass)) return false;

    auto* Player = World->SpawnActor<AOBPlayerCharacter>(PlayerClass, FVector(0, 0, 200), FRotator::ZeroRotator);
    auto* PC = World->SpawnActor<AOBPlayerController>(); World->AddController(PC); PC->Possess(Player);
    Player->SetCanBeDamaged(true);
    Player->GetActionComponent()->SetExternalActionState(true);
    PC->SetTrainingControls(true, true, false, false); PC->SetTrainingAmmo(30, 30);
    PC->FireSound.Reset();
    FHitResult Resolved;
    PC->OnShotResolved.AddLambda([&](const FHitResult& Hit) { Resolved = Hit; });
    TestEqual(TEXT("Movement capsule keeps the original Pawn object type"), Player->GetCapsuleComponent()->GetCollisionObjectType(), ECC_Pawn);

    auto* Wall = World->SpawnActor<AActor>();
    auto* WallBox = NewObject<UBoxComponent>(Wall);
    Wall->SetRootComponent(WallBox); Wall->AddInstanceComponent(WallBox);
    WallBox->SetBoxExtent(FVector(30, 120, 200));
    WallBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    WallBox->SetCollisionResponseToAllChannels(ECR_Block);
    WallBox->RegisterComponent();

    const FTransform EnemyTransform(FVector(2000, 0, 200));
    auto* Enemy = World->SpawnActorDeferred<AOBEnemyCharacter>(EnemyClass, EnemyTransform,
        nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    Enemy->AutoPossessAI = EAutoPossessAI::Disabled; Enemy->FinishSpawning(EnemyTransform);
    Enemy->DispatchBeginPlay(); Enemy->GetMesh()->RefreshBoneTransforms(); Enemy->SetCanBeDamaged(true);
    UBoxComponent* Head = nullptr;
    TInlineComponentArray<UBoxComponent*> Boxes(Enemy);
    for (auto* Box : Boxes) if (Box->GetFName() == TEXT("HeadHitBox")) Head = Box;
    if (!TestNotNull(TEXT("Real enemy head hit box exists"), Head)) return false;
    const FVector Aim = Head->GetComponentLocation();
    const FVector Start = Aim - FVector(1000, 0, 0), End = Aim + FVector(100, 0, 0);
    Wall->SetActorLocation(Aim - FVector(400, 0, 0));
    PC->ShotDamage = 1.f;
    FHitResult OldVisibility;
    World->LineTraceSingleByChannel(OldVisibility, Start, End, ECC_Visibility);
    TestTrue(TEXT("The old Visibility trace is intercepted by the same wall"), OldVisibility.GetActor() == Wall);
    for (ECollisionChannel ObjectType : {ECC_WorldStatic, ECC_WorldDynamic, ECC_Pawn, ECC_PhysicsBody})
    {
        WallBox->SetCollisionObjectType(ObjectType);
        const float Before = Enemy->GetCurrentHealth();
        PC->ShotTraceChannel = ECC_Visibility; // Emulate a retained Blueprint default.
        PC->FireAlongRay(Start, End);
        TestTrue(TEXT("Production shot ignores BlockAll scenery and resolves the intended head"), Resolved.GetComponent() == Head);
        TestTrue(TEXT("A wall cannot prevent actual point damage to the enemy"), Enemy->GetCurrentHealth() < Before);
    }

    auto* Ally = World->SpawnActor<AOBAllyCharacter>(Aim - FVector(600, 0, 0), FRotator::ZeroRotator);
    Ally->GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Block);
    auto* CrossingBullet = World->SpawnActor<AOBBulletActor>(BulletClass, Aim - FVector(700, 0, 0), FRotator::ZeroRotator);
    Cast<USphereComponent>(CrossingBullet->GetRootComponent())->SetCollisionResponseToAllChannels(ECR_Block);
    PC->FireAlongRay(Start, End);
    TestTrue(TEXT("An ally and an incoming projectile cannot absorb the player shot"), Resolved.GetComponent() == Head);
    CrossingBullet->Destroy(); Ally->Destroy();

    auto* Board = World->SpawnActor<AOBTrainingTarget>(FVector(2000, 2000, 200), FRotator::ZeroRotator);
    Board->SetTargetEnabled(true, true);
    const FVector BoardCenter = Board->BoardMesh->GetComponentLocation();
    Wall->SetActorLocation(BoardCenter - FVector(400, 0, 0));
    PC->FireAlongRay(BoardCenter - FVector(1000, 0, 0), BoardCenter + FVector(100, 0, 0));
    TestTrue(TEXT("Training scoring still receives a real raised-board hit behind scenery"), Board->IsScoringHit(Resolved));
    Board->SetTargetEnabled(false, true);
    PC->FireAlongRay(BoardCenter - FVector(1000, 0, 0), BoardCenter + FVector(100, 0, 0));
    TestFalse(TEXT("A lowered board and BlockAll scenery do not produce a scoring hit"), Resolved.bBlockingHit);

    // Sweep the actual saved bullet/RPG sphere through a blocker and then the player.
    // This exercises the dynamic hit delegate and point damage, rather than calling TakeDamage.
    for (UClass* ProjectileClass : {BulletClass, AOBRPGRocketActor::StaticClass()})
    for (ECollisionChannel ObjectType : {ECC_WorldStatic, ECC_WorldDynamic, ECC_Pawn, ECC_PhysicsBody})
    {
        WallBox->SetCollisionObjectType(ObjectType);
        Wall->SetActorLocation(Player->GetActorLocation() - FVector(450, 0, 0));
        Player->CurrentHealth = 5.f;
        auto* Projectile = World->SpawnActor<AOBBulletActor>(ProjectileClass,
            Player->GetActorLocation() - FVector(700, 0, 0), FRotator::ZeroRotator);
        auto* Sphere = Cast<USphereComponent>(Projectile->GetRootComponent());
        Sphere->SetCollisionObjectType(ECC_WorldDynamic);
        Sphere->SetCollisionResponseToAllChannels(ECR_Block); // Old serialized BP settings.
        Projectile->DispatchBeginPlay();
        FHitResult ObstacleHit;
        Sphere->MoveComponent(FVector(400, 0, 0), FQuat::Identity, true, &ObstacleHit);
        TestFalse(TEXT("Enemy projectile passes through scenery without an impact"), ObstacleHit.bBlockingHit);
        TestFalse(TEXT("Scenery cannot consume the projectile"), Projectile->IsActorBeingDestroyed());
        FHitResult PlayerHit;
        Sphere->MoveComponent(FVector(600, 0, 0), FQuat::Identity, true, &PlayerHit);
        TestTrue(TEXT("The next real swept impact reaches the player's dedicated receiver"),
            PlayerHit.GetComponent() == Player->EnemyBulletHitBox && PlayerHit.GetActor() == Player);
        TestTrue(TEXT("Only the player impact consumes the projectile"), Projectile->IsActorBeingDestroyed());
        TestEqual(TEXT("Actual collision applies the original single impact damage"), Player->CurrentHealth,
            ProjectileClass->IsChildOf(AOBRPGRocketActor::StaticClass()) ? 3.f : 4.f);
    }

    Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    Player->GetCharacterMovement()->Crouch(false);
    TestTrue(TEXT("The actual movement component can crouch"), Player->bIsCrouched);
    TestEqual(TEXT("Projectile receiver follows the crouched body height"),
        Player->EnemyBulletHitBox->GetUnscaledCapsuleHalfHeight(), Player->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight());
    Player->GetCharacterMovement()->UnCrouch(false);
    TestFalse(TEXT("The actual movement component can stand again"), Player->bIsCrouched);
    TestEqual(TEXT("Projectile receiver follows the restored standing body height"),
        Player->EnemyBulletHitBox->GetUnscaledCapsuleHalfHeight(), Player->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight());

    // Test hostage rules independently of imported asset availability. This native
    // fixture uses an existing body skeleton and never changes the saved hostage BPs.
    const FTransform HostageTransform(FVector(2000, 4000, 200));
    auto* Hostage = World->SpawnActorDeferred<AOBHostageCharacter>(AOBHostageCharacter::StaticClass(), HostageTransform);
    Hostage->GetMesh()->SetSkeletalMeshAsset(Enemy->GetMesh()->GetSkeletalMeshAsset());
    Hostage->GetMesh()->SetRelativeTransform(Enemy->GetMesh()->GetRelativeTransform());
    Hostage->FinishSpawning(HostageTransform); Hostage->DispatchBeginPlay();
    Hostage->GetMesh()->RefreshBoneTransforms();
    auto* Spawner = World->SpawnActor<AOBEncounterSpawner>();
    Spawner->PlacedEnemy = Hostage; Spawner->bParticipatesInCombat = false; Spawner->PrepareEncounter(86);
    auto* Action = Player->GetActionComponent();
    Action->BeginEncounter(86); Action->RegisterEnemy(Enemy); Action->FinalizeEncounterSetup(); Action->SetExternalActionState(true);
    auto* Flow = Player->GetStageFlowComponent();
    Flow->Player = Player; Flow->bActive = true; Flow->Phase = EOBStagePhase::Combat; Flow->CurrentZone = 6;
    Flow->RemainingSeconds = 30.f; Flow->HandleEncounterTriggered(86);
    const FVector HostageAim = Hostage->GetMesh()->GetSocketLocation(TEXT("head"));
    Wall->SetActorLocation(HostageAim - FVector(400, 0, 0));
    PC->FireAlongRay(HostageAim - FVector(1000, 0, 0), HostageAim + FVector(100, 0, 0));
    TestTrue(TEXT("The production shot filter preserves hostage hit boxes through scenery"), Resolved.GetActor() == Hostage);
    TestTrue(TEXT("An actual hostage hit still selects mission failure"), !Flow->DidSucceed()
        && (Flow->GetPhase() == EOBStagePhase::Ending || Flow->GetPhase() == EOBStagePhase::Complete));
    Flow->bActive = false;
    PC->OnShotResolved.Clear();
    return !HasAnyErrors();
}
#endif

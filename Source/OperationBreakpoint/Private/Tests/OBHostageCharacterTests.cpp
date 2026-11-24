#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/DamageEvents.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "EngineUtils.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "Character/OBHostageCharacter.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBActionComponent.h"
#include "Character/OBWeaponGripComponent.h"
#include "Character/OBModularSoldierComponent.h"
#include "World/OBEncounterSpawner.h"
#include "World/OBBulletActor.h"
#include "Core/OBPlayerController.h"
#include "Stage/OBStageFlowComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBDedicatedHostageTest, "OperationBreakpoint.Infiltration.DedicatedHostages",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOBDedicatedHostageTest::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    FURL URL; URL.AddOption(TEXT("game=/Script/Engine.GameModeBase")); World->SetGameMode(URL);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };

    auto* EnemyClass = LoadClass<AOBEnemyCharacter>(nullptr, TEXT("/Game/Blueprints/Enemy/BP_EnemyCharacter.BP_EnemyCharacter_C"));
    if (!TestNotNull(TEXT("Existing character mesh reference loads"), EnemyClass)) return false;
    const auto* EnemyDefaults = EnemyClass->GetDefaultObject<AOBEnemyCharacter>();
    auto* BodyMesh = EnemyDefaults->GetSoldierAppearanceComponent()->BodyMesh.Get();
    if (!BodyMesh) BodyMesh = EnemyDefaults->GetMesh()->GetSkeletalMeshAsset();
    if (!TestNotNull(TEXT("Existing body skeleton is available for hit boxes"), BodyMesh)) return false;
    const FTransform Transform(FVector(3000, 0, 200));
    auto* Hostage = World->SpawnActorDeferred<AOBHostageCharacter>(AOBHostageCharacter::StaticClass(), Transform);
    if (!TestNotNull(TEXT("Dedicated native hostage spawns"), Hostage)) return false;
    // Use the project's existing skeleton without making this code test depend on a newly authored BP/idle asset.
    Hostage->GetMesh()->SetSkeletalMeshAsset(BodyMesh);
    Hostage->GetMesh()->SetRelativeTransform(EnemyDefaults->GetMesh()->GetRelativeTransform());
    Hostage->FinishSpawning(Transform);
    Hostage->DispatchBeginPlay();
    Hostage->GetMesh()->RefreshBoneTransforms();
    TestFalse(TEXT("Hostage is noncombat from initialization"), Hostage->IsEncounterCombatant());
    TestTrue(TEXT("Automatic AI possession is disabled"), Hostage->AutoPossessAI == EAutoPossessAI::Disabled);
    TestTrue(TEXT("Hostage has no controller class or controller"), !Hostage->AIControllerClass && !Hostage->GetController());
    Hostage->SpawnDefaultController();
    TestNull(TEXT("Even an explicit default-controller request produces no AI"), Hostage->GetController());
    TestNull(TEXT("The public weapon accessor reports an unarmed character"), Hostage->GetWeaponMesh());
    TestFalse(TEXT("Weapon grip/ADS correction is disabled"), Hostage->GetWeaponGripComponent()->bEnabled);
    TestTrue(TEXT("Idle pose uses single-node animation instead of enemy attack animation"),
        Hostage->GetMesh()->GetAnimationMode() == EAnimationMode::AnimationSingleNode);
    TestTrue(TEXT("Hostage physical movement is disabled"), Hostage->GetCharacterMovement()->MovementMode == MOVE_None);
    TInlineComponentArray<USkeletalMeshComponent*> Meshes(Hostage);
    for (const auto* Mesh : Meshes)
        if (Mesh->GetFName() == TEXT("WeaponMesh"))
            TestTrue(TEXT("Inherited rifle component contains no weapon and is hidden"), !Mesh->GetSkeletalMeshAsset() && !Mesh->IsVisible() && Mesh->bHiddenInGame);
    TestTrue(TEXT("Inherited launcher contains no mesh and is hidden"),
        !Hostage->RPGLauncherMesh->GetStaticMesh() && !Hostage->RPGLauncherMesh->IsVisible());

    AOBEnemyCharacter* ThroughBase = Hostage;
    ThroughBase->SetEncounterCombatant(true);
    TestFalse(TEXT("Calls through the spawner's Enemy pointer cannot enable combat"), Hostage->IsEncounterCombatant());
    auto* Spawner = World->SpawnActor<AOBEncounterSpawner>();
    // Even an incorrect combat flag must honor this dedicated character's noncombat role.
    Spawner->PlacedEnemy = Hostage; Spawner->bParticipatesInCombat = true;
    Spawner->bMoveToStagingPoint = true;
    Spawner->PrepareEncounter(85);
    TestTrue(TEXT("Existing spawner can prepare the dedicated hostage class"), Spawner->IsPrepared() && Spawner->GetSpawnedEnemy() == Hostage);
    TestNull(TEXT("Noncombat preparation never creates an AI"), Hostage->GetController());
    auto* Player = World->SpawnActor<AOBPlayerCharacter>();
    auto* Action = Player->GetActionComponent();
    Action->BeginEncounter(85); Action->RegisterEnemy(Hostage); Action->FinalizeEncounterSetup();
    TestFalse(TEXT("Hostage cannot enter the combat registry"), Action->HasEncounterEnemies());
    TestFalse(TEXT("Hostage deaths are not subscribed as enemy kills"), Hostage->OnKilled.IsBoundToObject(Action));
    auto CountBullets = [&]() { int32 Count = 0; for (TActorIterator<AOBBulletActor> It(World); It; ++It) ++Count; return Count; };
    const int32 BulletsBefore = CountBullets();
    ThroughBase->SetCombatState(EOBEnemyCombatState::Attacking);
    ThroughBase->StartStateTree(); ThroughBase->StartEncounterBehavior(); ThroughBase->FireBullet();
    TestTrue(TEXT("Direct attack calls cannot leave the hostage's idle state"), Hostage->GetCombatState() == EOBEnemyCombatState::Idle);
    TestEqual(TEXT("Hostage attack calls produce no projectile"), CountBullets(), BulletsBefore);

    TInlineComponentArray<UBoxComponent*> Boxes(Hostage);
    UBoxComponent* Body = nullptr;
    for (auto* Box : Boxes) if (Box->GetFName() == TEXT("BodyHitBox")) Body = Box;
    if (!TestNotNull(TEXT("Dedicated hostage preserves the actual shot body hit box"), Body)) return false;
    TestTrue(TEXT("Hostage body hit box remains shootable"), Body->GetCollisionEnabled() == ECollisionEnabled::QueryOnly);
    const float BeforeHP = Hostage->GetCurrentHealth();
    FHitResult Hit(Hostage, Body, Body->GetComponentLocation(), FVector::ForwardVector);
    const float Applied = UGameplayStatics::ApplyPointDamage(Hostage, 10.f, FVector::ForwardVector, Hit, nullptr, Player, UDamageType::StaticClass());
    TestTrue(TEXT("Player point damage still reaches the dedicated hostage"), Applied > 0 && Hostage->GetCurrentHealth() < BeforeHP);
    TestFalse(TEXT("Hostage damage cannot add an enemy to the registry"), Action->HasEncounterEnemies());
    Spawner->ResetSpawnForQA();
    TestTrue(TEXT("Existing QA cleanup resets a dedicated hostage spawn"), !Spawner->IsPrepared() && !Spawner->GetSpawnedEnemy() && Hostage->IsActorBeingDestroyed());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBHostageFloorPlacementTest, "OperationBreakpoint.Stage.HostageFloorPlacement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOBHostageFloorPlacementTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false);GEngine->DestroyWorldContext(World); };
    auto* Floor=World->SpawnActor<AActor>();
    auto* Surface=NewObject<UBoxComponent>(Floor);
    Floor->SetRootComponent(Surface);Surface->SetBoxExtent(FVector(2000,2000,10));
    Surface->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Surface->SetCollisionObjectType(ECC_WorldStatic);Surface->SetCollisionResponseToAllChannels(ECR_Block);
    Surface->RegisterComponent();Floor->SetActorLocation(FVector(0,0,190));
    int32 Index=0;
    for (const TCHAR* Path:{
        TEXT("/Game/Blueprints/Hostage/BP_HostageCharacter.BP_HostageCharacter_C"),
        TEXT("/Game/Blueprints/Hostage/BP_HostageWorker.BP_HostageWorker_C")})
    {
        auto* Class=LoadClass<AOBHostageCharacter>(nullptr,Path);
        if (!TestNotNull(TEXT("Saved hostage class loads for floor placement"),Class)) return false;
        for (float Height:{230.f,350.f})
        {
            FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
            const FVector Authored(300.f*Index++,0,Height);
            auto* Hostage=World->SpawnActor<AOBHostageCharacter>(Class,Authored,FRotator::ZeroRotator,Params);
            if (!TestNotNull(TEXT("Hostage spawns with the encounter collision policy"),Hostage)) return false;
            const FVector MeshOffset=Hostage->GetMesh()->GetRelativeLocation();
            Hostage->DispatchBeginPlay();
            const FVector Grounded=Hostage->GetActorLocation();
            TestTrue(TEXT("Both penetrating and elevated spawn markers settle on the actual floor"),
                FMath::IsNearlyEqual(Grounded.Z,200.f+Hostage->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2.f,.05f));
            TestTrue(TEXT("Floor placement preserves the authored horizontal position"),
                FMath::IsNearlyEqual(Grounded.X,Authored.X,.05f) && FMath::IsNearlyEqual(Grounded.Y,Authored.Y,.05f));
            TestTrue(TEXT("Floor placement preserves the authored pose offset"),MeshOffset.Equals(Hostage->GetMesh()->GetRelativeLocation()));
            TestTrue(TEXT("Settled hostages remain stationary and unarmed"),
                Hostage->GetCharacterMovement()->MovementMode==MOVE_None && !Hostage->GetWeaponMesh());
        }
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBHostageShotIsolationTest, "OperationBreakpoint.Stage.HostageShotIsolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOBHostageShotIsolationTest::RunTest(const FString&)
{
    for (const TCHAR* HostagePath : {
        TEXT("/Game/Blueprints/Hostage/BP_HostageCharacter.BP_HostageCharacter_C"),
        TEXT("/Game/Blueprints/Hostage/BP_HostageWorker.BP_HostageWorker_C")})
    for (bool bShootHostage : {false, true})
    {
        auto* World = UWorld::CreateWorld(EWorldType::Game, false);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL; URL.AddOption(TEXT("game=/Script/Engine.GameModeBase")); World->SetGameMode(URL);
        ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
        auto* PC = World->SpawnActor<AOBPlayerController>(); World->AddController(PC);
        auto* Player = World->SpawnActor<AOBPlayerCharacter>(); PC->Possess(Player);
        auto* HostageClass = LoadClass<AOBHostageCharacter>(nullptr, HostagePath);
        auto* EnemyClass = LoadClass<AOBEnemyCharacter>(nullptr,
            TEXT("/Game/Blueprints/Enemy/BP_EnemyCharacter.BP_EnemyCharacter_C"));
        if (!TestNotNull(TEXT("Saved hostage class loads"), HostageClass) || !EnemyClass) return false;
        auto* Hostage = World->SpawnActor<AOBHostageCharacter>(HostageClass, FVector(500,300,200), FRotator::ZeroRotator);
        auto* Enemy = World->SpawnActor<AOBEnemyCharacter>(EnemyClass, FVector(1000,0,200), FRotator::ZeroRotator);
        if (!TestNotNull(TEXT("Saved hostage spawns"), Hostage) || !Enemy) return false;
        auto* VisibleMesh=Hostage->GetMesh()->GetSkeletalMeshAsset();
        if (!TestNotNull(TEXT("Saved hostage model exists after construction"),VisibleMesh)
            || !TestNotNull(TEXT("Saved hostage idle animation exists"),Hostage->IdleAnimation.Get())) return false;
        TestTrue(TEXT("Saved hostage pose matches its imported skeleton"),Hostage->IdleAnimation->GetSkeleton()==VisibleMesh->GetSkeleton());
        Hostage->DispatchBeginPlay(); Enemy->DispatchBeginPlay();
        TestTrue(TEXT("BeginPlay preserves the visible hostage model"),Hostage->GetMesh()->GetSkeletalMeshAsset()==VisibleMesh && Hostage->GetMesh()->IsVisible() && !Hostage->GetMesh()->bHiddenInGame);
        Enemy->GetMesh()->TickAnimation(0.f, false); Enemy->GetMesh()->RefreshBoneTransforms();

        for (float Time : {0.f, 2.1f, 4.4f})
        {
            Hostage->GetMesh()->SetPosition(Time, false);
            Hostage->GetMesh()->TickAnimation(0.f, false); Hostage->GetMesh()->RefreshBoneTransforms();
            TInlineComponentArray<UBoxComponent*> Boxes(Hostage);
            for (const UBoxComponent* Box : Boxes)
            {
                TestTrue(TEXT("Imported hostage boxes stay human-sized throughout the loop"), Box->GetScaledBoxExtent().GetMax() < 50.f);
                TestTrue(TEXT("Imported hostage offsets stay within the attached limb"), FVector::Distance(Box->GetComponentLocation(),
                    Hostage->GetMesh()->GetSocketLocation(Box->GetAttachSocketName())) < 35.f);
            }
        }

        auto* HostageSpawner = World->SpawnActor<AOBEncounterSpawner>();
        HostageSpawner->PlacedEnemy = Hostage; HostageSpawner->bParticipatesInCombat = false;
        HostageSpawner->PrepareEncounter(86);
        auto* Action = Player->GetActionComponent();
        Action->BeginEncounter(86); Action->RegisterEnemy(Enemy); Action->FinalizeEncounterSetup(); Action->SetExternalActionState(true);
        auto* Flow = Player->GetStageFlowComponent();
        Flow->Player = Player; Flow->bActive = true; Flow->Phase = EOBStagePhase::Combat; Flow->CurrentZone = 6;
        Flow->RemainingSeconds = 30.f; Flow->HandleEncounterTriggered(86);
        Flow->HandleHostageDamage(Enemy, 1.f, nullptr, PC, Player);
        TestTrue(TEXT("A combatant cannot be classified as a hostage by the failure callback"), Flow->GetPhase() == EOBStagePhase::Combat);

        FHitResult Resolved;
        const auto ShotHandle = PC->OnShotResolved.AddLambda([&Resolved](const FHitResult& Hit) { Resolved = Hit; });
        auto FireAt = [&](const FVector& Point, const FVector& Direction, float Distance) {
            Resolved = FHitResult(); PC->FireAlongRay(Point - Direction * Distance, Point + Direction * 50.f);
        };
        if (bShootHostage)
        {
            FireAt(Hostage->GetMesh()->GetSocketLocation(TEXT("head")), FVector::ForwardVector, 200.f);
            TestTrue(TEXT("The real controller ray resolves the visible hostage"), Resolved.GetActor() == Hostage);
            TestTrue(TEXT("An actual hostage shot selects mission failure"), !Flow->DidSucceed()
                && (Flow->GetPhase() == EOBStagePhase::Ending || Flow->GetPhase() == EOBStagePhase::Complete));
        }
        else
        {
            auto* Body = Cast<UBoxComponent>(Enemy->GetDefaultSubobjectByName(TEXT("BodyHitBox")));
            if (!TestNotNull(TEXT("Saved enemy body hit box exists"), Body)) return false;
            auto* Bullet = World->SpawnActor<AOBBulletActor>(Body->GetComponentLocation() - FVector(300,0,0), FRotator::ZeroRotator);
            TestNotNull(TEXT("An incoming projectile is placed on the actual shot ray"), Bullet);
            const float HostageHealth = Hostage->GetCurrentHealth();
            FireAt(Body->GetComponentLocation(), FVector::ForwardVector, 1200.f);
            TestTrue(TEXT("Neither an off-axis hostage nor an incoming bullet intercepts the enemy body ray"), Resolved.GetActor() == Enemy);
            TestTrue(TEXT("An enemy body hit damages the enemy and keeps final combat active"),
                Enemy->GetCurrentHealth() < Enemy->GetMaxHealth() && Flow->GetPhase() == EOBStagePhase::Combat);
            TestEqual(TEXT("Enemy shots leave the nearby hostage unharmed"), Hostage->GetCurrentHealth(), HostageHealth);
            FireAt(Enemy->GetMesh()->GetSocketLocation(TEXT("head")), FVector::ForwardVector, 1200.f);
            TestTrue(TEXT("The last actual enemy headshot selects mission success"), Resolved.GetActor() == Enemy
                && Enemy->GetCombatState() == EOBEnemyCombatState::Dead && Flow->DidSucceed());
        }
        PC->OnShotResolved.Remove(ShotHandle); Flow->bActive = false;
    }
    return !HasAnyErrors();
}
#endif
